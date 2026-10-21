"""Read-only CVX PS3 SFH/ARC inventory. Never modifies game archives."""
import argparse, hashlib, json, struct, zlib
from pathlib import Path

def unwrap_sfh(data):
    if data[:4] != b'\0SFH':
        return data
    if struct.unpack_from('>I',data,4)[0] != 0x00010001:
        raise ValueError('Unknown SFH version')
    size=struct.unpack_from('>I',data,8)[0]
    # SFH headers occupy the first 16 bytes of each physical 128 KiB block.
    raw=b''.join(data[i+16:i+0x20000] for i in range(0,len(data),0x20000))
    if size>len(raw):raise ValueError('Truncated SFH payload')
    return raw[:size]

def entries(path):
    raw=unwrap_sfh(path.read_bytes())
    if raw[:4]!=b'\0CRA':raise ValueError('Not a big-endian ARC')
    version,count=struct.unpack_from('>HH',raw,4)
    if version!=8 or count>4096 or 8+80*count>len(raw):raise ValueError('Unsupported ARC table')
    result=[]
    for i in range(count):
        at=8+80*i
        name=raw[at:at+64].split(b'\0',1)[0].decode('ascii')
        kind,compressed,sizeflag,offset=struct.unpack_from('>4I',raw,at+64)
        size=sizeflag>>3
        if offset<8+80*count or offset+compressed>len(raw) or size>128*1024*1024:
            raise ValueError('Invalid entry bounds')
        payload=raw[offset:offset+compressed]
        is_zlib=len(payload)>=2 and payload[0]&15==8 and int.from_bytes(payload[:2],'big')%31==0
        data=zlib.decompress(payload) if is_zlib else payload
        if len(data)>size:raise ValueError('Resource exceeds declared capacity')
        result.append({'name':name,'type':f'{kind:08X}','compressed_bytes':compressed,
            'bytes':len(data),'size_field':f'{sizeflag:08X}','declared_shift3':size,'sha256':hashlib.sha256(data).hexdigest(),
            'zlib_verified':is_zlib,'prefix_hex':data[:40].hex(),'skin_tag_offset':data.find(b'SKIN')})
    return {'archive':str(path),'archive_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'entries':result}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();files=sorted(args.directory.glob('pl??.arc'))
    if not files:raise SystemExit('No player archives')
    report=[entries(f) for f in files]
    args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    for f in report:print(Path(f['archive']).name,len(f['entries']),'verified entries')
if __name__=='__main__':main()

