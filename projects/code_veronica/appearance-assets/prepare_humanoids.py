"""Offline, local-only conversion of selected PS3 humanoids into a player rig.
Original archives are read-only. Generated game data must never be committed.
"""
from pathlib import Path
import struct,zlib,json,sys,math
_here=Path(__file__).resolve().parent
sys.path.insert(0,str(_here if (_here/'prepare_lens_archive.py').exists() else _here.parent))
from prepare_lens_archive import resources,checksum
U=lambda b,p:struct.unpack_from('>I',b,p)[0]
H=lambda b,p:struct.unpack_from('>H',b,p)[0]
S=lambda b,p:struct.unpack_from('>h',b,p)[0]
F=lambda b,p:struct.unpack_from('>f',b,p)[0]
P=lambda n:struct.pack('>I',n)
def parts(b):
 at=0
 while at+4<=len(b):
  n=U(b,at);at+=4
  if n==0xffffffff:return
  if n>len(b)-at:raise ValueError('part bounds')
  yield b[at:at+n];at+=n

def player(r):
 b=next(x[4] for x in r if x[1]==0x74afe18c);out=[];at=0
 for _ in range(3):n=U(b,at);at+=4;out.append(b[at:at+n]);at+=n
 return out

def rig(m):
 count=H(m,6);header=H(m,4);root=U(m,12);par=[-1]*count;nodes=[]
 assert header==16 and count<64
 for i in range(count):
  a=header+root+52*i;rec=m[a:a+52];assert len(rec)==52
  assert all(U(rec,20+4*j)==0 for j in range(3)) and all(abs(F(rec,32+4*j)-1)<1e-5 for j in range(3))
  def link(p):
   q=U(rec,p);return None if q==0xffffffff else (q-root)//52
  nodes.append({'raw':rec,'p':[F(rec,8+4*j) for j in range(3)],'child':link(44),'sib':link(48)})
 seen=set()
 def walk(i,parent):
  assert i not in seen and 0<=i<count;seen.add(i);par[i]=parent
  if nodes[i]['child'] is not None:walk(nodes[i]['child'],i)
  if nodes[i]['sib'] is not None:walk(nodes[i]['sib'],parent)
 walk(0,-1);assert len(seen)==count
 for i,n in enumerate(nodes):
  n['world']=[n['p'][j]+(nodes[par[i]]['world'][j] if par[i]>=0 else 0) for j in range(3)];n['parent']=par[i]
 return nodes

def mappings(key,n):
 if key=='en51a00':assert n==18;return [0,1,2,3,4,5,7,8,9,11,12,13,14,15,16,18,19,20]
 if key.startswith('en01'):assert n==18;return [0,1,14,15,16,18,19,20,2,3,4,5,7,8,9,11,12,13]
 if key=='en26a00':assert n==20;return [0,1,14,15,16,14,18,19,20,18,2,3,4,5,7,8,9,11,12,13]
 if key=='en12a00':assert n==22;return list(range(22))
 if key=='en95a00':assert n==27;return [0,1,2,3,4,5,5,5,5,5,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21]
 raise ValueError(key)

def skin_tables(skin,n):
 assert skin[:4]==b'SKIN';at=4;tables={}
 for i in range(n):
  flags=U(skin,at);at+=4;assert flags in (0,1,3)
  if not flags:continue
  owner=U(skin,at);at+=4
  if flags&2:
   cnt=U(skin,at);at+=4;table=skin[at:at+4*cnt];at+=4*cnt;assert len(table)==cnt*4
   assert i not in tables;tables[i]=table
 assert len(skin)-at in (0,4) and (not skin[at:] or skin[at:] in (bytes(4),b'\xff'*4));return tables

def texlist(m):
 base=16+U(m,8);ptr,count=U(m,base),U(m,base+4);out=[]
 for i in range(count):
  a=16+ptr+12*i;name=16+int.from_bytes(m[a:a+4],'little');end=m.index(0,name);out.append((m[name:end],U(m,a+4),U(m,a+8)))
 return out

def mdl_new(source,template,key):
 nodes=rig(source);target=rig(template);mapping=mappings(key,len(nodes));globals=[None]*22
 for i,to in enumerate(mapping):
  if globals[to] is None:globals[to]=nodes[i]['world'][:]
 # Clavicles and toes missing in zombies remain zero-length helper joints.
 for i in range(22):
  if globals[i] is None:globals[i]=globals[target[i]['parent']][:]
 assert globals[7][0]>.5 and globals[11][0]<-.5
 assert globals[14][1]<globals[1][1] and globals[18][1]<globals[1][1]
 assert globals[9][1]>globals[14][1] and globals[13][1]>globals[18][1]
 skin=mdl_new.skin;tables=skin_tables(skin,len(nodes));verts=bytearray();weights=bytearray();poly=bytearray();vertex_count=0;triangles=0
 for i,node in enumerate(nodes):
  g=U(node['raw'],4)
  if g==0xffffffff:continue
  gp=16+g;vp=16+U(source,gp);pp=16+U(source,gp+4)
  assert source[vp]==0x33
  first,count=H(source,vp+4),H(source,vp+6);assert source[vp+64+count*32]==255
  table=tables.get(i)
  if table is not None:assert len(table)==count*4
  for k in range(count):
   v=bytearray(source[vp+64+k*32:vp+64+(k+1)*32]);assert len(v)==32
   # Like the player's own body, vertices are stored relative to the hips.
   for axis in range(3):struct.pack_into('>f',v,axis*4,F(v,axis*4)+node['world'][axis]-globals[1][axis])
   verts+=v
   w=bytearray(table[k*4:k*4+4]) if table is not None else bytearray([i,9,i,9])
   assert w[0]<len(nodes) and (w[2]<len(nodes) or (w[2]==255 and w[3]==0));w[0]=mapping[w[0]];w[2]=mapping[w[2]] if w[2]!=255 else 255;weights+=w
  # Explicit local material reset avoids inheriting the preceding node's state.
  poly+=bytes([8,0])+struct.pack('>H',0)
  at=pp;ended=False
  for _ in range(4096):
   op=source[at]
   if op==255:ended=True;break
   size=4 if op==8 else 4+H(source,at+2)*2
   c=bytearray(source[at:at+size]);assert len(c)==size
   if op in (65,66):
    num=H(c,4);assert not num>>14;p=6
    for j in range(num):
     n=abs(S(c,p));p+=2;assert n>=3;triangles+=n-2
     for v in range(n):
      old=H(c,p);assert first<=old<first+count;struct.pack_into('>H',c,p,vertex_count+old-first)
      if op==65:
       for axis in (2,4):uv=S(c,p+axis)*4;assert -32768<=uv<=32767;struct.pack_into('>h',c,p+axis,uv)
      p+=6
    assert p<=size and size-p<=2;c[0]=66
   elif op not in (8,17,18,19):raise ValueError(f'unsupported polygon {op}')
   poly+=c;at+=size
  assert ended;vertex_count+=count
 assert vertex_count<65536 and len(weights)==4*vertex_count
 # The player's root has no skin entry; no vertex may be bound to it.
 assert all(weights[j]!=0 for j in range(0,len(weights),2) if weights[j]!=255)
 poly+=b'\xff\0\0\0'
 # All source geometry is stored in one hip-relative vertex buffer. SKIN keeps
 # both original joint influences and weights, with only joint indices remapped.
 payload=bytearray(22*52)
 def append(data,align=4):
  payload.extend(bytes((-len(payload))%align));pos=len(payload);payload.extend(data);return pos
 # A chunk model is 24 bytes, including bounding center and radius. A
 # NEGATIVE radius marks relative pointers for the original relocation code.
 xyz=[struct.unpack_from('>3f',verts,i*32) for i in range(vertex_count)]
 center=[(min(v[a] for v in xyz)+max(v[a] for v in xyz))*.5 for a in range(3)]
 radius=max(math.dist(v,center) for v in xyz)
 assert math.isfinite(radius) and radius>0
 geometry=append(struct.pack('>II4f',0,0,*center,-radius));vhead=bytearray(64)
 vhead[0]=0x33;struct.pack_into('>H',vhead,2,(64+len(verts))//4-1);struct.pack_into('>HH',vhead,4,0,vertex_count)
 vpos=append(vhead+verts+b'\xff\0\0\0',16);ppos=append(poly);struct.pack_into('>II',payload,geometry,vpos,ppos)
 names=texlist(source);tpos=append(bytes(8));entries=append(bytes(12*len(names)))
 struct.pack_into('>II',payload,tpos,entries,len(names))
 for j,(name,attr,address) in enumerate(names):
  string=append(name+b'\0');struct.pack_into('<III',payload,entries+j*12,string,attr,0)
 for i,t in enumerate(target):
  # Keep the player's own evaluation flags and put the body in node1, as in
  # pl00-pl04. With the merged body in the root, firing turned the screen white.
  flags=U(t['raw'],0);assert (i==1)==(not flags&8)
  a=i*52;rec=bytearray(t['raw']);struct.pack_into('>II',rec,0,flags,geometry if i==1 else 0xffffffff)
  parent=t['parent'];pos=[globals[i][axis]-(globals[parent][axis] if parent>=0 else 0) for axis in range(3)]
  if flags&1:assert all(abs(x)<1e-4 for x in pos)
  struct.pack_into('>3f',rec,8,*pos)
  for field,link in ((44,t['child']),(48,t['sib'])):struct.pack_into('>I',rec,field,0xffffffff if link is None else link*52)
  payload[a:a+52]=rec
 header=b'MDL\xbc'+struct.pack('>HHII',16,22,tpos,0)
 payload+=bytes((-len(payload))%4);new=header+payload
 # SKIN pointer passed to the game skips the magic. Every target bone
 # references buffer0; node1 owns the merged geometry and influence table.
 # The root has no entry, as in the player's SKIN. The hands keep theirs
 # because enemy hands are bound to joints 9/13.
 sk=bytearray(b'SKIN')
 for i in range(22):
  if i==0:sk+=P(0);continue
  sk+=P(3 if i==1 else 1)+P(0)
  if i==1:sk+=P(vertex_count)+weights
 sk+=P(0xffffffff)
 # Two texture-only expression MDLs select the SAME bank, so player blinking
 # never switches back to the original five-entry Claire material table.
 def expression():
  data=bytearray(8+12*len(names));struct.pack_into('>II',data,0,8,len(names))
  for j,(name,attr,address) in enumerate(names):
   pos=len(data);data+=name+b'\0';struct.pack_into('<III',data,8+12*j,pos,attr,0)
  data+=bytes((-len(data))%4)
  return b'MDL\x13'+struct.pack('>HHII',16,0,0,0xffffffff)+data
 expr=expression();block=b''.join(P(len(x))+x for x in (sk,new,expr,expr))+P(0xffffffff)
 assert len(block)<0x30000
 return block,{'source_nodes':len(nodes),'vertices':vertex_count,'triangles':triangles,'mapping':mapping,'target_globals':globals}

def archive(records,destination):
 table=bytearray(b'\0CRA'+struct.pack('>HH',8,len(records)));payload=bytearray();start=8+len(records)*80
 for name,kind,data in records:
  name=name.encode() if isinstance(name,str) else name;assert len(name)<64
  comp=zlib.compress(data,9);table+=name.ljust(64,b'\0')+struct.pack('>4I',kind,len(comp),(len(data)<<3)|2,start+len(payload));payload+=comp
 logical=table+payload;logical+=bytes((-len(logical))%16)
 physical=bytearray(b'\0SFH'+struct.pack('>III',0x10001,len(logical),0))
 for at in range(0,len(logical),0x1fff0):c=bytes(logical[at:at+0x1fff0]);physical+=c+checksum(c)
 destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(physical)

def generate(system,out,selection):
 donor={i:resources(system/f'pl{i:02}.arc') for i in (0,1)};baseline=next(x for x in parts(player(donor[0])[0]) if x[:3]==b'MDL' and H(x,6)==22)
 reports=[]
 for number,item in enumerate(selection,5):
  key=item['id'];stem=f'cvskin{number:02}';r=resources(system.parent/'rdx_lnk'/item['room']);name=next(x[0].split(b'\0')[0].decode() for x in r if x[1]==0x108f442e and x[0].split(b'\0')[0].endswith(('\\'+key).encode()));model=next(x[4] for x in r if x[1]==0x108f442e and x[0].split(b'\0')[0].decode()==name)
  chunks=list(parts(model));skin=chunks[0];mdl=next(x for x in chunks if x[:3]==b'MDL');female=key.startswith(('en12','en95'));base=donor[0 if female else 1];bp=player(base)
  mdl_new.skin=skin;converted,report=mdl_new(mdl,baseline,key)
  # Original engine player reader expects its own ALD resource and DDS names.
  primary=next(x for x in base if x[1]==0x74afe18c)
  dds=next(x for x in r if x[1]==0x6505b384 and x[0].split(b'\0')[0].decode()==name)
  texnames=[];dd=dds[4];p=12
  for _ in range(int.from_bytes(dd[8:12],'little')):
   attr,index,length=struct.unpack_from('<III',dd,p);nm=dd[p+12:p+12+length].split(b'\0')[0];p+=20+length;texnames.append(nm)
  texture_records=[next(x for x in r if x[1]==0x241f5deb and x[0].split(b'\0')[0]==nm) for nm in texnames]
  # Source DDS and SCRA retain their genuine texture dependencies. Borrowing
  # a donor DDS resource name made the engine return Claire/Chris's cached DDS.
  source_scra=next(x for x in r if x[1]==0x73850d05 and x[0].split(b'\0')[0].decode()==name+'_tex')
  source_count=int.from_bytes(source_scra[4][6:8],'big');assert source_count==len(texture_records)+1
  primary_name=primary[0].split(b'\0')[0].rsplit(b'\\',1)[0]+b'\\'+stem.encode()
  record=[(primary_name,primary[1],b''.join(P(len(x))+x for x in (converted,bp[1],bp[2])))]
  record += [(x[0].split(b'\0')[0],x[1],x[4]) for x in texture_records]
  record += [(dds[0].split(b'\0')[0],dds[1],dd),(source_scra[0].split(b'\0')[0],source_scra[1],source_scra[4])]
  dest=out/(stem+'.arc');archive(record,dest)
  reports.append({**item,**report,'choice':number,'stem':stem,'motion':'Claire' if female else 'Chris','model_bytes':len(converted),'archive_bytes':dest.stat().st_size})
  print(system.parent.name,stem,key,report['source_nodes'],'->22',report['vertices'],'vertices',flush=True)
 return reports
if __name__=='__main__':
 system,out=map(Path,sys.argv[1:3]);catalog=json.loads(Path(sys.argv[3]).read_text(encoding='utf-8'));selected=[x for x in catalog if x['id'] not in ('en52a00','en52a01')];assert len(selected)==28
 result=generate(system,out,selected);(out/'manifest.json').write_text(json.dumps(result,indent=2),encoding='utf-8')







