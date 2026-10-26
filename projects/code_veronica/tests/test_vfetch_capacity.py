"""Regression harness: compile the actual vf_note allocator at old/new capacities.
Run with Python after activating an x64 MSVC/Windows SDK toolchain.
Outputs remain under the port build-distribution directory.
"""
from pathlib import Path
import re,subprocess
port=Path(__file__).resolve().parents[1];repo=port.parents[1]
out=port/'build-distribution/test-vfetch';out.mkdir(parents=True,exist_ok=True)
backend=(repo/'libs/video/rsx_d3d12_backend.c').read_text(encoding='utf-8')
def function(source):
 start=source.index('static void vf_note(');opening=source.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return start,end
fixture=(port/'tests/test_vfetch_capacity.cpp').read_text(encoding='utf-8')
a,b=function(fixture);c,d=function(backend)
fixture=fixture[:a]+backend[c:d]+fixture[b:]
capacity=re.search(r'^#define VF_DESC_MAX[ \t]+(.+)$',backend,re.M)[1]
fixture=fixture.replace('#define VF_DESC_MAX 49152u','#define VF_DESC_MAX '+capacity)
source=out/'actual-allocator.cpp';source.write_text(fixture,encoding='utf-8')
for version,flags in [('old',['/DVF_DESC_MAX=16384u','/DEXPECT_OLD']),('fixed',[])]:
 exe=out/(version+'.exe')
 subprocess.run(['C:/Program Files/LLVM/bin/clang-cl.exe','/nologo','/O2','/EHsc','/UNDEBUG',*flags,str(source),'/Fe:'+str(exe)],cwd=out,check=True)
 subprocess.run([str(exe)],cwd=out,check=True)
