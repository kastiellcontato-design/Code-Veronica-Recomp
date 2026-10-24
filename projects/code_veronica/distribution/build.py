"""Build GUI distribution separately; preserve normal local executables."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
PORT=Path(__file__).resolve().parents[1]
ROOT=PORT.parents[1]
OUT=PORT/'build-distribution'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def run(command,log):
    print(' '.join(map(str,command)),flush=True)
    with log.open('w',encoding='utf-8') as f:
        subprocess.run(list(map(str,command)),stdout=f,stderr=subprocess.STDOUT,check=True)
def main():
    OUT.mkdir(exist_ok=True)
    names=['CodeVeronicaPort.exe','CodeVeronicaPort_JP.exe','CodeVeronicaPort_US.exe','cv_launcher.ini']
    kept={n:sha(PORT/'build-release/bin'/n) for n in names}
    results={}
    for region,suffix,cache in [('JP','_jp','build-release-jp'),('EU','','build-release'),('US','_us','build-release-us')]:
        build=OUT/region.lower();build.mkdir(exist_ok=True)
        old=PORT/cache
        objects=[];provenance=[]
        # Rebuild host/runtime/UI, but reuse only source-verified lifted units.
        for source in sorted((ROOT/('src/recomp'+suffix)).glob('ppu_recomp_*.cpp')):
            fresh=build/'verified-lift'/source.name;fresh.parent.mkdir(exist_ok=True)
            run([sys.executable,ROOT/'tools/cv_appearance_hooks.py','--input',source,'--output',fresh,'--region',region.lower()],build/f'verify-{source.stem}.log')
            saved=old/'native_menu_lift'/source.name
            obj=old/'CMakeFiles/MyGameRecomp.dir/native_menu_lift'/(source.name+'.obj')
            if not saved.is_file() or sha(saved)!=sha(fresh):raise RuntimeError(f'Cached lifted source mismatch: {source}')
            if not obj.is_file() or obj.stat().st_mtime<saved.stat().st_mtime:raise RuntimeError(f'Stale lifted object: {obj}')
            objects.append(obj);provenance.append(dict(source=str(source),generated_sha256=sha(fresh),object=str(obj),object_sha256=sha(obj)))
        for source in sorted((PORT/'spu').glob('*/spu_recomp.c')):
            obj=old/'CMakeFiles/MyGameRecomp.dir/spu'/source.parent.name/'spu_recomp.c.obj'
            if not obj.is_file() or obj.stat().st_mtime<source.stat().st_mtime:raise RuntimeError(f'Stale SPU object: {obj}')
            objects.append(obj);provenance.append(dict(source=str(source),source_sha256=sha(source),object=str(obj),object_sha256=sha(obj)))
        copied=[]
        for index,obj in enumerate(objects):
            dst=build/'lifted-objects'/f'{index:03}.obj';dst.parent.mkdir(exist_ok=True)
            shutil.copy2(obj,dst);copied.append(dst.as_posix())
        (build/'reuse.cmake').write_text('set(CV_LIFTED_OBJECTS\n'+''.join(f' "{p}"\n' for p in copied)+')\n',encoding='utf-8')
        (build/'lift-provenance.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
        run(['cmake','-S',PORT/'distribution','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe','-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe',
             '-DCMAKE_LINKER=C:/Program Files/LLVM/bin/lld-link.exe','-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2',
             '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2',f'-DCV_REGION={region}',f'-DRECOMP_DIR={(ROOT/("src/recomp"+suffix)).as_posix()}',
             f'-DSPU_DIR={(PORT/"spu").as_posix()}'],build/'configure.log')
        run(['cmake','--build',build,'--target','MyGameRecomp','CodeVeronicaLauncher','-j','2'],build/'build.log')
        exe=OUT/'bin'/('CodeVeronicaPort'+('' if region=='EU' else '_'+region)+'.exe')
        results[exe.name]=sha(exe)
        print(f'DONE {region}: {exe}',flush=True)
    results['Code Veronica.exe']=sha(OUT/'bin/Code Veronica.exe')
    if kept!={n:sha(PORT/'build-release/bin'/n) for n in names}:raise RuntimeError('Local normal files changed')
    (OUT/'build-result.json').write_text(json.dumps(dict(distribution=True,version='v1.0',executables=results,normal_preserved=kept),indent=2),encoding='utf-8')
    print('PASS: all three GUI distribution builds completed; normal local files unchanged',flush=True)
if __name__=='__main__':main()
