"""Build only the redistributable port ZIP; never include game data or profiles."""
from pathlib import Path
import argparse,glob,hashlib,json,re,struct,zipfile

FILES=("CodeVeronicaPort.exe","CodeVeronicaPort_US.exe","CodeVeronicaPort_JP.exe",
       "avcodec-61.dll","avutil-59.dll","swresample-5.dll","swscale-8.dll",
       "amd_fidelityfx_framegeneration_dx12.dll","dxcompiler.dll","dxil.dll",
       "AMD-FSR-LICENSE.md","SAVE-LIBRARY-NOTICES.txt")
def find_vc_redist():
    # Newest Visual Studio copy of the redistributable installer (Microsoft lets apps ship it).
    found=glob.glob(r'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\vc_redist.x64.exe')
    return Path(max(found,key=lambda f:Path(f).stat().st_mtime)) if found else None
def main():
    root=Path(__file__).resolve().parents[1]
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bin-dir',type=Path)
    ap.add_argument('--distribution',action='store_true',help='Package the GUI build with developer captures disabled')
    ap.add_argument('--output-dir',type=Path,default=root/'build-release/pacote')
    ap.add_argument('--version',help='Defaults to k_launcher_version in launcher.cpp')
    ap.add_argument('--localization-dir',type=Path,default=root/'localization')
    ap.add_argument('--check-only',action='store_true')
    ap.add_argument('--vc-redist',action='store_true',help='Add requisitos/vc_redist.x64.exe for packages sent by hand')
    args=ap.parse_args()
    if args.bin_dir is None:args.bin_dir=root/('build-distribution/bin' if args.distribution else 'build-release/bin')
    version=args.version
    if not version:
        m=re.search(r'k_launcher_version\s*=\s*L"([^"]+)"',(root/'launcher.cpp').read_text(encoding='utf-8'))
        if not m:ap.error('k_launcher_version was not found')
        version=m[1]
    if not re.fullmatch(r'v?\d+(?:\.\d+)*',version):ap.error('Use a stable numeric version, such as v1.1.0')
    missing=[n for n in FILES if not (args.bin_dir/n).is_file()]
    if missing:ap.error('Missing build files: '+', '.join(missing))
    if args.distribution:
        result_path=args.bin_dir.parent/'build-result.json'
        if not result_path.is_file():ap.error('Missing distribution build-result.json; run distribution/build.py first')
        result=json.loads(result_path.read_text(encoding='utf-8'))
        if result.get('distribution') is not True:ap.error('Not a distribution build')
        for name in (*FILES[:3], 'Code Veronica.exe'):
            with (args.bin_dir.parent/name if name=='Code Veronica.exe' else args.bin_dir/name).open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
            if result.get('executables',{}).get(name)!=digest:ap.error('Distribution executable changed since build: '+name)
        for name in (*FILES[:3], 'Code Veronica.exe'):
            with (args.bin_dir.parent/name if name=='Code Veronica.exe' else args.bin_dir/name).open('rb') as f:
                f.seek(0x3c); pe=struct.unpack('<I',f.read(4))[0]
                f.seek(pe); signature=f.read(4)
                f.seek(pe+24+68); subsystem=struct.unpack('<H',f.read(2))[0]
            if signature!=b'PE\0\0' or subsystem!=2:ap.error('Distribution requires Windows GUI executables: '+name)
        for name in ('LEIA-ME.txt','README.txt'):
            if not (root/name).is_file():ap.error('Missing player guide: '+name)
    # Reject older executables: changing the source version alone is not a build.
    source=root/'launcher.cpp'
    if source.is_file() and not args.version:
        stale=[n for n in FILES[:3] if (args.bin_dir/n).stat().st_mtime<source.stat().st_mtime]
        if stale:ap.error('Rebuild the regional executables first: '+', '.join(stale))
    print(f'{version}: {len(FILES)} redistributable files; no game data, saves or settings.')
    if args.check_only:return
    args.output_dir.mkdir(parents=True,exist_ok=True)
    archive=args.output_dir/f'CodeVeronica-PC-{version}.zip'
    if archive.exists():ap.error(f'Refusing to overwrite an existing release package: {archive}')
    temporary=archive.with_suffix('.zip.part')
    manifest={'version':version,'files':{}}
    sources={(('bin/'+n) if args.distribution else n):args.bin_dir/n for n in FILES}
    if args.distribution:sources['Code Veronica.exe']=args.bin_dir.parent/'Code Veronica.exe'
    octicons=root/'launcher_art/github-octicons-LICENSE.txt'
    if octicons.is_file():sources['github-octicons-LICENSE.txt']=octicons
    if (root/'LEIA-ME.txt').is_file():sources['LEIA-ME.txt']=root/'LEIA-ME.txt'
    if (root/'README.txt').is_file():sources['README.txt']=root/'README.txt'
    # The port's own license and the third-party license texts the binaries need.
    repo=root.parents[1]
    notices={'LICENSE.txt':repo/'LICENSE','THIRD-PARTY-NOTICES.md':repo/'THIRD-PARTY-NOTICES.md',
             'licenses/FFmpeg-COPYING.LGPLv2.1.txt':repo/'third_party/ffmpeg/COPYING.LGPLv2.1',
             'licenses/FFmpeg-LICENSE.md':repo/'third_party/ffmpeg/LICENSE.md',
             'licenses/at3_standalone-COPYING.LGPLv2.1.txt':repo/'third_party/at3_standalone/COPYING.LGPLv2.1',
             'licenses/SMAA-LICENSE.txt':repo/'third_party/smaa/LICENSE.txt'}
    for n,path in notices.items():
        if path.is_file():sources[n]=path
    for license_file in sorted((root/'distribution/licenses').glob('*')):
        if license_file.is_file():sources['licenses/'+license_file.name]=license_file
    if args.vc_redist:
        redist=find_vc_redist()
        if not redist:ap.error('vc_redist.x64.exe was not found in the Visual Studio installation')
        sources['requisitos/vc_redist.x64.exe']=redist
    if args.localization_dir and args.localization_dir.is_dir():
        for p in sorted(args.localization_dir.rglob('*')):
            if p.is_file():sources['assets/'+p.relative_to(args.localization_dir).as_posix()]=p
    for n in sources:
        with sources[n].open('rb') as f:manifest['files'][n]=hashlib.file_digest(f,'sha256').hexdigest()
    try:
        with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for n in sources:z.write(sources[n],n)
            z.writestr('release-manifest.json',json.dumps(manifest,indent=2)+'\n')
        temporary.replace(archive)
    finally:
        if temporary.exists():temporary.unlink()
    with archive.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n',encoding='utf-8')
    print(archive)
    if not args.vc_redist:print(f'Publish this ZIP as an asset of the stable GitHub Release with tag {version}.')
if __name__=='__main__':main()
