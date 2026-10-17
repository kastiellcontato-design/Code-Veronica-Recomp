// Native EU func_00033FD0 from the supplied lift. JP/US normalized bodies match.
// Only its name and external service calls are replaced for isolated execution.
/* Rotate helpers */
static inline uint32_t ppc_rlwinm(uint32_t rs, int sh, int mb, int me) {
    sh &= 31;
    uint32_t rotated = sh ? ((rs << sh) | (rs >> (32 - sh))) : rs;
    uint32_t mask;
    if (mb <= me) {
        mask = ((uint32_t)-1 >> mb) & ((uint32_t)-1 << (31 - me));
    } else {
        mask = ((uint32_t)-1 >> mb) | ((uint32_t)-1 << (31 - me));
    }
    return rotated & mask;
}

static inline uint32_t ppc_rlwimi(uint32_t ra, uint32_t rs, int sh, int mb, int me) {
    sh &= 31;
    uint32_t rotated = sh ? ((rs << sh) | (rs >> (32 - sh))) : rs;
    uint32_t mask;
    if (mb <= me) {
        mask = ((uint32_t)-1 >> mb) & ((uint32_t)-1 << (31 - me));
    } else {
        mask = ((uint32_t)-1 >> mb) | ((uint32_t)-1 << (31 - me));
    }
    return (rotated & mask) | (ra & ~mask);
}

/* 64-bit rotate helpers */
static inline uint64_t ppc_rotl64(uint64_t v, int n) {
    n &= 63;
    return n ? ((v << n) | (v >> (64 - n))) : v;
}
static inline uint64_t ppc_mask64(int mb, int me) {
    uint64_t mask;
    if (mb <= me) {
        mask = (mb == 0) ? ~0ULL : (~0ULL >> mb);
        mask &= (me == 63) ? ~0ULL : (~0ULL << (63 - me));
    } else {
        mask = (~0ULL >> mb) | (~0ULL << (63 - me));
    }
    return mask;
}
static inline uint64_t ppc_rldicl(uint64_t rs, int sh, int mb) {
    return ppc_rotl64(rs, sh) & ppc_mask64(mb, 63);
}

static void native_finish_fixture(ppu_context* ctx) {
        uint64_t _cs_29 = ctx->gpr[29];
        uint64_t _cs_30 = ctx->gpr[30];
        uint64_t _cs_31 = ctx->gpr[31];
        vm_write64(ctx->gpr[1] + -0x90, ctx->gpr[1]); ctx->gpr[1] += -0x90;
        ctx->gpr[0] = ctx->lr;
        vm_write64(ctx->gpr[1] + 0xA0, ctx->gpr[0]);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x6430);
        vm_write64(ctx->gpr[1] + 0x88, ctx->gpr[31]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x50);
        ctx->gpr[31] = vm_read32(ctx->gpr[3] + 0x60);
        ctx->gpr[4] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[4], 0, 22, 22);
        vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[30]);
        vm_write64(ctx->gpr[1] + 0x78, ctx->gpr[29]);
        { int64_t a = (int32_t)ctx->gpr[4]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 2)) goto loc_000340DC;
        ctx->lr = 0x00034008; native_finish_service(ctx,0x001D0680u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = ctx->gpr[31] | 0x0;
        ctx->lr = 0x00034010; native_finish_service(ctx,0x001D9080u); DRAIN_TRAMPOLINE(ctx);
        ctx->lr = 0x00034014; native_finish_service(ctx,0x001D9060u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[4] = (int64_t)(int32_t)(0);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[6] = vm_read32(ctx->gpr[5] + 0x50);
        ctx->gpr[6] = (ctx->gpr[6] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[4], 0, 25, 25);
        vm_write32(ctx->gpr[5] + 0x50, ctx->gpr[6]);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x58);
        ctx->gpr[4] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[4], 0, 27, 27);
        { int64_t a = (int32_t)ctx->gpr[4]; int64_t b = (int64_t)0x10; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if ((!((ctx->cr >> 28) & 2))) goto loc_00034050;
        ctx->gpr[4] = (int64_t)(int32_t)((uint32_t)0x30 << 16);
        ctx->gpr[4] = ctx->gpr[4] | 0x50;
        vm_write32(ctx->gpr[3] + 0x64, ctx->gpr[4]);
        goto loc_00034078;
loc_00034050:
        ctx->gpr[4] = (int64_t)(int32_t)((uint32_t)0x30 << 16);
        ctx->gpr[4] = ctx->gpr[4] | 0x48;
        vm_write32(ctx->gpr[3] + 0x64, ctx->gpr[4]);
        ctx->lr = 0x00034060; native_finish_service(ctx,0x001D7E14u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[4] = (int64_t)(int32_t)(0);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + 0x50);
        ctx->gpr[5] = (ctx->gpr[5] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[4], 0, 22, 22);
        vm_write32(ctx->gpr[3] + 0x50, ctx->gpr[5]);
loc_00034078:
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[4] = (int64_t)(int32_t)(0);
        ctx->gpr[6] = (int64_t)(int32_t)((uint32_t)0x1 << 16);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[6] = ctx->gpr[6] | 0xB1D4;
        vm_write32(ctx->gpr[5] + 0x58, ctx->gpr[4]);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + -0x6430);
        vm_write16(ctx->gpr[5] + 0x5C, ctx->gpr[4]);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + -0x6430);
        vm_write16(ctx->gpr[5] + 0x5E, ctx->gpr[4]);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[7] = vm_read32(ctx->gpr[5] + 0x50);
        ctx->gpr[7] = (ctx->gpr[7] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[7], (uint32_t)ctx->gpr[4], 0, 14, 14);
        vm_write32(ctx->gpr[5] + 0x50, ctx->gpr[7]);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[7] = (int64_t)(int32_t)((uint32_t)0x1 << 16);
        ctx->gpr[7] = ctx->gpr[7] | 0xB18C;
        ctx->gpr[8] = vm_read32(ctx->gpr[5] + 0x50);
        ctx->gpr[8] = (ctx->gpr[8] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[8], (uint32_t)ctx->gpr[4], 0, 15, 15);
        vm_write32(ctx->gpr[5] + 0x50, ctx->gpr[8]);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[5] = vm_read32((ctx->gpr[3] + ctx->gpr[6]));
        vm_write32((ctx->gpr[3] + ctx->gpr[7]), ctx->gpr[5]);
        vm_write32(ctx->gpr[31] + 0x2C, ctx->gpr[4]);
        goto loc_000343A0;
loc_000340DC:
        ctx->gpr[3] = vm_read16(ctx->gpr[3] + 0x5E);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)-1; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 24)) | (cr_val << 24); }
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)1; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 4)) | (cr_val << 4); }
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)2; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        if (((ctx->cr >> 28) & 4)) goto loc_000340FC;
        if ((!((ctx->cr >> 24) & 4))) goto loc_000343A0;
        goto loc_00034108;
loc_000340FC:
        if ((!((ctx->cr >> 4) & 4))) goto loc_000341C4;
        if ((!((ctx->cr >> 0) & 4))) goto loc_000342F4;
        goto loc_000343A0;
loc_00034108:
        ctx->gpr[4] = (int64_t)(int32_t)(0);
        ctx->gpr[30] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        { uint64_t _a = ctx->gpr[31]; uint64_t _r = _a + (uint64_t)(int64_t)(0x24); ctx->gpr[3] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        vm_write32(ctx->gpr[31] + 0x2C, ctx->gpr[4]);
        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);
        ctx->gpr[5] = vm_read32(ctx->gpr[30] + -0x6430);
        ctx->gpr[6] = vm_read32(ctx->gpr[5] + 0x50);
        ctx->gpr[6] = (ctx->gpr[6] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[4], 0, 14, 14);
        vm_write32(ctx->gpr[5] + 0x50, ctx->gpr[6]);
        ctx->gpr[5] = vm_read32(ctx->gpr[30] + -0x6430);
        ctx->gpr[6] = vm_read32(ctx->gpr[5] + 0x50);
        ctx->gpr[6] = (ctx->gpr[6] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[4], 0, 15, 15);
        vm_write32(ctx->gpr[5] + 0x50, ctx->gpr[6]);
        ctx->lr = 0x00034140; native_finish_service(ctx,0x001D06B0u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = ctx->gpr[31] | 0x0;
        ctx->lr = 0x00034148; native_finish_service(ctx,0x001D9080u); DRAIN_TRAMPOLINE(ctx);
        ctx->lr = 0x0003414C; native_finish_service(ctx,0x001D9060u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = (int64_t)(int32_t)(0);
        ctx->gpr[4] = (int64_t)(int32_t)(0);
        ctx->lr = 0x00034158; native_finish_service(ctx,0x001EA6B0u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0x2 << 16);
        ctx->gpr[5] = (int64_t)(int32_t)((uint32_t)0x3 << 16);
        ctx->gpr[4] = vm_read32(ctx->gpr[30] + -0x6430);
        ctx->gpr[3] = ctx->gpr[3] | 0xA604;
        ctx->gpr[5] = ctx->gpr[5] + (int64_t)(0x3FC8);
        ctx->gpr[3] = vm_read32((ctx->gpr[4] + ctx->gpr[3]));
        { uint32_t tmp = vm_read32(ctx->gpr[5] + 0x0); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[1] = ftmp; }
        ctx->lr = 0x00034178; native_finish_service(ctx,0x001910F0u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[31] = vm_read32(ctx->gpr[30] + -0x6430);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + 0x58);
        ctx->gpr[3] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[3], 0, 27, 28);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)8; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 4)) goto loc_00034198;
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)7; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if ((!((ctx->cr >> 28) & 4))) goto loc_000341B8;
        goto loc_000341AC;
loc_00034198:
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0x10; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if ((!((ctx->cr >> 28) & 2))) goto loc_000341B8;
        ctx->gpr[3] = (int64_t)(int32_t)(2);
        vm_write16(ctx->gpr[31] + 0x5E, ctx->gpr[3]);
        goto loc_000343A0;
loc_000341AC:
        ctx->gpr[3] = (int64_t)(int32_t)(1);
        vm_write16(ctx->gpr[31] + 0x5E, ctx->gpr[3]);
        goto loc_000343A0;
loc_000341B8:
        ctx->gpr[3] = (int64_t)(int32_t)(1);
        vm_write16(ctx->gpr[31] + 0x5E, ctx->gpr[3]);
        goto loc_000343A0;
loc_000341C4:
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x642C);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x1B0);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 2)) goto loc_000341DC;
        ctx->lr = 0x000341DC; native_finish_service(ctx,0x001EAC40u); DRAIN_TRAMPOLINE(ctx);
loc_000341DC:
        ctx->gpr[3] = (int64_t)(int32_t)(2);
        ctx->lr = 0x000341E4; native_finish_service(ctx,0x001D0F88u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xAD << 16);
        ctx->gpr[5] = (int64_t)(int32_t)(0x400);
        { uint64_t _a = ctx->gpr[3]; uint64_t _r = _a + (uint64_t)(int64_t)(0x6300); ctx->gpr[3] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        { uint64_t _a = ctx->gpr[3]; uint64_t _r = _a + (uint64_t)(int64_t)(0x3000); ctx->gpr[4] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->lr = 0x000341F8; native_finish_service(ctx,0x001CFD34u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[31] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0x1 << 16);
        ctx->gpr[4] = (int64_t)(int32_t)((uint32_t)0x1 << 16);
        ctx->gpr[3] = ctx->gpr[3] | 0xB1D4;
        ctx->gpr[4] = ctx->gpr[4] | 0xB18C;
        ctx->gpr[5] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[8] = (int64_t)(int32_t)(-1);
        ctx->gpr[6] = (int64_t)(int32_t)((uint32_t)0x1 << 16);
        ctx->gpr[30] = ctx->gpr[6] | 0xB15C;
        ctx->gpr[7] = vm_read32(ctx->gpr[5] + 0x6C);
        ctx->gpr[6] = (int64_t)(int32_t)((uint32_t)0x0 << 16);
        ctx->gpr[7] = ctx->gpr[7] | 0x4;
        ctx->gpr[29] = ctx->gpr[6] | 0x8000;
        vm_write32(ctx->gpr[5] + 0x6C, ctx->gpr[7]);
        ctx->gpr[5] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[7] = (int64_t)(int32_t)(0);
        ctx->gpr[3] = vm_read32((ctx->gpr[5] + ctx->gpr[3]));
        vm_write32((ctx->gpr[5] + ctx->gpr[4]), ctx->gpr[3]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        vm_write32(ctx->gpr[3] + 0x80, ctx->gpr[8]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        vm_write32((ctx->gpr[3] + ctx->gpr[30]), ctx->gpr[7]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x6C);
        ctx->gpr[4] = ctx->gpr[4] | ctx->gpr[29];
        vm_write32(ctx->gpr[3] + 0x6C, ctx->gpr[4]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x70);
        ctx->gpr[4] = (ctx->gpr[4] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[7], 0, 4, 4);
        vm_write32(ctx->gpr[3] + 0x70, ctx->gpr[4]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x74);
        ctx->gpr[4] = (ctx->gpr[4] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[7], 0, 10, 10);
        vm_write32(ctx->gpr[3] + 0x74, ctx->gpr[4]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x68);
        ctx->gpr[4] = ctx->gpr[4] | ctx->gpr[29];
        vm_write32(ctx->gpr[3] + 0x68, ctx->gpr[4]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x68);
        ctx->gpr[4] = (ctx->gpr[4] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[7], 0, 24, 24);
        vm_write32(ctx->gpr[3] + 0x68, ctx->gpr[4]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        vm_write32(ctx->gpr[3] + 0x58, ctx->gpr[7]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        vm_write16(ctx->gpr[3] + 0x5C, ctx->gpr[7]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        vm_write16(ctx->gpr[3] + 0x5E, ctx->gpr[7]);
        ctx->lr = 0x000342BC; native_finish_service(ctx,0x001915A0u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[4] = (int64_t)(int32_t)(1);
        vm_write32((ctx->gpr[3] + ctx->gpr[30]), ctx->gpr[4]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x6C);
        ctx->gpr[4] = ctx->gpr[4] | ctx->gpr[29];
        vm_write32(ctx->gpr[3] + 0x6C, ctx->gpr[4]);
        ctx->gpr[3] = vm_read32(ctx->gpr[31] + -0x6430);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x70);
        ctx->gpr[3] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[3], 0, 30, 30);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 2)) goto loc_000343A0;
        ctx->lr = 0x000342F0; native_finish_service(ctx,0x001D0F9Cu); DRAIN_TRAMPOLINE(ctx);
        goto loc_000343A0;
loc_000342F4:
        ctx->gpr[3] = (int64_t)(int32_t)(2);
        ctx->lr = 0x000342FC; native_finish_service(ctx,0x001D0F88u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[4] = (int64_t)(int32_t)((uint32_t)0xAD << 16);
        ctx->gpr[3] = (int64_t)(int32_t)(0);
        { uint64_t _a = ctx->gpr[4]; uint64_t _r = _a + (uint64_t)(int64_t)(0x6300); ctx->gpr[5] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->gpr[4] = (int64_t)(int32_t)(0x400);
        { uint64_t _a = ctx->gpr[5]; uint64_t _r = _a + (uint64_t)(int64_t)(0x3000); ctx->gpr[5] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->lr = 0x00034314; native_finish_service(ctx,0x001D0F94u); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[4] = (int64_t)(int32_t)(-1);
        ctx->gpr[5] = (int64_t)(int32_t)((uint32_t)0x1 << 16);
        ctx->gpr[6] = (int64_t)(int32_t)(0);
        ctx->gpr[5] = ctx->gpr[5] | 0xB15C;
        ctx->gpr[7] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[8] = (int64_t)(int32_t)((uint32_t)0x0 << 16);
        vm_write32(ctx->gpr[7] + 0x80, ctx->gpr[4]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[7] = ctx->gpr[8] | 0x8000;
        vm_write32((ctx->gpr[4] + ctx->gpr[5]), ctx->gpr[6]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x6C);
        ctx->gpr[5] = ctx->gpr[5] | ctx->gpr[7];
        vm_write32(ctx->gpr[4] + 0x6C, ctx->gpr[5]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x70);
        ctx->gpr[5] = (ctx->gpr[5] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], 0, 4, 4);
        vm_write32(ctx->gpr[4] + 0x70, ctx->gpr[5]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x74);
        ctx->gpr[5] = (ctx->gpr[5] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], 0, 10, 10);
        vm_write32(ctx->gpr[4] + 0x74, ctx->gpr[5]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x68);
        ctx->gpr[5] = ctx->gpr[5] | ctx->gpr[7];
        vm_write32(ctx->gpr[4] + 0x68, ctx->gpr[5]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x68);
        ctx->gpr[5] = (ctx->gpr[5] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], 0, 24, 24);
        vm_write32(ctx->gpr[4] + 0x68, ctx->gpr[5]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + -0x6430);
        vm_write32(ctx->gpr[4] + 0x58, ctx->gpr[6]);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x6430);
        vm_write16(ctx->gpr[3] + 0x5C, ctx->gpr[6]);
loc_000343A0:
        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xA0);
        ctx->lr = ctx->gpr[0];
        ctx->gpr[29] = _cs_29;
        ctx->gpr[30] = _cs_30;
        ctx->gpr[31] = _cs_31;
        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x90);
        return;
        return;
        return;
        return;
        return;
        return;
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xCB << 16);
        { uint64_t _a = ctx->gpr[3]; uint64_t _r = _a + (uint64_t)(int64_t)(0x7310); ctx->gpr[3] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        return;
}
