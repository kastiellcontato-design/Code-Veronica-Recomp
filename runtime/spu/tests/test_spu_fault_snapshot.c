/* Isolated synthetic VM: a bad pointer in the fault must not fault again. */
#include "../spu_fault_snapshot.h"
#include <windows.h>
#include <assert.h>

uint8_t* vm_base;

static unsigned verify(const char* path)
{
    FILE* f = fopen(path, "rb"); assert(f);
    char magic[16]; uint32_t h[16], g[128][4];
    assert(fread(magic, 1, 16, f) == 16 && !strcmp(magic, "SPUFAULT-v1"));
    assert(fread(h, 4, 16, f) == 16 && h[0] == 1 && h[3] == 10 && h[4] == 0x20D54);
    assert(fread(g, 1, sizeof(g), f) == sizeof(g) && g[80][2] == 0xFFFFFFFFu);
    uint8_t* ls = malloc(SPU_LS_SIZE); assert(ls);
    assert(fread(ls, 1, SPU_LS_SIZE, f) == SPU_LS_SIZE && ls[0x39080] == 0xA5);
    free(ls);
    unsigned count = 0, src = 0, top = 0;
    uint32_t ea, n; uint8_t bytes[0x4100];
    while (fread(&ea, 4, 1, f) == 1) {
        assert(fread(&n, 4, 1, f) == 1 && n && n <= sizeof(bytes));
        assert((uint64_t)ea + n <= 0x100000000ULL);
        assert(ea != 0x20000 && ea != 0x30000); /* protected/uncommitted */
        assert(fread(bytes, 1, n, f) == n);
        if (ea == 0x10020) { src++; assert(n == 64 && bytes[0] == 0x5A); }
        if (ea == 0xFFFFFFFFu) { top++; assert(n == 1 && bytes[0] == 0x77); }
        count++;
    }
    assert(feof(f) && count == 2 && src == 1 && top == 1);
    fclose(f);
    return count;
}

int main(void)
{
    char tmp[MAX_PATH], dir[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    assert(GetTempPathA(sizeof(tmp), tmp));
    snprintf(dir, sizeof(dir), "%scv-spu-fault-test-%lu-%llu", tmp,
             GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    assert(CreateDirectoryA(dir, NULL));
    spu_context* ctx = calloc(1, sizeof(*ctx)); assert(ctx);
    ctx->ls = ctx->ls_store;
    ctx->image_id = 10; ctx->pc = 0x20D54;
    ctx->mfc_eal = 0x10020; ctx->mfc_size = 64;
    ctx->ls[0x39080] = 0xA5;
    ctx->gpr[80]._u32[2] = 0xFFFFFFFFu;
    ctx->gpr[81]._u32[0] = 0x20000;
    ctx->gpr[82]._u32[0] = 0x30000;
    ctx->gpr[83]._u32[0] = 0x10020; /* duplicate source is not written twice */
    vm_base = VirtualAlloc(NULL, 0x100000000ULL, MEM_RESERVE, PAGE_READWRITE); assert(vm_base);
    assert(VirtualAlloc(vm_base + 0x10000, 0x10000, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(vm_base + 0x20000, 0x10000, MEM_COMMIT, PAGE_NOACCESS));
    assert(VirtualAlloc(vm_base + 0xFFFF0000u, 0x10000, MEM_COMMIT, PAGE_READWRITE));
    vm_base[0x10020] = 0x5A; vm_base[0xFFFFFFFFu] = 0x77;
    _putenv_s("SPU_FAULT_DIR", "");
    assert(!spu_fault_snapshot(ctx, 0, 0, 0));
    _putenv_s("SPU_FAULT_DIR", dir);
    spu_fault_event("dispatch",0x10000,0,10,1);
    for(unsigned i=0;i<600;i++) spu_fault_event("test-history",0x10000,0,10,i);
    assert(!spu_fault_snapshot(NULL, 0, 0, 0));
    assert(spu_fault_snapshot(ctx, 0, 0, 0));
    assert(spu_fault_snapshot(ctx, 0, 0, 0));
    assert(!spu_fault_snapshot(ctx, 0, 0, 0)); /* process-wide hard limit */
    snprintf(pattern, sizeof(pattern), "%s\\*.spufault", dir);
    WIN32_FIND_DATAA fd; HANDLE find = FindFirstFileA(pattern, &fd); assert(find != INVALID_HANDLE_VALUE);
    unsigned files = 0;
    do {
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        verify(path); assert(DeleteFileA(path)); files++;
        char event_path[MAX_PATH];
        snprintf(event_path,sizeof(event_path),"%s.events.txt",path);
        FILE* events=fopen(event_path,"r"); assert(events);
        char line[512]; unsigned lines=0;
        while(fgets(line,sizeof(line),events)) lines++;
        fclose(events); assert(lines==513); /* heading + exactly 512 retained events */
        assert(DeleteFileA(event_path));
    } while (FindNextFileA(find, &fd));
    FindClose(find); assert(files == 2);
    assert(RemoveDirectoryA(dir)); /* only our own now-empty temporary directory */
    VirtualFree(vm_base, 0, MEM_RELEASE); free(ctx);
    puts("SPU fault snapshot: PASS (off by default, exact state, safe invalid pointers, bounds, deduplication, two-file limit)");
    return 0;
}
