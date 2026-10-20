"""Inspect an F9 burst without modifying the game or its capture files.

Usage: python analyze_motion_capture.py <motion-captures/session_...>
Reports are evidence candidates; thresholds are diagnostics, not render fixes.
"""
import argparse
import array
import json
import math
from pathlib import Path
import struct
import sys


def inspect_pose(path):
    data = path.read_bytes()
    h = struct.unpack_from('<16I', data)
    magic, version, nv, nd, packed, mode = h[:6]
    if magic != 0x4D435031 or version not in (1, 2) or h[10:14] != (20, 24, 16, 32):
        raise ValueError('Unknown pose schema / ABI')
    offset = 64

    def floats(count):
        nonlocal offset
        n = count * 4
        if offset + n > len(data):
            raise ValueError('Truncated snapshot')
        values = array.array('f')
        values.frombytes(data[offset:offset+n])
        if sys.byteorder != 'little':
            values.byteswap()
        offset += n
        return values

    offset += nv * 20  # original xyz, uv identity and stamp
    final = floats(nv * 3)
    current = [struct.unpack_from('<Q4I', data, offset + i * 24) for i in range(nd)]
    offset += nd * 24
    bindings = [struct.unpack_from('<Q4I', data, offset + i * 24) for i in range(nd)]
    offset += nd * 24 + nd * 16  # tracks: double changed_at, float alpha, int active
    sources = struct.unpack_from(f'<{nd}i', data, offset) if nd else ()
    offset += nd * 8  # vertex sources + camera sources
    old, new = floats(packed * 3), floats(packed * 3)
    offset += nd * 32 * 4  # previous camera
    cameras, submitted = floats(nd * 32), floats(nd * 32)
    # Per-op metadata plus up to 16 frozen fragment float4 constants.
    operations, stride = h[14:16]
    lighting = []
    if operations and stride != (336 if version == 2 else 320):
        raise ValueError('Unknown lighting-record stride')
    for i in range(operations):
        row = struct.unpack_from('<16I', data, offset)
        if row[1] == 0x7EA4C4E7:
            lighting.append({'op': row[0], 'fragment_program': f'{row[1]:08x}',
                             'cb': row[3], 'target': f'{row[4]:08x}',
                             'pose_accepted': bool(row[9]),
                             'fragment_constants': struct.unpack_from(f'<{row[12]*4}f', data, offset+64)})
        offset += stride
    state_summary = {'schema': version, 'operations': operations, 'vertex_banks': 0, 'vertex_programs': 0}
    if version == 2:
        if offset + 16 > len(data):
            raise ValueError('Missing vertex-state extension')
        tag, bank_count, program_count, constants = struct.unpack_from('<4I', data, offset)
        offset += 16
        if tag != 0x3256434d or constants != 512 or bank_count > 24576 or program_count > 24576:
            raise ValueError('Unknown vertex-state schema / limits')
        bank_slots = set()
        for _ in range(bank_count):
            end = offset + 4 + constants * 16
            if end > len(data):
                raise ValueError('Truncated vertex bank')
            slot = struct.unpack_from('<I', data, offset)[0]
            if slot >= 24576 or slot in bank_slots:
                raise ValueError('Invalid / duplicate vertex bank')
            bank_slots.add(slot)
            offset = end
        program_hashes = set()
        for _ in range(program_count):
            if offset + 8 > len(data):
                raise ValueError('Truncated vertex program header')
            code, size = struct.unpack_from('<2I', data, offset)
            offset += 8
            if size > 4 * 1024 * 1024 or offset + size > len(data):
                raise ValueError('Truncated / oversized vertex program')
            data[offset:offset+size].decode('utf-8')
            program_hashes.add(f'{code:08x}')
            offset += size
        state_summary.update(vertex_banks=bank_count, vertex_programs=program_count,
                             program_hashes=sorted(program_hashes))
    if offset != len(data):
        raise ValueError('Snapshot length mismatch')
    if mode == 2:
        return {'capture_state': state_summary, 'final_positions_available': False, 'note': 'GPU compute result not read back'}

    def edge(a, ia, ib):
        return sum((a[ia*3+k] - a[ib*3+k])**2 for k in range(3))

    suspects = []
    changed_shadow_cameras = 0
    for d, binding in enumerate(bindings):
        _, first, count, _, flags = binding
        if flags & 1 and cameras[d*32:(d+1)*32] != submitted[d*32:(d+1)*32]:
            changed_shadow_cameras += 1
        source = sources[d]
        if not (0 <= source < nd and first + count <= nv):
            continue
        _, start, source_count, _, _ = current[source]
        if count != source_count or start + count > packed:
            continue
        worst = 0.0
        for v in range(0, count - 2, 3):
            for a, b in ((0, 1), (1, 2), (2, 0)):
                ia, ib = start + v + a, start + v + b
                baseline = max(edge(old, ia, ib), edge(new, ia, ib), 1e-8)
                ratio = edge(final, first + v + a, first + v + b) / baseline
                if math.isfinite(ratio):
                    worst = max(worst, ratio)
        if worst > 4.0:
            suspects.append({'draw': d, 'key': f'{binding[0]:016x}', 'flags': flags,
                             'edge_stretch_ratio': math.sqrt(worst), 'source': source})
    return {'capture_state': state_summary, 'final_positions_available': True,
            'shadow_camera_changes': changed_shadow_cameras,
            'shadow_passes': lighting,
            'suspect_draws': sorted(suspects, key=lambda d: -d['edge_stretch_ratio'])[:12]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    args = parser.parse_args()
    frames = []
    for path in sorted(args.session.glob('frame_*.json')):
        row = json.loads(path.read_text(encoding='utf-8'))
        row['sample'] = path.stem
        pose = path.with_suffix('.pose')
        if pose.exists() and row.get('pose_bytes'):
            row['geometry_analysis'] = inspect_pose(pose)
        frames.append(row)
    if not frames:
        parser.error('No captured frame JSONs found')
    ticks = [f['qpc'] / f['qpc_frequency'] for f in frames if f.get('qpc_frequency')]
    report = {'session': str(args.session.resolve()), 'samples': len(frames),
              'span_seconds': ticks[-1] - ticks[0] if len(ticks) > 1 else None,
              'note': 'Captures affect timing; do not use this burst as an FPS benchmark. '
                      'Geometry candidates do not prove a visual bug.',
              'frames': frames}
    output = args.session / 'analysis.json'
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'{len(frames)} samples analyzed -> {output}')


if __name__ == '__main__':
    main()
