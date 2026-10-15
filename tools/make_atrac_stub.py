#!/usr/bin/env python3
"""Write the flashATRAC.pic stand-in served when no PS3 firmware is present.

The bytes are embedded in runtime/syscalls/sys_fs.c (k_flash_atrac_stub);
rerun this and paste the output file as a C array if the stub changes.
Usage: python tools/make_atrac_stub.py OUT.pic
"""
# Builds a minimal SPU ELF that stands in for flashATRAC.pic (no Sony code).
import struct, sys
def ila(rt, imm): return (0x21 << 25) | ((imm & 0x3FFFF) << 7) | rt
def brsl(rt, off): return (0x66 << 23) | (((off >> 2) & 0xFFFF) << 7) | rt
def rr(op, rt, ra, rb): return (op << 21) | (rb << 14) | (ra << 7) | rt
def sf(rt, ra, rb): return rr(0x040, rt, ra, rb)      # rt = rb - ra
def a(rt, ra, rb): return rr(0x0C0, rt, ra, rb)
def lqd(rt, ra): return (0x34 << 24) | (ra << 7) | rt
def stqd(rt, ra): return (0x24 << 24) | (ra << 7) | rt
def cwd(rt, ra): return (0x1F6 << 21) | (ra << 7) | rt
def shufb(rt, ra, rb, rc): return (0xB << 28) | (rt << 21) | (rb << 14) | (ra << 7) | rc
def il(rt, imm): return (0x81 << 23) | ((imm & 0xFFFF) << 7) | rt
BI_R0 = 0x35000000
TEXT = 0x90
code = []
pc = lambda: TEXT + 4 * len(code)
# setup(r3 = event callback, r4 -> init slot, r5 -> decode slot)
L1 = TEXT + 8
code += [ila(2, L1), brsl(79, 4)]
assert pc() == L1
INIT = TEXT + 4 * 16; DEC = INIT + 8
code += [sf(79, 2, 79),
         ila(6, INIT), a(6, 6, 79), lqd(7, 4), cwd(8, 4), shufb(7, 6, 7, 8), stqd(7, 4),
         ila(6, DEC),  a(6, 6, 79), lqd(7, 5), cwd(8, 5), shufb(7, 6, 7, 8), stqd(7, 5),
         BI_R0]
while pc() < INIT: code.append(0x40200000)  # nop
code += [il(3, 0), BI_R0]                       # init: return 0
code += [ila(2, 0x3A7C0), BI_R0]                # decode: marker (the runtime decodes natively), return
text = b''.join(struct.pack('>I', w) for w in code)
guid = b'PS3RECOMP-ATRAC\0'
body_off = 0x80
body = guid + text
shstr = b'\0.SpuGUID\0.text\0.shstrtab\0'
sh_off = (body_off + len(body) + len(shstr) + 15) & ~15
ehdr = b'\x7fELF' + bytes([1, 2, 1]) + bytes(9)
ehdr += struct.pack('>HHIIIIIHHHHHH', 2, 0x17, 1, TEXT, 0x34, sh_off, 0, 0x34, 0x20, 1, 0x28, 4, 3)
phdr = struct.pack('>IIIIIIII', 1, body_off, 0x80, 0x80, len(body), len(body), 5, 0x80)
f = bytearray(ehdr + phdr); f += bytes(body_off - len(f)); f += body
str_off = len(f); f += shstr; f += bytes(sh_off - len(f))
def sh(n, t, fl, ad, of, sz, al): return struct.pack('>IIIIIIIIII', n, t, fl, ad, of, sz, 0, 0, al, 0)
f += sh(0, 0, 0, 0, 0, 0, 0)
f += sh(1, 1, 2, 0x80, body_off, 16, 16)
f += sh(10, 1, 6, TEXT, body_off + 16, len(text), 8)
f += sh(16, 3, 0, 0, str_off, len(shstr), 1)
open(sys.argv[1], 'wb').write(f)
print(len(f), 'bytes;', ' '.join('%08X' % w for w in code))
