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
pt='''CODE VERONICA - LIVESPLIT SPEEDRUN v1.0

Autosplitter original de Kapdap. Adaptacao para o port PC por Kastiel.
Licenca e creditos originais preservados em LICENSE-recvx.txt.
Splits e icones organizados por Kastiel.

1. Extraia este ZIP para uma pasta permanente. Instale/abra o LiveSplit.
2. Use Open Splits > From File para abrir o .lss Any% ou S-Rank.
3. Em Edit Splits, clique Deactivate se o autosplitter de emuladores estiver
   ativo. Se aparecer Activate, deixe desativado. Use apenas um autosplitter.
4. Em Edit Layout, adicione + > Control > Scriptable Auto Splitter.
5. Nas configuracoes desse componente, use Browse e escolha:
   Any%: recvx-port-AnyPercent.asl
   S-Rank: recvx-port-SRank.asl
6. Marque Start e Split. Os eventos de cada rota ja vem pre-marcados de
   acordo com os dois arquivos .lss. Reset automatico fica desativado.
7. No menu principal do LiveSplit, selecione Compare Against > Game Time.
   O Load Remover Time pausa Game Time durante portas; Real Time nao pausa.
8. Abra o port, inicie o jogo e salve o layout do LiveSplit para manter o caminho.

recvx-ps3recomp.asl e a versao geral para configurar sua propria rota.
Os scripts deste pacote conectam-se somente ao port PC (EU/JP/US), nao aos
emuladores. Os tempos/historico dos splits sao exemplos; use Clear History
no LiveSplit se quiser iniciar seu proprio historico. Confira as categorias
e regras com os moderadores: este pacote nao representa aprovacao do port
para um ranking. A entrada no catalogo automatico do LiveSplit depende da
aprovacao dos mantenedores; a instalacao manual acima funciona sem ela.
'''
en='''CODE VERONICA - LIVESPLIT SPEEDRUN v1.0

Original autosplitter by Kapdap. PC port adaptation by Kastiel.
Original license and credits retained in LICENSE-recvx.txt.
Split lists and icons arranged by Kastiel.

1. Extract this ZIP into a permanent folder. Install/open LiveSplit.
2. Open Splits > From File: choose the Any% or S-Rank .lss file.
3. In Edit Splits, Deactivate the emulator autosplitter if active. If the
   button says Activate, leave it inactive. Use only one autosplitter.
4. Edit Layout > + > Control > Scriptable Auto Splitter.
5. Open that component's settings. Browse to the matching preset:
   Any%: recvx-port-AnyPercent.asl
   S-Rank: recvx-port-SRank.asl
6. Enable Start and Split. Route events are already checked according to
   the two .lss files. Automatic Reset is disabled.
7. In LiveSplit's main menu, choose Compare Against > Game Time.
   Load Remover Time pauses Game Time during doors; Real Time keeps running.
8. Start the PC port and save your LiveSplit layout to retain the script path.

recvx-ps3recomp.asl is the general script for configuring a custom route.
These scripts attach only to the PC port (EU/JP/US), not emulators.
Stored split times/history are examples; use Clear History in LiveSplit
to begin your own history. Check category rules with the moderators; this
package does not establish leaderboard approval. Availability in LiveSplit's
automatic catalog depends on maintainer approval; manual installation above
does not require it.
'''
files['LEIA-ME.txt']=pt.encode('utf-8-sig');files['README.txt']=en.encode('utf-8-sig')
args.output_dir.mkdir(parents=True,exist_ok=True)
output=args.output_dir/'CodeVeronica-LiveSplit-Speedrun-v1.0.zip'
assert not output.exists(),output
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for name,data in files.items():z.writestr('CodeVeronica-LiveSplit/'+name,data)
with zipfile.ZipFile(output) as z:assert z.testzip() is None
digest=hashlib.sha256(output.read_bytes()).hexdigest()
output.with_suffix('.zip.sha256').write_text(digest+'  '+output.name+'\n')
(args.output_dir/'livesplit-verification.json').write_text(json.dumps(reports,indent=2))
print(output)
