// Native EU UI fade/reset routines; the room fader is a different mechanism.
static void native_ui_fade_fixture(ppu_context* ctx) {
        vm_write32(ctx->gpr[3] + 0x40, ctx->gpr[5]);
        ctx->gpr[7] = (int64_t)(int32_t)(1);
        ctx->gpr[5] = vm_read32(ctx->gpr[3] + 0x30);
        ctx->gpr[8] = (int64_t)(int32_t)(0);
        vm_write32(ctx->gpr[3] + 0x3C, ctx->gpr[4]);
        vm_write32(ctx->gpr[3] + 0x44, ctx->gpr[6]);
        vm_write8(ctx->gpr[3] + 0x39, ctx->gpr[7]);
        ctx->gpr[4] = vm_read32(ctx->gpr[5] + 0x4);
        ctx->gpr[4] = ctx->gpr[4] | ((uint64_t)0x2 << 16);
        vm_write32(ctx->gpr[5] + 0x4, ctx->gpr[4]);
        ctx->gpr[4] = vm_read32(ctx->gpr[3] + 0x30);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x40);
        { uint64_t _a = ctx->gpr[4]; uint64_t _r = _a + (uint64_t)(int64_t)(0x300); ctx->gpr[4] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->gpr[3] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[3], 24, 0, 7);
        ctx->gpr[4] = ppc_rldicl(ctx->gpr[4], 0, 32);
        ctx->gpr[3] = (ctx->gpr[3] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[8], 16, 8, 15);
        ctx->gpr[3] = (ctx->gpr[3] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[8], 8, 16, 23);
        ctx->gpr[3] = (ctx->gpr[3] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[8], 0, 24, 31);
        vm_write32(ctx->gpr[4] + 0x0, ctx->gpr[3]);
        return;
        ctx->gpr[4] = (int64_t)(int32_t)((uint32_t)0xAC << 16);
        ctx->gpr[5] = (int64_t)(int32_t)(0xFF);
        ctx->gpr[6] = (int64_t)(int32_t)(0);
        ctx->gpr[7] = (int64_t)(int32_t)(1);
        ctx->gpr[4] = vm_read32(ctx->gpr[4] + 0x968);
        vm_write32(ctx->gpr[4] + 0x40, ctx->gpr[5]);
        vm_write32(ctx->gpr[4] + 0x3C, ctx->gpr[6]);
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x30);
        vm_write32(ctx->gpr[4] + 0x44, ctx->gpr[3]);
        vm_write8(ctx->gpr[4] + 0x39, ctx->gpr[7]);
        ctx->gpr[3] = vm_read32(ctx->gpr[5] + 0x4);
        ctx->gpr[3] = ctx->gpr[3] | ((uint64_t)0x2 << 16);
        vm_write32(ctx->gpr[5] + 0x4, ctx->gpr[3]);
        ctx->gpr[3] = vm_read32(ctx->gpr[4] + 0x30);
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x40);
        { uint64_t _a = ctx->gpr[3]; uint64_t _r = _a + (uint64_t)(int64_t)(0x300); ctx->gpr[3] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->gpr[5] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[5], 24, 0, 7);
        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);
        ctx->gpr[5] = (ctx->gpr[5] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], 16, 8, 15);
        ctx->gpr[5] = (ctx->gpr[5] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], 8, 16, 23);
        ctx->gpr[5] = (ctx->gpr[5] & 0xFFFFFFFF00000000ull) | (uint64_t)ppc_rlwimi((uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], 0, 24, 31);
        vm_write32(ctx->gpr[3] + 0x0, ctx->gpr[5]);
        vm_write8(ctx->gpr[4] + 0x4C, ctx->gpr[6]);
        return;
        return;
}

static void native_ui_return_fixture(ppu_context* ctx) {
        uint64_t _cs_31 = ctx->gpr[31];
        vm_write64(ctx->gpr[1] + -0x80, ctx->gpr[1]); ctx->gpr[1] += -0x80;
        ctx->gpr[0] = ctx->lr;
        vm_write64(ctx->gpr[1] + 0x90, ctx->gpr[0]);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xA9 << 16);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x6430);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x74);
        ctx->gpr[3] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[3], 0, 31, 31);
        vm_write64(ctx->gpr[1] + 0x78, ctx->gpr[31]);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if ((!((ctx->cr >> 28) & 2))) goto loc_001D7E60;
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0xAC << 16);
        ctx->gpr[4] = (int64_t)(int32_t)(0);
        ctx->gpr[5] = (int64_t)(int32_t)(0xFF);
        ctx->gpr[6] = (int64_t)(int32_t)(1);
        ctx->gpr[31] = vm_read32(ctx->gpr[3] + 0x968);
        ctx->gpr[3] = ctx->gpr[31] | 0x0;
        ctx->lr = 0x001D7E58; native_ui_fade_fixture(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = (int64_t)(int32_t)(0);
        vm_write8(ctx->gpr[31] + 0x4C, ctx->gpr[3]);
loc_001D7E60:
        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x90);
        ctx->lr = ctx->gpr[0];
        ctx->gpr[31] = _cs_31;
        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x80);
        return;
}
