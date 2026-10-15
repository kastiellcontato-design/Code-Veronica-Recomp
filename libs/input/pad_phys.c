/*
 * ps3recomp - physical controller reader (see pad_phys.h)
 */
#include "pad_phys.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

static const char* const k_ids[PB_COUNT] = {
    "south", "east", "west", "north", "l1", "r1", "l2", "r2", "l3", "r3",
    "select", "start", "up", "down", "left", "right", "home", "touch"
};
const char* pad_phys_id(int pb) { return (pb >= 0 && pb < PB_COUNT) ? k_ids[pb] : "none"; }
int pad_phys_from_id(const char* id)
{
    for (int i = 0; i < PB_COUNT; i++) if (!_stricmp(id, k_ids[i])) return i;
    return -1;
}

/* ---- XInput ------------------------------------------------------------ */
static uint8_t xi_axis(short v, int invert)
{
    const int dz = 7849;
    float n = 0.0f;
    if (v > dz) n = (float)(v - dz) / (float)(32767 - dz);
    else if (v < -dz) n = (float)(v + dz) / (float)(32767 - dz);
    if (invert) n = -n;
    int r = (int)(n * 127.0f) + 128;
    return (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
}

static int xi_read(int i, PadPhys* p)
{
    XINPUT_STATE st; memset(&st, 0, sizeof st);
    if (XInputGetState((DWORD)i, &st) != ERROR_SUCCESS) return 0;
    const XINPUT_GAMEPAD* g = &st.Gamepad;
    uint32_t b = 0;
    if (g->wButtons & XINPUT_GAMEPAD_A)              b |= 1u << PB_SOUTH;
    if (g->wButtons & XINPUT_GAMEPAD_B)              b |= 1u << PB_EAST;
    if (g->wButtons & XINPUT_GAMEPAD_X)              b |= 1u << PB_WEST;
    if (g->wButtons & XINPUT_GAMEPAD_Y)              b |= 1u << PB_NORTH;
    if (g->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  b |= 1u << PB_L1;
    if (g->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) b |= 1u << PB_R1;
    if (g->bLeftTrigger  > 30)                       b |= 1u << PB_L2;
    if (g->bRightTrigger > 30)                       b |= 1u << PB_R2;
    if (g->wButtons & XINPUT_GAMEPAD_LEFT_THUMB)     b |= 1u << PB_L3;
    if (g->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)    b |= 1u << PB_R3;
    if (g->wButtons & XINPUT_GAMEPAD_BACK)           b |= 1u << PB_SELECT;
    if (g->wButtons & XINPUT_GAMEPAD_START)          b |= 1u << PB_START;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_UP)        b |= 1u << PB_UP;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)      b |= 1u << PB_DOWN;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)      b |= 1u << PB_LEFT;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)     b |= 1u << PB_RIGHT;
    p->connected = 1; p->kind = PAD_KIND_XINPUT; p->buttons = b;
    p->lx = xi_axis(g->sThumbLX, 0); p->ly = xi_axis(g->sThumbLY, 1);
    p->rx = xi_axis(g->sThumbRX, 0); p->ry = xi_axis(g->sThumbRY, 1);
    p->l2 = g->bLeftTrigger; p->r2 = g->bRightTrigger;
    return 1;
}

/* ---- Sony over HID -------------------------------------------------------
 * A thread enumerates HID devices with Sony's vendor id every two seconds and
 * gives each controller its own reader thread. Reports are parsed straight
 * into a PadPhys under a lock; a failed read (unplugged) frees the slot. */
#define SONY_MAX 4
typedef struct {
    volatile LONG used;
    HANDLE   h;
    int      ds5;          /* DualSense family */
    int      bt;           /* over Bluetooth (its input report is not the 64-byte USB one) */
    USHORT   in_len;       /* InputReportByteLength */
    USHORT   out_len;      /* OutputReportByteLength */
    int      rumble;       /* last motors sent, large << 8 | weak; -1 = none yet */
    uint8_t  seq;          /* DualSense Bluetooth output sequence */
    SRWLOCK  lock;
    PadPhys  st;
    char     path[512];
} SonyDev;
static SonyDev s_sony[SONY_MAX];
static volatile LONG s_sony_started;

static void sony_hat(uint8_t hat, uint32_t* b)
{
    switch (hat & 0x0F) {
    case 0: *b |= 1u << PB_UP; break;
    case 1: *b |= (1u << PB_UP) | (1u << PB_RIGHT); break;
    case 2: *b |= 1u << PB_RIGHT; break;
    case 3: *b |= (1u << PB_DOWN) | (1u << PB_RIGHT); break;
    case 4: *b |= 1u << PB_DOWN; break;
    case 5: *b |= (1u << PB_DOWN) | (1u << PB_LEFT); break;
    case 6: *b |= 1u << PB_LEFT; break;
    case 7: *b |= (1u << PB_UP) | (1u << PB_LEFT); break;
    default: break;
    }
}

/* face = hat byte (low nibble hat, high nibble square/cross/circle/triangle);
 * sh = L1 R1 L2 R2 share options L3 R3; ps = PS, touchpad. */
static void sony_fill(PadPhys* p, int kind, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry,
                      uint8_t face, uint8_t sh, uint8_t ps, uint8_t l2, uint8_t r2)
{
    uint32_t b = 0;
    sony_hat(face, &b);
    if (face & 0x10) b |= 1u << PB_WEST;
    if (face & 0x20) b |= 1u << PB_SOUTH;
    if (face & 0x40) b |= 1u << PB_EAST;
    if (face & 0x80) b |= 1u << PB_NORTH;
    if (sh & 0x01) b |= 1u << PB_L1;
    if (sh & 0x02) b |= 1u << PB_R1;
    if (sh & 0x04) b |= 1u << PB_L2;
    if (sh & 0x08) b |= 1u << PB_R2;
    if (sh & 0x10) b |= 1u << PB_SELECT;
    if (sh & 0x20) b |= 1u << PB_START;
    if (sh & 0x40) b |= 1u << PB_L3;
    if (sh & 0x80) b |= 1u << PB_R3;
    if (ps & 0x01) b |= 1u << PB_HOME;
    if (ps & 0x02) b |= 1u << PB_TOUCH;
    p->connected = 1; p->kind = kind; p->buttons = b;
    p->lx = lx; p->ly = ly; p->rx = rx; p->ry = ry; p->l2 = l2; p->r2 = r2;
}

static int sony_parse(SonyDev* d, const uint8_t* r, DWORD n, PadPhys* p)
{
    if (n < 10) return 0;
    if (!d->ds5) {
        const uint8_t* q;
        if (r[0] == 0x01) q = r + 1;                       /* USB, or Bluetooth basic */
        else if (r[0] == 0x11 && n >= 13) q = r + 3;       /* Bluetooth full */
        else return 0;
        sony_fill(p, PAD_KIND_DS4, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8]);
        return 1;
    }
    if (r[0] == 0x31 && n >= 12) {                         /* DualSense Bluetooth full */
        const uint8_t* q = r + 2;
        sony_fill(p, PAD_KIND_DUALSENSE, q[0], q[1], q[2], q[3], q[7], q[8], q[9], q[4], q[5]);
        return 1;
    }
    if (r[0] == 0x01) {
        if (d->in_len == 64) {                             /* DualSense USB */
            const uint8_t* q = r + 1;
            sony_fill(p, PAD_KIND_DUALSENSE, q[0], q[1], q[2], q[3], q[7], q[8], q[9], q[4], q[5]);
        } else {                                           /* Bluetooth simple: DS4-like */
            const uint8_t* q = r + 1;
            sony_fill(p, PAD_KIND_DUALSENSE, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8]);
        }
        return 1;
    }
    return 0;
}

static DWORD WINAPI sony_reader(LPVOID arg)
{
    SonyDev* d = (SonyDev*)arg;
    uint8_t buf[1024];
    OVERLAPPED ov; memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (;;) {
        DWORD got = 0;
        ResetEvent(ov.hEvent);
        DWORD want = d->in_len ? d->in_len : sizeof buf;
        if (want > sizeof buf) want = sizeof buf;
        if (!ReadFile(d->h, buf, want, &got, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) break;
            if (!GetOverlappedResult(d->h, &ov, &got, TRUE)) break;
        }
        PadPhys p; memset(&p, 0, sizeof p);
        if (sony_parse(d, buf, got, &p)) {
            AcquireSRWLockExclusive(&d->lock); d->st = p; ReleaseSRWLockExclusive(&d->lock);
        }
    }
    CloseHandle(ov.hEvent);
    AcquireSRWLockExclusive(&d->lock); memset(&d->st, 0, sizeof d->st); ReleaseSRWLockExclusive(&d->lock);
    CloseHandle(d->h); d->h = NULL; d->path[0] = 0;
    InterlockedExchange(&d->used, 0);
    return 0;
}

static int sony_pid_kind(USHORT pid)
{
    switch (pid) {
    case 0x05C4: case 0x09CC: case 0x0BA0: return PAD_KIND_DS4;
    case 0x0CE6: case 0x0DF2:              return PAD_KIND_DUALSENSE;
    default: return 0;
    }
}

static void sony_scan(void)
{
    GUID hid; HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsA(&hid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA ifd; ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &hid, i, &ifd); i++) {
        char dbuf[1024];
        SP_DEVICE_INTERFACE_DETAIL_DATA_A* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)dbuf;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, sizeof dbuf, NULL, NULL)) continue;
        int open_already = 0;
        for (int k = 0; k < SONY_MAX; k++)
            if (s_sony[k].used && !_stricmp(s_sony[k].path, det->DevicePath)) open_already = 1;
        if (open_already) continue;
        HANDLE h = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES at; at.Size = sizeof at;
        int kind = 0;
        if (HidD_GetAttributes(h, &at) && at.VendorID == 0x054C) kind = sony_pid_kind(at.ProductID);
        USHORT in_len = 0, out_len = 0;
        if (kind) {
            PHIDP_PREPARSED_DATA pp = NULL; HIDP_CAPS caps;
            if (HidD_GetPreparsedData(h, &pp)) {
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                    in_len = caps.InputReportByteLength;
                    out_len = caps.OutputReportByteLength;
                    /* the gamepad collection only (usage page 1, usage 5) */
                    if (!(caps.UsagePage == 1 && caps.Usage == 5)) kind = 0;
                }
                HidD_FreePreparsedData(pp);
            }
        }
        int slot = -1;
        if (kind) for (int k = 0; k < SONY_MAX; k++)
            if (InterlockedCompareExchange(&s_sony[k].used, 1, 0) == 0) { slot = k; break; }
        if (slot < 0) { CloseHandle(h); continue; }
        SonyDev* d = &s_sony[slot];
        d->h = h; d->ds5 = (kind == PAD_KIND_DUALSENSE); d->in_len = in_len;
        d->out_len = out_len; d->bt = in_len != 64; d->rumble = -1; d->seq = 0;
        strncpy(d->path, det->DevicePath, sizeof d->path - 1);
        HANDLE th = CreateThread(NULL, 0, sony_reader, d, 0, NULL);
        if (th) CloseHandle(th);
        else { CloseHandle(h); d->h = NULL; d->path[0] = 0; InterlockedExchange(&d->used, 0); }
    }
    SetupDiDestroyDeviceInfoList(set);
}

static DWORD WINAPI sony_manager(LPVOID arg)
{
    (void)arg;
    for (;;) { sony_scan(); Sleep(2000); }
    return 0;
}

static void sony_start(void)
{
    if (InterlockedCompareExchange(&s_sony_started, 1, 0) != 0) return;
    for (int k = 0; k < SONY_MAX; k++) InitializeSRWLock(&s_sony[k].lock);
    if (getenv("PAD_NO_HID")) return;
    HANDLE th = CreateThread(NULL, 0, sony_manager, NULL, 0, NULL);
    if (th) CloseHandle(th);
}

/* ---- Rumble ----------------------------------------------------------------
 * Sony controllers take it in an HID output report: DualShock 4 report 0x05
 * (USB) / 0x11 (Bluetooth), DualSense 0x02 (USB) / 0x31 (Bluetooth, in its
 * "compatible vibration" mode). Bluetooth reports end in a CRC-32 of 0xA2 plus
 * the report. Only the motor bits are flagged valid, so the light bar and the
 * rest keep their state. */
static uint32_t crc32_bt(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t a2 = 0xA2;
    for (size_t i = 0; i <= n; i++) {
        c ^= i == 0 ? a2 : p[i - 1];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void sony_write(SonyDev* d, const uint8_t* buf, DWORD len)
{
    OVERLAPPED ov; memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return;
    DWORD put = 0;
    if (!WriteFile(d->h, buf, len, &put, &ov) && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 100) != WAIT_OBJECT_0) CancelIo(d->h);
        GetOverlappedResult(d->h, &ov, &put, TRUE);
    }
    CloseHandle(ov.hEvent);
}

static void sony_rumble(SonyDev* d, uint8_t large, uint8_t weak)
{
    uint8_t b[96]; memset(b, 0, sizeof b);
    DWORD len;
    if (!d->ds5 && !d->bt) {                       /* DualShock 4, USB */
        b[0] = 0x05; b[1] = 0x01; b[4] = weak; b[5] = large;
        len = 32;
    } else if (!d->ds5) {                          /* DualShock 4, Bluetooth */
        b[0] = 0x11; b[1] = 0xC0; b[3] = 0x01; b[6] = weak; b[7] = large;
        uint32_t c = crc32_bt(b, 74);
        b[74] = (uint8_t)c; b[75] = (uint8_t)(c >> 8); b[76] = (uint8_t)(c >> 16); b[77] = (uint8_t)(c >> 24);
        len = 78;
    } else if (!d->bt) {                           /* DualSense, USB */
        b[0] = 0x02; b[1] = 0x03; b[3] = weak; b[4] = large;
        len = 48;
    } else {                                       /* DualSense, Bluetooth */
        b[0] = 0x31; b[1] = (uint8_t)(d->seq << 4); b[2] = 0x10;
        d->seq = (uint8_t)((d->seq + 1) & 0x0F);
        b[3] = 0x03; b[5] = weak; b[6] = large;
        uint32_t c = crc32_bt(b, 74);
        b[74] = (uint8_t)c; b[75] = (uint8_t)(c >> 8); b[76] = (uint8_t)(c >> 16); b[77] = (uint8_t)(c >> 24);
        len = 78;
    }
    if (d->out_len && d->out_len > len && d->out_len <= sizeof b) len = d->out_len;
    sony_write(d, b, len);
}

void pad_phys_rumble(int index, uint8_t large, uint8_t weak)
{
    sony_start();
    int n = 0;
    for (int i = 0; i < 4; i++) {
        XINPUT_STATE st;
        if (XInputGetState((DWORD)i, &st) != ERROR_SUCCESS) continue;
        if (n++ == index) {
            XINPUT_VIBRATION v;
            v.wLeftMotorSpeed = (WORD)(large * 257);
            v.wRightMotorSpeed = (WORD)(weak * 257);
            XInputSetState((DWORD)i, &v);
            return;
        }
    }
    for (int k = 0; k < SONY_MAX; k++) {
        SonyDev* d = &s_sony[k];
        if (!d->used) continue;
        AcquireSRWLockShared(&d->lock);
        int on = d->st.connected;
        ReleaseSRWLockShared(&d->lock);
        if (!on) continue;
        if (n++ != index) continue;
        const int want = large << 8 | weak;
        if (want != d->rumble && d->h) {
            d->rumble = want;
            sony_rumble(d, large, weak);
        }
        return;
    }
}

void pad_phys_rumble_off(void)
{
    for (int i = 0; i < PAD_PHYS_MAX; i++) pad_phys_rumble(i, 0, 0);
}

int pad_phys_poll(int index, PadPhys* out)
{
    memset(out, 0, sizeof *out);
    sony_start();
    int n = 0;
    for (int i = 0; i < 4; i++) {
        PadPhys p; memset(&p, 0, sizeof p);
        if (xi_read(i, &p)) { if (n == index) { *out = p; return 1; } n++; }
    }
    for (int k = 0; k < SONY_MAX; k++) {
        if (!s_sony[k].used) continue;
        PadPhys p;
        AcquireSRWLockShared(&s_sony[k].lock); p = s_sony[k].st; ReleaseSRWLockShared(&s_sony[k].lock);
        if (!p.connected) continue;
        if (n == index) { *out = p; return 1; }
        n++;
    }
    return 0;
}

#else  /* !_WIN32: no physical reader here; cellPad keeps its SDL2 backend */
#include <strings.h>
static const char* const k_ids[PB_COUNT] = {
    "south", "east", "west", "north", "l1", "r1", "l2", "r2", "l3", "r3",
    "select", "start", "up", "down", "left", "right", "home", "touch"
};
const char* pad_phys_id(int pb) { return (pb >= 0 && pb < PB_COUNT) ? k_ids[pb] : "none"; }
int pad_phys_from_id(const char* id)
{
    for (int i = 0; i < PB_COUNT; i++) if (!strcasecmp(id, k_ids[i])) return i;
    return -1;
}
int pad_phys_poll(int index, PadPhys* out) { (void)index; memset(out, 0, sizeof *out); return 0; }
void pad_phys_rumble(int index, uint8_t large, uint8_t weak) { (void)index; (void)large; (void)weak; }
void pad_phys_rumble_off(void) { }
#endif
