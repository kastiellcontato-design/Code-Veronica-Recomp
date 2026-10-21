"""Build an optional local overlay; never overwrite original game archives."""
import sys,struct,zlib,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from inspect_models import unwrap_sfh
def resources(path):
 b=unwrap_sfh(path.read_bytes());out=[]
 for i in range(struct.unpack_from('>H',b,6)[0]):
  at=8+i*80;kind,n,cap,off=struct.unpack_from('>4I',b,at+64)
  raw=b[off:off+n];data=zlib.decompress(raw) if raw[0]&15==8 and int.from_bytes(raw[:2],'big')%31==0 else raw
  out.append([b[at:at+64],kind,cap,raw,data])
 return out
def replace(r,data):
 compressed=r[3][0]&15==8 and int.from_bytes(r[3][:2],'big')%31==0
 r[2]=(len(data)<<3)|(r[2]&7);r[3]=zlib.compress(data,9) if compressed else data;r[4]=data
def checksum(chunk):
 rotations=[7,2,5,4,6,3,1,4,2,6,7,3,5,7,1,5]
 seeds=[135,85,7,181,75,4,165,174,199,103,190,203,1,80,88,68]
 out=[]
 for j,(r,s) in enumerate(zip(rotations,seeds)):
  v=(sum(chunk[j::16])+s)&255;out.append(((v<<r)|(v>>(8-r)))&255)
 return bytes(out)
def build(system,destination):
 original=system/'pl03.arc';room=system.parent/'rdx_lnk/rm_1002.arc'
 # Verify the checksum implementation against every block of the input archive.
 physical=original.read_bytes()
 for at in range(16,len(physical)-16,0x20000):
  end=min(at+0x1fff0,len(physical)-16)
  assert checksum(physical[at:end])==physical[end:end+16]
 player=resources(original);accessory=resources(room)
 prefix=next(r[0].split(b'\0')[0] for r in accessory if r[1]==0x7db518e8 and r[0].split(b'\0')[0].endswith(b'\\ob_1127'))
 lens=next(r for r in accessory if r[1]==0x241f5deb and r[0].split(b'\0')[0]==prefix+b'\\tex0000_BM')
 ddsp=next(r[4] for r in accessory if r[1]==0x6505b384 and r[0].split(b'\0')[0]==prefix)
 scra=next(r[4] for r in accessory if r[1]==0x73850d05 and r[0].split(b'\0')[0]==prefix+b'_tex')
 d=next(r for r in player if r[1]==0x6505b384)
 assert int.from_bytes(d[4][8:12],'little')==12 and int.from_bytes(ddsp[8:12],'little')==1
 replace(d,d[4][:8]+struct.pack('<I',13)+d[4][12:]+ddsp[12:])
 s=next(r for r in player if r[1]==0x73850d05)
 count=int.from_bytes(s[4][6:8],'big');assert count==13
 replace(s,s[4][:6]+struct.pack('>H',14)+s[4][8:-8]+scra[8:16]+s[4][-8:])
 player.insert(player.index(d),lens)
 # Keep the exact original logical/physical length: the engine may stat the
 # original path before its open is overlaid. Recompression changes no pixels.
 for r in player:
  if r[3][0]&15==8 and int.from_bytes(r[3][:2],'big')%31==0:r[3]=zlib.compress(r[4],9)
 table=bytearray(b'\0CRA'+struct.pack('>HH',8,len(player)));payload=bytearray();offset=8+80*len(player)
 for name,kind,cap,raw,data in player:
  table+=name+struct.pack('>4I',kind,len(raw),cap,offset+len(payload));payload+=raw
 logical=table+payload;original_size=int.from_bytes(physical[8:12],'big')
 if len(logical)>original_size:raise ValueError('Overlay cannot preserve original file length')
 logical+=bytes(original_size-len(logical))
 output=bytearray(b'\0SFH'+struct.pack('>III',0x10001,len(logical),0))
 for at in range(0,len(logical),0x1fff0):
  chunk=bytes(logical[at:at+0x1fff0]);chunk+=bytes((-len(chunk))%16);output+=chunk+checksum(chunk)
 destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(output)
 assert len(output)==len(physical)
 assert unwrap_sfh(output)==logical
 assert len([r for r in resources(destination) if r[1]==0x241f5deb])==13
 print('Original unchanged:',hashlib.sha256(original.read_bytes()).hexdigest());print('Overlay:',destination,len(output))
if __name__=='__main__':build(Path(sys.argv[1]),Path(sys.argv[2]))

