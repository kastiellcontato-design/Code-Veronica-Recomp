"""Package ready-made split lists and guides for official catalog activation."""
from pathlib import Path
import argparse,hashlib,json,zipfile,xml.etree.ElementTree as ET
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]/'livesplit'
files={name:(root/name).read_bytes() for name in ['LICENSE-recvx.txt','LEIA-ME.txt','README.txt']}
reports=[]
for source in sorted((root/'splits').glob('*.lss')):
 data=source.read_bytes();doc=ET.fromstring(data)
 assert doc.findtext('GameName')=='Resident Evil: Code: Veronica X (PS3 Recomp)'
 assert not any((p.text or '').strip() for p in doc.iter('ScriptPath'))
 segments=doc.findall('./Segments/Segment')
 assert len(segments)==63 and all(s.findtext('Icon') for s in segments)
 files[source.name]=data
 reports.append({'split':source.name,'segments':len(segments),'sha256':hashlib.sha256(data).hexdigest()})
assert len(reports)==2
args.output_dir.mkdir(parents=True,exist_ok=True)
output=args.output_dir/'CodeVeronica-LiveSplit-Speedrun-v1.0.zip'
assert not output.exists(),output
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for name,data in files.items():z.writestr('CodeVeronica-LiveSplit/'+name,data)
with zipfile.ZipFile(output) as z:
 assert z.testzip() is None
 assert not any(name.lower().endswith('.asl') for name in z.namelist())
(args.output_dir/'livesplit-verification.json').write_text(json.dumps(reports,indent=2))
print(output)
