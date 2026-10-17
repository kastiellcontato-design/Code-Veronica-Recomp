"""Create a build-local lift with named wrappers (native menu, boot skip); leave generated sources intact."""
import argparse
from pathlib import Path

# EU: 0002A468, 0002F3D0, 001BD060: the OPTION menu (native_pc_menu.cpp);
# 0020140C: the opening logos' end check, 0002B35C: the title sequence that
# plays the intro movie (boot_skip.cpp). JP: the same functions in the
# Japanese EBOOT, in the same order (projects/code_veronica/cv_version.h).
HOOKS_BY_REGION = {
    "eu": ("0002A468", "0002F3D0", "001BD060", "0020140C", "0002B35C", "000333A0", "001D9370", "001D8090", "001D952C", "00033FD0"),
    "us": ("0002A038", "0002EF60", "001BB5E0", "001FF46C", "0002AEEC", "00032FA0", "001D7550", "001D6270", "001D770C", "00033BD0"),
    "jp": ("0002A038", "0002EF38", "001BB688", "001FF7C0", "0002AEC4", "00032F80", "001D76D0", "001D63F0", "001D788C", "00033B78"),
}
HOOKS = HOOKS_BY_REGION["eu"]

def main():
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
        for path in sorted(args.discover.glob("ppu_recomp_*.cpp")):
            text = path.read_text(encoding="utf-8", errors="surrogateescape")
            for address in HOOKS:
                definition = f"void func_{address}(ppu_context* ctx) {{"
                count = text.count(definition)
                if count:
                    if count != 1 or address in owners:
                        raise SystemExit(f"Ambiguous native-menu hook {address}")
                    owners[address] = path.resolve().as_posix()
        if len(owners) != len(HOOKS):
            raise SystemExit(f"Native-menu hooks missing from lift: {set(HOOKS)-owners.keys()}")
        print(";".join(sorted(set(owners.values()))))
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
        source = source.replace(original, replacement)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8", errors="surrogateescape") != source:
        args.output.write_text(source, encoding="utf-8", errors="surrogateescape", newline="\n")

if __name__ == "__main__":
    main()