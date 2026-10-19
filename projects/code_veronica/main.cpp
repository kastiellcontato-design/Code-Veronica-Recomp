/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"
#include "cv_version.h"
#include "cv_srt.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"
#include "host_thread_name.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <string>

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#include <io.h>        /* _dup2: the log redirect of a double-clicked start */
#endif

#ifdef _WIN32
extern "C" void cv_fps_register_game_update();
#endif
#ifdef _WIN32
extern "C" int cv_focus_pause_poll(void);   /* cv_focus_pause.cpp */
#endif
extern "C" void pad_phys_rumble_off(void);   /* libs/input/pad_phys.c */

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
void     cellGame_init_from_paramsfo(const char* sfo_path);
int32_t  cellSaveData_set_root(const char* path);
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;

/* LiveSplit (livesplit/recvx-ps3recomp.asl) finds the game through this
 * export: a signature, where guest memory starts (guest address 0 -- the
 * game's variables sit at their PS3 addresses from there, big-endian) and
 * which release this executable runs. Laid out for the script: magic at 0,
 * vm_base at 16, title at 24, layout version at 40. */
extern "C" __declspec(dllexport) volatile struct CvLiveSplit {
    char magic[16];
    uint64_t vm_base;
    char title[16];
    uint32_t version;
} g_cv_livesplit = { "PS3RECOMP-CVX", 0, CV_TITLE_ID, 1 };
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

extern "C" void ps3_tl_begin(int ev);   /* PS3_TIMELINE, cellGcmSys.c */
extern "C" void ps3_tl_end(int ev);
extern "C" void ps3_tl_frame(void);
extern "C" int cellGcm_drain_more_ready(void);
extern "C" void ps3_tl_sleep_begin(void);
extern "C" void ps3_tl_sleep_end(void);

static void present_guest_frame(void)
{
    ps3_tl_begin(2);
    rsx_backend_present();
    ps3_tl_end(2);
    ps3_tl_frame();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

/* Last flip request this loop has presented. File scope so the FIFO drain's
 * catch-up present (below) can mark its flip as shown too. */
static unsigned g_last_flip = 0;
extern "C" void (*g_gcm_catchup_present)(void);

static void catchup_present(void)
{
    present_guest_frame();
    g_last_flip = cellGcm_flip_request_count();
}

extern "C" void ppu_report_guest_lrs(void);
extern "C" void cellFiberPpuContextReport(void);
extern void ps3_sampler_start(void);

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    g_gcm_catchup_present = catchup_present;
    /* The vblank clock, in microseconds off the performance counter.
     * GetTickCount64 advances in ~15.6 ms steps, so a 16 ms tick scheduled on
     * it fired in bursts: no vblank for one step, two at once on the next.
     * The title paces its frames on vblank counts, so even a steady 30 fps
     * left the screen at uneven intervals (a jagged frametime line in menus
     * that run at exactly 30). VBLANK_HZ overrides the 60 Hz rate. */
    LARGE_INTEGER qpf; QueryPerformanceFrequency(&qpf);
    auto now_us = [&qpf]() -> long long {
        LARGE_INTEGER c; QueryPerformanceCounter(&c);
        return (long long)(c.QuadPart / qpf.QuadPart) * 1000000ll
             + (long long)(c.QuadPart % qpf.QuadPart) * 1000000ll / qpf.QuadPart;
    };
    double vblank_us = 1000000.0 / 60.0;
    { const char* e = getenv("VBLANK_HZ"); if (e && atof(e) > 1.0) vblank_us = 1000000.0 / atof(e); }
    double next_tick = (double)now_us();

    ULONGLONG diag_at = GetTickCount64() + 20000;
    int progress_diag = getenv("CV_PROGRESS_DIAG") != nullptr;
    for (;;) {
        if (progress_diag && GetTickCount64() >= diag_at) {
            ppu_report_guest_lrs();
            cellFiberPpuContextReport();
            diag_at += 10000;
        }
        /* 1 ms, not 4: the guest's main thread polls a label this drain
         * writes, and every ms slept here after a present is a ms it spins. */
        /* No sleep when the drain stopped at a flip with the next frame
         * already queued: present it and keep translating. */
        if (!cellGcm_drain_more_ready()) {
            ps3_tl_sleep_begin();
            Sleep(1);
            ps3_tl_sleep_end();
        }
        const double now = (double)now_us();

        /* Paused (the window lost focus, cv_focus_pause.cpp): no vblank or
         * flip, the guest clock held. The FIFO drain and the window pump
         * below go on, so the frame in flight completes and a click on the
         * game window is seen. On resume the clock restarts from now, not
         * with a burst of the ticks the pause skipped. */
#ifdef _WIN32
        const bool paused = cv_focus_pause_poll() != 0;
#else
        const bool paused = false;
#endif
        if (paused) next_tick = now;

        int fired = 0;
        while (!paused && now >= next_tick && fired < 240) {
            ps3_tl_begin(11);
            cellGcmTickVBlank();
            cellGcmTickFlip();
            ps3_tl_end(11);
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
                g_last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick += vblank_us;      /* 60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
                g_last_flip = cellGcm_flip_request_count();
            }
            cellGcm_rsx_process_fifo();

            ps3_tl_begin(10);
            int pumped = rsx_backend_pump();
            ps3_tl_end(10);
            if (pumped != 0) {
                /* Window closed: end the process. Left running, the guest went
                 * on headless (CPU, audio) with nothing on screen. Terminate,
                 * not exit(): SPU and PPU threads are still mid-flight. A motor
                 * the game left running would keep going, so stop them first. */
                pad_phys_rumble_off();
                fflush(nullptr);
#ifdef _WIN32
                TerminateProcess(GetCurrentProcess(), 0);
#else
                std::_Exit(0);
#endif
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            if (fc != g_last_flip) {
                present_guest_frame();
                g_last_flip = fc;
            } else if (fc == 0) {
                rsx_backend_present();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    /* MEM_WRITE_WATCH: the RSX backend asks GetWriteWatch whether a vertex
     * texture was written since its last snapshot instead of comparing the
     * whole thing (a 1 MB matrix texture, dozens of times a frame). */
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
    if (!vm_base)   /* not every system grants write-watch on 4 GB; the backend falls back */
        vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

extern "C" int cv_launcher_run(char* eboot_out, size_t cap);
extern "C" const rsx_ui_quad* (*g_rsx_srt_hook)(uint32_t,uint32_t,uint32_t*);
extern "C" const rsx_ui_sprite* (*g_rsx_srt_sprites_hook)(uint32_t*,const uint32_t**,uint32_t*,uint32_t*,uint64_t*);
extern "C" void cv_button_icons_init(void);   /* button_icons.cpp */
extern "C" void ps3_patch_site(uint32_t addr, uint32_t word, const char* flag);   /* runtime/patches */
extern "C" void ps3_patch_writes_from_env(void);

int main(int argc, char** argv)
{
    std::string eboot_path;
#ifdef _WIN32
    /* Started by a double-click, this process owns its console: send the log
     * to run-log.txt beside the executable and drop the black window. (The
     * launcher script redirects the log itself and shares its console.) */
    { DWORD pl[4];
      if (GetConsoleWindow() && GetConsoleProcessList(pl, 4) == 1) {
          char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH);
          std::string log = (std::filesystem::path(exe).parent_path() / "run-log.txt").string();
          /* freopen (not freopen_s) shares the file, so the log can be read while the game runs */
          if (freopen(log.c_str(), "w", stdout)) _dup2(_fileno(stdout), _fileno(stderr));
          FreeConsole();
      } }
    /* Xbox button icons: the game's own Xbox 360 art, swapped in per archive. */
    cv_button_icons_init();
    g_rsx_srt_hook = cv_srt_quads;
    g_rsx_srt_sprites_hook = cv_srt_sprites;
    /* The switchable sites lifted into this build (projects/code_veronica/
     * patches.json, this version's sites): a patch file with the same write
     * switches them. */
    ps3_patch_site(CV_DOOR_SKIP_SITE, 0x38600000, "cv_door_skip");

    /* The options window. CV_NO_LAUNCHER=1 skips it (tests, scripts). */
    if (!getenv("CV_NO_LAUNCHER")) {
        char chosen[1024] = {0};
        if (!cv_launcher_run(chosen, sizeof chosen)) return 0;
        eboot_path = chosen;
    } else
#endif
    if (argc >= 2) {
        eboot_path = argv[1];
    } else {
        namespace fs = std::filesystem;
        const char* root_env = getenv("PS3_VFS_ROOT");
        fs::path game_root;
        if (root_env && *root_env) {
            game_root = fs::path(root_env);
        } else {
            /* The project executable lives under
             * <ps3recomp>/projects/code_veronica/<build>/.
             * This default makes a direct double-click work for this setup. */
            fs::path exe_dir = fs::absolute(argv[0]).parent_path();
            game_root = exe_dir.parent_path().parent_path().parent_path()
                      / "JOGO" / CV_TITLE_ID;
        }

        fs::path candidate = game_root / "USRDIR" / "EBOOT.elf";
        if (!fs::exists(candidate)) {
            fprintf(stderr, "Nao encontrei o EBOOT.elf em:\n  %s\n",
                    candidate.string().c_str());
            fprintf(stderr, "Abra pelo atalho Abrir Code Veronica ou passe o caminho do EBOOT.elf.\n");
            return 2;
        }
        eboot_path = candidate.string();

        /* derive_vfs_root honors this override; without it, the usual PS3_GAME
         * folder layout would derive the wrong root for this JOGO/<title> path. */
#ifdef _WIN32
        _putenv_s("PS3_VFS_ROOT", game_root.string().c_str());
#else
        setenv("PS3_VFS_ROOT", game_root.string().c_str(), 1);
#endif
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    /* Keep this process's requested timer precision when its game window is
     * covered. Windows 11 may otherwise round media waits to ~15.6ms. */
    PROCESS_POWER_THROTTLING_STATE power = {};
    power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u;
    power.StateMask = 0;
    if(!SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&power,sizeof(power)))
        fprintf(stderr,"[boot] precise background timers unavailable: %lu\n",GetLastError());
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    if (cellSaveData_set_root(getenv("PS3_SAVE_ROOT")) != 0) {
        fprintf(stderr, "ERROR: invalid save directory (PS3_SAVE_ROOT)\n");
        return 2;
    }

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }
    g_cv_livesplit.vm_base = (uint64_t)(uintptr_t)vm_base;

    uint32_t entry = ppu_load_elf(eboot_path.c_str());
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", eboot_path.c_str());
        return 1;
    }

    ps3_patch_writes_from_env();   /* patch files: the data they change, before the game reads it */

    derive_vfs_root(eboot_path.c_str());
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    /* Use the title ID from this game's PARAM.SFO for /dev_hdd0/game paths.
     * Without this, cellGame keeps its BLES00000 fallback and checks the
     * wrong title directory. */
    {
        static const char* const kSfoLayouts[] = {
            "%s/PS3_GAME/PARAM.SFO", /* VFS root is the disc root */
            "%s/../PARAM.SFO",       /* VFS root is USRDIR */
            "%s/PARAM.SFO",          /* VFS root is the title root */
        };
        char sfo[1200];
        int found = 0;
        for (size_t i = 0; i < sizeof kSfoLayouts / sizeof kSfoLayouts[0]; i++) {
            snprintf(sfo, sizeof sfo, kSfoLayouts[i], ppu_vfs_root);
            FILE* f = fopen(sfo, "rb");
            if (f) { fclose(f); found = 1; break; }
        }
        if (!found)
            snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        cellGame_init_from_paramsfo(sfo);
    }

    ppu_recomp_register();   /* lifted function table -> address map */
#ifdef _WIN32
    cv_fps_register_game_update();
#endif
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    ps3_sampler_start();     /* opt-in PS3_SAMPLE diagnostics */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    {
        HANDLE fc = CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);
        ps3_host_thread_name(fc, "frame_clock (vblank+render)");
        /* It drains the FIFO the guest's frame waits on; keep it ahead of the
         * guest's own busy threads. FRAME_CLOCK_PRIO=0 leaves it normal. */
        const char* pe = getenv("FRAME_CLOCK_PRIO");
        if (fc && (!pe || *pe != '0')) SetThreadPriority(fc, THREAD_PRIORITY_ABOVE_NORMAL);
    }
    ps3_host_thread_name(GetCurrentThread(), "ppu:main");

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
