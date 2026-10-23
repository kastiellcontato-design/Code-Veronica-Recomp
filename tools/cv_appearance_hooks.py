"""Appearance hooks layered on the shared native-menu/Battle transformation.
Finalize in memory so unchanged generated units retain their timestamp.
"""
import importlib.util
from pathlib import Path
base=Path(__file__).with_name('cv_native_menu_hooks.py')
spec=importlib.util.spec_from_file_location('cv_native_menu_hooks',base)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
def appearance(text,region):
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
    return text

module.main(appearance)
