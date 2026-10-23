"""Create a build-local lift with named wrappers (native menu, boot skip); leave generated sources intact."""
import argparse
import re
from pathlib import Path

# EU: 0002A468, 0002F3D0, 001BD060: the OPTION menu (native_pc_menu.cpp);
# 0020140C: the opening logos' end check, 0002B35C: the title sequence that
# plays the intro movie (boot_skip.cpp). JP: the same functions in the
# Japanese EBOOT, in the same order (projects/code_veronica/cv_version.h).
# The last two: the guest libc memset and memmove (cv_native_libc.cpp).
HOOKS_BY_REGION = {
    "eu": ("0002A468", "0002F3D0", "001BD060", "0020140C", "0002B35C", "000333A0", "001D9370", "001D8090", "0050E538", "001D952C", "00033FD0", "007B6C38", "007AE7B0"),
    "us": ("0002A038", "0002EF60", "001BB5E0", "001FF46C", "0002AEEC", "00032FA0", "001D7550", "001D6270", "0050C578", "001D770C", "00033BD0", "007B4C78", "007AC7F0"),
    "jp": ("0002A038", "0002EF38", "001BB688", "001FF7C0", "0002AEC4", "00032F80", "001D76D0", "001D63F0", "0050CE68", "001D788C", "00033B78", "007B5568", "007AD0E0"),
}
HOOKS = HOOKS_BY_REGION["eu"]


def battle_reads(source,region):
    title,script={"eu":(0x2D3A4,0x12A900),"jp":(0x2CF14,0x12A498),"us":(0x2CF34,0x12A500)}[region]
    functions=list(re.finditer(r"^void (?:func_|cv_original_)([0-9A-F]+)\(ppu_context\* ctx\) \{",source,re.M))
    changed=0
    for i,m in reversed(list(enumerate(functions))):
        address=int(m[1],16)
        if not (title<=address<title+20 or address==script):continue
        end=functions[i+1].start() if i+1<len(functions) else len(source);body=source[m.start():end]
        if title<=address<title+20:
            old="ctx->gpr[5] = vm_read32(ctx->gpr[5] + 0x4);"
            if body.count(old)!=2:raise SystemExit(f"Battle title read sites changed in {region}: {address:08X}")
            body=body.replace(old,"ctx->gpr[5] = cv_battle_read32(ctx->gpr[5] + 0x4,0x1u);");changed+=2
        elif address==script:
            old="ctx->gpr[3] = vm_read32((ctx->gpr[3] + ctx->gpr[6]));"
            if body.count(old)!=1:raise SystemExit(f"Battle character read site changed in {region}")
            body=body.replace(old,"ctx->gpr[3] = cv_battle_read32((ctx->gpr[3] + ctx->gpr[6]),0x7Cu);");changed+=1
        source=source[:m.start()]+body+source[end:]
    return source,changed

def main(finalize=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--discover", type=Path)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--region", choices=sorted(HOOKS_BY_REGION), default="eu")
    args = parser.parse_args()
    global HOOKS
    HOOKS = HOOKS_BY_REGION[args.region]
    if args.discover:
        owners = {}
        battle_count=0
        for path in sorted(args.discover.glob("ppu_recomp_*.cpp")):
            text = path.read_text(encoding="utf-8", errors="surrogateescape")
            _,verified=battle_reads(text,args.region)
            battle_count+=verified
            for address in HOOKS:
                definition = f"void func_{address}(ppu_context* ctx) {{"
                count = text.count(definition)
                if count:
                    if count != 1 or address in owners:
                        raise SystemExit(f"Ambiguous native-menu hook {address}")
                    owners[address] = path.resolve().as_posix()
        if len(owners) != len(HOOKS):
            raise SystemExit(f"Native-menu hooks missing from lift: {set(HOOKS)-owners.keys()}")
        if battle_count!=5:raise SystemExit(f"Expected five verified Battle read sites in {args.region}; found {battle_count}")
        print(";".join(path.resolve().as_posix() for path in sorted(args.discover.glob("ppu_recomp_*.cpp"))))
        return
    if not args.input or not args.output:
        parser.error("Pass --discover or both --input and --output")
    source = args.input.read_text(encoding="utf-8", errors="surrogateescape")
    for address in HOOKS:
        original = f"void func_{address}(ppu_context* ctx) {{"
        replacement = f"void cv_original_{address}(ppu_context* ctx) {{"
        if original not in source:
            continue
        if source.count(original) != 1:
            raise SystemExit(f"Expected exactly one definition for {address}; regenerate/check the lift")
        if address == HOOKS[8]:  # frame pacing, on the PPU thread in every FPS mode
            replacement += "\n    cv_cheats_frame();"
        source = source.replace(original, replacement)
    source,battle_count=battle_reads(source,args.region)
    first = re.search(r"^void (?:func_|cv_original_)[0-9A-F]+\(ppu_context\* ctx\) \{", source, re.M)
    if not first:
        raise SystemExit("No guest function found for cheat store wrappers")
    source = source[:first.start()] + '#include "cv_cheats_lift.h"\n' + source[first.start():]
    if battle_count:source=source.replace('#include "cv_cheats_lift.h"','#include "cv_cheats_lift.h"\n#include "cv_unlock_lift.h"')
    if finalize:source=finalize(source,args.region)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8", errors="surrogateescape") != source:
        args.output.write_text(source, encoding="utf-8", errors="surrogateescape", newline="\n")

if __name__ == "__main__":
    main()
