"""Optional JP visual appearance hooks; preserve the normal lift generator."""
import importlib.util
import sys
from pathlib import Path

base=Path(__file__).with_name('cv_native_menu_hooks.py')
spec=importlib.util.spec_from_file_location('cv_native_menu_hooks',base)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.main()
args=sys.argv[1:]
if '--output' in args:
    output=Path(args[args.index('--output')+1])
    text=output.read_text(encoding='utf-8',errors='surrogateescape')
    region=args[args.index('--region')+1] if '--region' in args else 'eu'
    hooks={'jp':('0003ECC0','0004A3F8','0050CE68'), 'us':('0003ED18','0004A450','0050C578'), 'eu':('0003F118','0004A850','0050E538')}[region]
    for address in (*hooks[:2],{'jp':'001DBCA0','us':'001DBB20','eu':'001DD940'}[region]):
        old=f'void func_{address}(ppu_context* ctx) {{'
        if old in text:
            if text.count(old)!=1:raise SystemExit(f'Ambiguous appearance hook {address}')
            text=text.replace(old,f'void cv_original_{address}(ppu_context* ctx) {{')
    pace=f'void cv_original_{hooks[2]}(ppu_context* ctx) {{'
    if pace in text:
        text=text.replace(pace,pace+'\n    cv_appearance::frame(ctx);')
        text=text.replace('#include "cv_cheats_lift.h"','#include "cv_cheats_lift.h"\n#include "cv_appearance_runtime.h"')
    if text!=output.read_text(encoding='utf-8',errors='surrogateescape'):
        output.write_text(text,encoding='utf-8',errors='surrogateescape',newline='\n')

