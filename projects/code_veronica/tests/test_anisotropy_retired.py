"""Run from a Windows compiler environment: --output <build/test-directory>."""
from pathlib import Path
import argparse,subprocess

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--output',type=Path,required=True)
 ap.add_argument('--compiler',default='clang-cl')
 args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 repo=Path(__file__).resolve().parents[3]
 source=(repo/'libs/video/rsx_d3d12_backend.c').read_text(encoding='utf8')
 block=source[source.index('static int s_forced_aniso_disabled;'):source.index('/* RES_SCALE=<f>')]
 test=args.output/'test-policy.cpp'
 test.write_text('#include <cstdlib>\n#include <cstdio>\n#include <cassert>\ntypedef unsigned UINT;\n'+block+ '\nint main(int argc,char**argv){assert(argc==2);_putenv_s("RSX_ANISO",argv[1]);assert(tex_aniso()==unsigned(atoi(argv[1])));rsx_d3d12_disable_forced_anisotropy();assert(tex_aniso()==0);_putenv_s("RSX_ANISO","16");assert(tex_aniso()==0);return 0;}\n',encoding='utf8')
 exe=args.output/'test-policy.exe'
 subprocess.run([args.compiler,'/nologo','/O2','/UNDEBUG','/MT',str(test),'/Fo'+str(args.output/'test-policy.obj'),'/Fe'+str(exe)],check=True)
 for factor in ('2','4','8','16'):subprocess.run([str(exe),factor],check=True)
 print('PASS forced anisotropy retirement: all legacy factors and later environment changes stay disabled')
if __name__=='__main__':main()
