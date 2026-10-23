"""Build only the redistributable port ZIP; never include game data or profiles."""
from pathlib import Path
import argparse,hashlib,json,re,zipfile

FILES=("MyGameRecomp.exe","MyGameRecomp_US.exe","MyGameRecomp_JP.exe",
       "avcodec-61.dll","avutil-59.dll","swresample-5.dll","swscale-8.dll",
       "amd_fidelityfx_framegeneration_dx12.dll","dxcompiler.dll","dxil.dll",
       "AMD-FSR-LICENSE.md","SAVE-LIBRARY-NOTICES.txt")
def main():
    root=Path(__file__).resolve().parents[1]
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bin-dir',type=Path,default=root/'build-release/bin')
    ap.add_argument('--output-dir',type=Path,default=root/'build-release/pacote')
    ap.add_argument('--version',help='Defaults to k_launcher_version in launcher.cpp')
    ap.add_argument('--check-only',action='store_true')
    args=ap.parse_args()
    version=args.version
    if not version:
        m=re.search(r'k_launcher_version\s*=\s*L"([^"]+)"',(root/'launcher.cpp').read_text(encoding='utf-8'))
        if not m:ap.error('k_launcher_version was not found')
        version=m[1]
    if not re.fullmatch(r'v?\d+(?:\.\d+)*',version):ap.error('Use a stable numeric version, such as v1.1.0')
    missing=[n for n in FILES if not (args.bin_dir/n).is_file()]
    if missing:ap.error('Missing build files: '+', '.join(missing))
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
    sources={n:args.bin_dir/n for n in FILES}
    octicons=root/'launcher_art/github-octicons-LICENSE.txt'
    if octicons.is_file():sources['github-octicons-LICENSE.txt']=octicons
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
    print(f'Publish this ZIP as an asset of the stable GitHub Release with tag {version}.')
if __name__=='__main__':main()
