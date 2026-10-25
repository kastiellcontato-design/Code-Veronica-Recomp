"""Run WASAPI initialization regressions in a Windows x64 developer prompt.

The real backend functions are extracted from cellAudio.c. COM devices are
mocked; no game files, host audio output, or device changes are involved.
"""
from pathlib import Path
import argparse,subprocess,os,shutil

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',default=shutil.which('clang-cl') or 'C:/Program Files/LLVM/bin/clang-cl.exe')
    ap.add_argument('--output-dir',type=Path,default=Path('build-wasapi-tests'))
    args=ap.parse_args()
    if os.name!='nt':ap.error('Windows x64 and the Visual Studio developer environment are required')
    here=Path(__file__).resolve().parent;out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    source=(here.parent/'cellAudio.c').read_text(encoding='utf-8')
    begin=source.index('static IAudioClient*');end=source.index('#endif /* AUDIO_BACKEND_WASAPI */',begin)
    (out/'wasapi-under-test.h').write_text(source[begin:end],encoding='utf-8')
    command=[args.compiler,'/nologo','/TC','/std:c11','/O2','/Gy','/Gw','/MT','/UNDEBUG','/I',str(out),str(here/'test_wasapi_backend.c'),'/Fe:'+str(out/'test_wasapi.exe'),'/Fo:'+str(out/'test_wasapi.obj'),'/link','/OPT:REF','ole32.lib','user32.lib','uuid.lib']
    subprocess.run(command,check=True)
    subprocess.run([str(out/'test_wasapi.exe')],check=True)

if __name__=='__main__':main()
