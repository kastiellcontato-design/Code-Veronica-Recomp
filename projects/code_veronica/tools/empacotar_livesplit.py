"""Package the dedicated port ASL and route presets with user-authored splits."""
from pathlib import Path
import argparse,hashlib,json,re,zipfile,xml.etree.ElementTree as ET
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',type=Path,required=True)
args=parser.parse_args()
port=Path(__file__).resolve().parents[1] if Path(__file__).parent.name=='tools' else Path('D:/ps3recomp/projects/code_veronica')
root=port/'livesplit';base=(root/'recvx-ps3recomp.asl').read_text(encoding='utf-8-sig')
pattern=re.compile(r'((?:settings\.Add|vars\.AddSplit|vars\.Add(?:Item|Killed|Events)Split)\((?:[^,\n]+,\s*)?"([^"]+)",\s*)(true|false)(?=\s*,)')
base_matches=list(pattern.finditer(base));assert len(base_matches)>100
files={'recvx-ps3recomp.asl':base.encode('utf-8'),'LICENSE-recvx.txt':(root/'LICENSE-recvx.txt').read_bytes()}
reports=[]
for source in sorted((root/'splits').glob('*.lss')):
 data=source.read_bytes();doc=ET.fromstring(data)
 settings={e.attrib['id']:e.text.strip().lower() for e in doc.findall('./AutoSplitterSettings/CustomSettings/Setting')}
 script_ids={m.group(2) for m in base_matches}
 assert set(settings)<=script_ids
 assert all(key.startswith('infogroup') for key in script_ids-set(settings)),script_ids-set(settings)
 preset='recvx-port-AnyPercent.asl' if 'Any%' in source.name else 'recvx-port-SRank.asl'
 script=pattern.sub(lambda m:m.group(1)+settings.get(m.group(2),m.group(3)),base)
 actual={m.group(2):m.group(3) for m in pattern.finditer(script)}
 assert {key:actual[key] for key in settings}==settings
 assert all(not p in script for p in ['state("rpcs3")','state("pcsx2")','state("Dolphin")'])
 assert 'Kapdap' in script and 'Kastiel' in script
 segments=doc.findall('./Segments/Segment');assert len(segments)==63 and all(s.findtext('Icon') for s in segments)
 files[source.name]=data;files[preset]=script.encode('utf-8')
 reports.append({'split':source.name,'preset':preset,'segments':len(segments),'settings':len(settings)})
assert len(reports)==2
files['LEIA-ME.txt']=(root/'LEIA-ME.txt').read_bytes()
files['README.txt']=(root/'README.txt').read_bytes()
args.output_dir.mkdir(parents=True,exist_ok=True)
output=args.output_dir/'CodeVeronica-LiveSplit-Speedrun-v1.0.zip'
assert not output.exists(),output
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for name,data in files.items():z.writestr('CodeVeronica-LiveSplit/'+name,data)
with zipfile.ZipFile(output) as z:assert z.testzip() is None
digest=hashlib.sha256(output.read_bytes()).hexdigest()
(args.output_dir/'livesplit-verification.json').write_text(json.dumps(reports,indent=2))
print(output)
