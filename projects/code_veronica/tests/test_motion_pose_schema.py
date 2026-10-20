"""Exercise F9 ABI compatibility and reject malformed vertex-state tails."""
import importlib.util
from pathlib import Path
import struct
import tempfile

spec = importlib.util.spec_from_file_location('motion_analyzer', Path(__file__).parents[1] / 'tools/analyze_motion_capture.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def snapshot(version, tail=b'', operations=0):
    stride = 336 if version == 2 else 320
    header = [0x4d435031, version, 0, 0, 0, 0, 0, 0, 0, 0, 20, 24, 16, 32, operations, stride]
    return struct.pack('<16I', *header) + bytes(operations * stride) + tail

def extra(banks=0, programs=0):
    return struct.pack('<4I', 0x3256434d, banks, programs, 512)

with tempfile.TemporaryDirectory(dir=Path.cwd()) as folder:
    path = Path(folder) / 'frame.pose'
    def check(data):
        path.write_bytes(data)
        return module.inspect_pose(path)
    assert check(snapshot(1))['capture_state']['schema'] == 1
    assert check(snapshot(2, extra()))['capture_state']['operations'] == 0
    bank = struct.pack('<I', 7) + bytes(512 * 16)
    source = b'VSOutput vf_main(VSInput input) { return o; }'
    program = struct.pack('<2I', 0x12345678, len(source)) + source
    result = check(snapshot(2, extra(1, 1) + bank + program, 3))['capture_state']
    assert result['operations'] == 3 and result['vertex_banks'] == 1
    assert result['program_hashes'] == ['12345678']
    valid = snapshot(2, extra(1, 1) + bank + program)
    for malformed in (snapshot(2), valid[:-1], valid+b'x', snapshot(2, extra(1, 0)),
                      snapshot(2, extra(2, 0)+bank+bank), snapshot(2, extra(0,1)+struct.pack('<2I',0,0xffffffff))):
        try:
            check(malformed)
        except (ValueError, struct.error):
            pass
        else:
            raise AssertionError('Malformed capture accepted')
print('PASS F9 pose v1/v2: op stride, full VP banks/programs, truncation, duplicate and size guards')
