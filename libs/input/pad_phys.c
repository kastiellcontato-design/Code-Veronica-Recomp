/*
 * ps3recomp - physical controller reader (see pad_phys.h)
 */
#include "pad_phys.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

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

/* PAD_360_LAYOUT: "on" for every controller, "off", or "auto" (the default)
 * for Xbox controllers only. Read on every poll, so a launcher can switch it. */
int pad_phys_360_layout(int kind)
{
    const char* e = getenv("PAD_360_LAYOUT");
    if (e && !strcmp(e, "on")) return 1;
    if (e && !strcmp(e, "off")) return 0;
    return kind == PAD_KIND_XINPUT;
}

/* The Xbox 360 releases of PS3 games put the PS3's L1/R1 actions on the
 * triggers and its Select/Start on Start/Back: aim with RT (R2 on a Sony pad),
 * Status on Start. Applied as the pad is read, so the game, a launcher's
 * button capture and its labels all see the same thing. */
static void apply_360_layout(PadPhys* p)
{
    if (!pad_phys_360_layout(p->kind)) return;
    const uint32_t b = p->buttons;
    const uint32_t sw = (1u << PB_L1) | (1u << PB_R1) | (1u << PB_L2) | (1u << PB_R2) |
                        (1u << PB_SELECT) | (1u << PB_START);
    uint32_t o = b & ~sw;
    if (b & (1u << PB_L2))     o |= 1u << PB_L1;
    if (b & (1u << PB_R2))     o |= 1u << PB_R1;
    if (b & (1u << PB_L1))     o |= 1u << PB_L2;
    if (b & (1u << PB_R1))     o |= 1u << PB_R2;
    if (b & (1u << PB_START))  o |= 1u << PB_SELECT;
    if (b & (1u << PB_SELECT)) o |= 1u << PB_START;
    p->buttons = o;
    p->l2 = (o & (1u << PB_L2)) ? 255 : 0;   /* the bumpers are digital */
    p->r2 = (o & (1u << PB_R2)) ? 255 : 0;
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

/* ---- Sony and Nintendo over HID -------------------------------------------
 * A thread enumerates HID devices with Sony's or Nintendo's vendor id every
 * two seconds and gives each controller its own reader thread. Reports are
 * parsed straight into a PadPhys under a lock; a failed read (unplugged) frees
 * the slot. */
#define HID_MAX 8
enum { NIN_NONE = 0, NIN_PRO, NIN_JOY_L, NIN_JOY_R, NIN_GRIP };   /* NIN_GRIP: side asked at start */
typedef struct {
    volatile LONG used;
    HANDLE   h;
    int      ds5;          /* DualSense family */
    int      nin;          /* NIN_*: a Nintendo controller (0: Sony) */
    int      bt;           /* over Bluetooth (its input report is not the 64-byte USB one) */
    USHORT   in_len;       /* InputReportByteLength */
    USHORT   out_len;      /* OutputReportByteLength */
    int      rumble;       /* last motors sent, large << 8 | weak; -1 = none yet */
    uint8_t  seq;          /* DualSense Bluetooth output sequence; Nintendo packet counter */
    SRWLOCK  lock;
    PadPhys  st;           /* the pad as it stands alone (a Joy-Con: held sideways) */
    PadPhys  half;         /* Joy-Con: its half of a pair, held upright */
    volatile LONG want;    /* Nintendo: motors asked for, large << 8 | weak (its reader sends them) */
    int      cal[2][6];    /* Nintendo sticks: x/y centre, x/y below centre, x/y above centre */
    char     path[512];
} HidDev;
static HidDev s_hid[HID_MAX];
static volatile LONG s_hid_started;

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

static int sony_parse(HidDev* d, const uint8_t* r, DWORD n, PadPhys* p)
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

/* One report, waiting at most `ms`: its length, 0 on time out, -1 when the
 * device is gone. */
static int hid_read(HidDev* d, uint8_t* buf, DWORD cap, DWORD ms)
{
    OVERLAPPED ov; memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return -1;
    DWORD want = d->in_len ? d->in_len : cap, got = 0;
    if (want > cap) want = cap;
    int r;
    if (ReadFile(d->h, buf, want, &got, &ov)) r = (int)got;
    else if (GetLastError() != ERROR_IO_PENDING) r = -1;
    else if (WaitForSingleObject(ov.hEvent, ms) == WAIT_OBJECT_0)
        r = GetOverlappedResult(d->h, &ov, &got, FALSE) ? (int)got : -1;
    else { CancelIo(d->h); r = GetOverlappedResult(d->h, &ov, &got, TRUE) ? (int)got : 0; }
    CloseHandle(ov.hEvent);
    return r;
}

static void hid_write(HidDev* d, const uint8_t* buf, DWORD len)
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

static void hid_release(HidDev* d)
{
    AcquireSRWLockExclusive(&d->lock);
    memset(&d->st, 0, sizeof d->st); memset(&d->half, 0, sizeof d->half);
    ReleaseSRWLockExclusive(&d->lock);
    CloseHandle(d->h); d->h = NULL; d->path[0] = 0;
    InterlockedExchange(&d->used, 0);
}

static DWORD WINAPI sony_reader(LPVOID arg)
{
    HidDev* d = (HidDev*)arg;
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
    hid_release(d);
    return 0;
}

/* ---- Nintendo ---------------------------------------------------------------
 * Switch Pro Controller and Joy-Con (and the pads that present themselves as a
 * Pro Controller). Over USB the pad stays silent until a handshake (output
 * report 0x80, which a Bluetooth pad ignores: that is how the two are told
 * apart); over Bluetooth it starts in a simple report sent only on change.
 * Subcommands (output report 0x01) then ask for the full report (0x30, every
 * 8-15 ms), turn the motors on, light the first player LED and read the
 * sticks' calibration from the pad's flash. Layout of report 0x30: buttons in
 * bytes 3 (right side), 4 (shared) and 5 (left side), the sticks as two 12-bit
 * values each in bytes 6-8 and 9-11. A left and a right Joy-Con make one pad;
 * one alone is held sideways. Names are by position: B (bottom) is south, A
 * (right) east, Y (left) west, X (top) north; ZL/ZR are L2/R2. */
static int nin_pid(USHORT pid)
{
    switch (pid) {
    case 0x2009: return NIN_PRO;
    case 0x2006: return NIN_JOY_L;
    case 0x2007: return NIN_JOY_R;
    case 0x200E: return NIN_GRIP;     /* Joy-Con in the charging grip, over USB */
    default: return NIN_NONE;
    }
}

/* Amplitude 0..255 in the Switch's HD rumble code, 0..100 (a log scale). */
static int nin_amp(uint8_t v)
{
    const float a = v / 255.0f;
    float e;
    if (v == 0) return 0;
    if (a > 0.23f)      e = log2f(a * 8.7f) * 32.0f;
    else if (a > 0.12f) e = log2f(a * 17.0f) * 16.0f;
    else                e = a / 0.12f * 16.0f;
    int r = (int)(e + 0.5f);
    return r < 0 ? 0 : r > 100 ? 100 : r;
}

/* Both actuators' 8 bytes: `large` on the low band (160 Hz), `weak` on the
 * high band (320 Hz). All zero is the neutral 00 01 40 40. */
static void nin_rumble_bytes(uint8_t large, uint8_t weak, uint8_t* o)
{
    const int lo = nin_amp(large), hi = nin_amp(weak);
    for (int s = 0; s < 2; s++) {
        o[s * 4 + 0] = 0x00;                       /* 320 Hz: 0x100, low byte */
        o[s * 4 + 1] = (uint8_t)(hi * 2 + 0x01);   /* high amplitude + the frequency's bit 8 */
        o[s * 4 + 2] = 0x40;                       /* 160 Hz */
        o[s * 4 + 3] = (uint8_t)(lo / 2 + 0x40);   /* low amplitude */
    }
}

/* An output report, padded to the device's length (64 on USB, 49 on Bluetooth). */
static void nin_send(HidDev* d, const uint8_t* p, int n)
{
    uint8_t b[128]; memset(b, 0, sizeof b);
    memcpy(b, p, (size_t)n);
    DWORD len = (DWORD)n;
    if (d->out_len >= len && d->out_len <= sizeof b) len = d->out_len;
    hid_write(d, b, len);
}

/* Send `p` and wait for the report that answers it: id `rid`, with `tag` at
 * byte `at`. Copied to `reply` (64 bytes) when given. */
static int nin_ask(HidDev* d, const uint8_t* p, int n, uint8_t rid, int at, uint8_t tag, uint8_t* reply, int tries)
{
    uint8_t buf[1024];
    for (int t = 0; t < tries; t++) {
        nin_send(d, p, n);
        const ULONGLONG until = GetTickCount64() + 150;
        while (GetTickCount64() < until) {
            const int got = hid_read(d, buf, sizeof buf, 150);
            if (got < 0) return -1;
            if (got > at && buf[0] == rid && buf[at] == tag) {
                if (reply) memcpy(reply, buf, 64);
                return 1;
            }
        }
    }
    return 0;
}

static int nin_usb(HidDev* d, uint8_t cmd, int tries)
{
    const uint8_t p[2] = { 0x80, cmd };
    return nin_ask(d, p, 2, 0x81, 1, cmd, NULL, tries);
}

/* Subcommand `id`; its reply (report 0x21) in `reply`. */
static int nin_sub(HidDev* d, uint8_t id, const uint8_t* arg, int n, uint8_t* reply)
{
    uint8_t p[64]; memset(p, 0, sizeof p);
    p[0] = 0x01; p[1] = d->seq; d->seq = (uint8_t)((d->seq + 1) & 0x0F);
    nin_rumble_bytes(0, 0, p + 2);
    p[10] = id;
    if (n) memcpy(p + 11, arg, (size_t)n);
    return nin_ask(d, p, 11 + n, 0x21, 14, id, reply, 3);
}

/* The sticks' calibration: the user's (from System Settings) when the pad
 * has one, else the factory's; a stick with neither keeps the defaults. */
static void nin_calibrate(HidDev* d)
{
    for (int s = 0; s < 2; s++) {
        d->cal[s][0] = d->cal[s][1] = 2048;
        for (int k = 2; k < 6; k++) d->cal[s][k] = 1400;
    }
    uint8_t user[64], fact[64];
    const uint8_t a_user[5] = { 0x10, 0x80, 0, 0, 22 }, a_fact[5] = { 0x3D, 0x60, 0, 0, 18 };
    const int have_user = nin_sub(d, 0x10, a_user, 5, user) == 1;
    const int have_fact = nin_sub(d, 0x10, a_fact, 5, fact) == 1;
    for (int s = 0; s < 2; s++) {
        const uint8_t* q = NULL;
        if (have_user && user[20 + s * 11] == 0xB2 && user[21 + s * 11] == 0xA1) q = user + 22 + s * 11;
        else if (have_fact) q = fact + 20 + s * 9;
        if (!q) continue;
        int v[6];
        for (int k = 0; k < 3; k++) {
            v[k * 2]     = ((q[k * 3 + 1] << 8) & 0xF00) | q[k * 3];
            v[k * 2 + 1] = (q[k * 3 + 2] << 4) | (q[k * 3 + 1] >> 4);
        }
        /* left: above, centre, below; right: centre, below, above */
        const int* above = s ? v + 4 : v, * centre = s ? v : v + 2, * below = s ? v + 2 : v + 4;
        if (centre[0] < 500 || centre[0] > 3600 || centre[1] < 500 || centre[1] > 3600) continue;
        int ok = 1;
        for (int k = 0; k < 2; k++) ok = ok && above[k] > 300 && below[k] > 300;
        if (!ok) continue;
        d->cal[s][0] = centre[0]; d->cal[s][1] = centre[1];
        d->cal[s][2] = below[0];  d->cal[s][3] = below[1];
        d->cal[s][4] = above[0];  d->cal[s][5] = above[1];
    }
}

/* A raw 12-bit axis as -1..1 (up and right positive), with a dead zone. */
static float nin_axis(const HidDev* d, int s, int k, int raw)
{
    const int c = d->cal[s][k];
    float n = raw >= c ? (float)(raw - c) / (float)d->cal[s][4 + k] : (float)(raw - c) / (float)d->cal[s][2 + k];
    const float dz = 0.12f;
    if (n > dz) n = (n - dz) / (1 - dz); else if (n < -dz) n = (n + dz) / (1 - dz); else n = 0;
    return n < -1 ? -1 : n > 1 ? 1 : n;
}
static uint8_t nin_u8(float x)   /* -1..1 to 0..255, centre 128 */
{
    const int r = (int)(x * 127.0f) + 128;
    return (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
}

/* Report 0x30 (or 0x21, which starts the same way) into the pad alone and,
 * for a Joy-Con, its half of a pair. */
static void nin_parse_full(HidDev* d, const uint8_t* r, PadPhys* alone, PadPhys* half)
{
    const uint8_t rb = r[3], sh = r[4], lb = r[5];
    float sx[2], sy[2];
    for (int s = 0; s < 2; s++) {
        const uint8_t* q = r + 6 + s * 3;
        sx[s] = nin_axis(d, s, 0, q[0] | ((q[1] & 0x0F) << 8));
        sy[s] = nin_axis(d, s, 1, (q[1] >> 4) | (q[2] << 4));
    }
    uint32_t b = 0;   /* upright: the Pro Controller, and a Joy-Con's half */
    if (rb & 0x01) b |= 1u << PB_WEST;    /* Y */
    if (rb & 0x02) b |= 1u << PB_NORTH;   /* X */
    if (rb & 0x04) b |= 1u << PB_SOUTH;   /* B */
    if (rb & 0x08) b |= 1u << PB_EAST;    /* A */
    if (rb & 0x40) b |= 1u << PB_R1;
    if (rb & 0x80) b |= 1u << PB_R2;      /* ZR */
    if (sh & 0x01) b |= 1u << PB_SELECT;  /* - */
    if (sh & 0x02) b |= 1u << PB_START;   /* + */
    if (sh & 0x04) b |= 1u << PB_R3;
    if (sh & 0x08) b |= 1u << PB_L3;
    if (sh & 0x10) b |= 1u << PB_HOME;
    if (sh & 0x20) b |= 1u << PB_TOUCH;   /* Capture */
    if (lb & 0x01) b |= 1u << PB_DOWN;
    if (lb & 0x02) b |= 1u << PB_UP;
    if (lb & 0x04) b |= 1u << PB_RIGHT;
    if (lb & 0x08) b |= 1u << PB_LEFT;
    if (lb & 0x40) b |= 1u << PB_L1;
    if (lb & 0x80) b |= 1u << PB_L2;      /* ZL */
    memset(half, 0, sizeof *half);
    half->connected = 1; half->kind = PAD_KIND_SWITCH; half->buttons = b;
    half->lx = half->ly = half->rx = half->ry = 128;
    if (d->nin != NIN_JOY_R) { half->lx = nin_u8(sx[0]); half->ly = nin_u8(-sy[0]); }
    if (d->nin != NIN_JOY_L) { half->rx = nin_u8(sx[1]); half->ry = nin_u8(-sy[1]); }
    half->l2 = (b & (1u << PB_L2)) ? 255 : 0;
    half->r2 = (b & (1u << PB_R2)) ? 255 : 0;
    *alone = *half;
    if (d->nin != NIN_JOY_L && d->nin != NIN_JOY_R) return;

    /* Sideways, rails up: SL and SR are the shoulders, the stick is on the
     * left and the four buttons turn a quarter (the left one anticlockwise,
     * the right one clockwise). Its L/R and ZL/ZR serve as L2/R2. */
    uint32_t s = 0;
    float nx, ny;
    if (d->nin == NIN_JOY_L) {
        if (lb & 0x08) s |= 1u << PB_SOUTH;   /* left  -> bottom */
        if (lb & 0x01) s |= 1u << PB_EAST;    /* down  -> right */
        if (lb & 0x02) s |= 1u << PB_WEST;    /* up    -> left */
        if (lb & 0x04) s |= 1u << PB_NORTH;   /* right -> top */
        if (lb & 0x20) s |= 1u << PB_L1;      /* SL */
        if (lb & 0x10) s |= 1u << PB_R1;      /* SR */
        if (lb & 0x40) s |= 1u << PB_L2;
        if (lb & 0x80) s |= 1u << PB_R2;
        if (sh & 0x01) s |= 1u << PB_START;
        if (sh & 0x20) s |= 1u << PB_SELECT;
        if (sh & 0x08) s |= 1u << PB_L3;
        nx = -sy[0]; ny = sx[0];
    } else {
        if (rb & 0x08) s |= 1u << PB_SOUTH;   /* A -> bottom */
        if (rb & 0x02) s |= 1u << PB_EAST;    /* X -> right */
        if (rb & 0x04) s |= 1u << PB_WEST;    /* B -> left */
        if (rb & 0x01) s |= 1u << PB_NORTH;   /* Y -> top */
        if (rb & 0x20) s |= 1u << PB_L1;      /* SL */
        if (rb & 0x10) s |= 1u << PB_R1;      /* SR */
        if (rb & 0x40) s |= 1u << PB_L2;
        if (rb & 0x80) s |= 1u << PB_R2;
        if (sh & 0x02) s |= 1u << PB_START;
        if (sh & 0x10) s |= 1u << PB_SELECT;  /* Home: the only other button */
        if (sh & 0x04) s |= 1u << PB_L3;
        nx = sy[1]; ny = -sx[1];
    }
    alone->buttons = s;
    alone->lx = nin_u8(nx); alone->ly = nin_u8(-ny); alone->rx = alone->ry = 128;
    alone->l2 = (s & (1u << PB_L2)) ? 255 : 0;
    alone->r2 = (s & (1u << PB_R2)) ? 255 : 0;
}

/* Report 0x3F, the Pro Controller's simple Bluetooth report: only if the
 * full one could not be had. */
static int nin_parse_simple(const uint8_t* r, int n, PadPhys* p)
{
    if (n < 12) return 0;
    uint32_t b = 0;
    sony_hat(r[3] & 0x0F, &b);
    if (r[1] & 0x01) b |= 1u << PB_SOUTH;   /* B */
    if (r[1] & 0x02) b |= 1u << PB_EAST;    /* A */
    if (r[1] & 0x04) b |= 1u << PB_WEST;    /* Y */
    if (r[1] & 0x08) b |= 1u << PB_NORTH;   /* X */
    if (r[1] & 0x10) b |= 1u << PB_L1;
    if (r[1] & 0x20) b |= 1u << PB_R1;
    if (r[1] & 0x40) b |= 1u << PB_L2;
    if (r[1] & 0x80) b |= 1u << PB_R2;
    if (r[2] & 0x01) b |= 1u << PB_SELECT;
    if (r[2] & 0x02) b |= 1u << PB_START;
    if (r[2] & 0x04) b |= 1u << PB_L3;
    if (r[2] & 0x08) b |= 1u << PB_R3;
    if (r[2] & 0x10) b |= 1u << PB_HOME;
    if (r[2] & 0x20) b |= 1u << PB_TOUCH;
    memset(p, 0, sizeof *p);
    p->connected = 1; p->kind = PAD_KIND_SWITCH; p->buttons = b;
    p->lx = r[5]; p->ly = r[7]; p->rx = r[9]; p->ry = r[11];   /* high bytes of 16-bit axes, up = 0 */
    p->l2 = (b & (1u << PB_L2)) ? 255 : 0;
    p->r2 = (b & (1u << PB_R2)) ? 255 : 0;
    return 1;
}

static DWORD WINAPI nin_reader(LPVOID arg)
{
    HidDev* d = (HidDev*)arg;
    uint8_t buf[1024], reply[64];
    /* USB: status, handshake, 3 Mbit, handshake, then HID only (no time out).
     * Some third-party pads skip the middle two. */
    d->bt = 1;
    if (nin_usb(d, 0x01, 2) == 1) {
        d->bt = 0;
        nin_usb(d, 0x02, 2); nin_usb(d, 0x03, 1); nin_usb(d, 0x02, 1);
        const uint8_t force[2] = { 0x80, 0x04 };
        nin_send(d, force, 2);
    }
    if (d->nin == NIN_GRIP && nin_sub(d, 0x02, NULL, 0, reply) == 1)   /* device info: 1 left, 2 right, 3 Pro */
        d->nin = reply[17] == 1 ? NIN_JOY_L : reply[17] == 2 ? NIN_JOY_R : NIN_PRO;
    if (d->nin == NIN_GRIP) d->nin = NIN_PRO;
    nin_calibrate(d);
    const uint8_t full = 0x30, on = 0x01, led = 0x01;
    nin_sub(d, 0x03, &full, 1, NULL);   /* input report mode: full */
    nin_sub(d, 0x48, &on, 1, NULL);     /* vibration on */
    nin_sub(d, 0x30, &led, 1, NULL);    /* player LED 1 */
    fprintf(stderr, "[pad] Nintendo %s over %s\n",
            d->nin == NIN_JOY_L ? "Joy-Con (L)" : d->nin == NIN_JOY_R ? "Joy-Con (R)" : "Pro Controller",
            d->bt ? "Bluetooth" : "USB");

    ULONGLONG sent_at = 0;
    for (;;) {
        const int got = hid_read(d, buf, sizeof buf, 40);
        if (got < 0) break;
        PadPhys alone, half;
        int parsed = 0;
        if (got >= 12 && (buf[0] == 0x30 || buf[0] == 0x21)) { nin_parse_full(d, buf, &alone, &half); parsed = 1; }
        else if (buf[0] == 0x3F && d->nin == NIN_PRO && nin_parse_simple(buf, got, &alone)) { half = alone; parsed = 1; }
        if (parsed) {
            AcquireSRWLockExclusive(&d->lock); d->st = alone; d->half = half; ReleaseSRWLockExclusive(&d->lock);
        }
        /* The motors: a change at most every 30 ms, and a steady one again
         * every 40 ms (the pad stops on its own otherwise). */
        const int want = (int)d->want;
        const ULONGLONG now = GetTickCount64();
        if ((want != d->rumble && now - sent_at >= 30) || (want && now - sent_at >= 40)) {
            uint8_t p[10];
            p[0] = 0x10; p[1] = d->seq; d->seq = (uint8_t)((d->seq + 1) & 0x0F);
            nin_rumble_bytes((uint8_t)(want >> 8), (uint8_t)want, p + 2);
            nin_send(d, p, 10);
            d->rumble = want; sent_at = now;
        }
    }
    hid_release(d);
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

static void hid_scan(void)
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
        for (int k = 0; k < HID_MAX; k++)
            if (s_hid[k].used && !_stricmp(s_hid[k].path, det->DevicePath)) open_already = 1;
        if (open_already) continue;
        HANDLE h = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES at; at.Size = sizeof at;
        int kind = 0, nin = NIN_NONE;
        if (HidD_GetAttributes(h, &at)) {
            if (at.VendorID == 0x054C) kind = sony_pid_kind(at.ProductID);
            else if (at.VendorID == 0x057E && (nin = nin_pid(at.ProductID)) != NIN_NONE) kind = PAD_KIND_SWITCH;
        }
        USHORT in_len = 0, out_len = 0;
        if (kind) {
            PHIDP_PREPARSED_DATA pp = NULL; HIDP_CAPS caps;
            if (HidD_GetPreparsedData(h, &pp)) {
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                    in_len = caps.InputReportByteLength;
                    out_len = caps.OutputReportByteLength;
                    /* the gamepad collection only (usage page 1, usage 5;
                     * Nintendo's pads call themselves joysticks, usage 4) */
                    if (!(caps.UsagePage == 1 && (caps.Usage == 5 || (nin && caps.Usage == 4)))) kind = 0;
                }
                HidD_FreePreparsedData(pp);
            }
        }
        int slot = -1;
        if (kind) for (int k = 0; k < HID_MAX; k++)
            if (InterlockedCompareExchange(&s_hid[k].used, 1, 0) == 0) { slot = k; break; }
        if (slot < 0) { CloseHandle(h); continue; }
        HidDev* d = &s_hid[slot];
        d->h = h; d->ds5 = (kind == PAD_KIND_DUALSENSE); d->nin = nin; d->in_len = in_len;
        d->out_len = out_len; d->bt = in_len != 64; d->rumble = d->want = 0; d->seq = 0;
        if (!nin) d->rumble = -1;
        strncpy(d->path, det->DevicePath, sizeof d->path - 1);
        HANDLE th = CreateThread(NULL, 0, nin ? nin_reader : sony_reader, d, 0, NULL);
        if (th) CloseHandle(th);
        else { CloseHandle(h); d->h = NULL; d->path[0] = 0; InterlockedExchange(&d->used, 0); }
    }
    SetupDiDestroyDeviceInfoList(set);
}

static DWORD WINAPI hid_manager(LPVOID arg)
{
    (void)arg;
    for (;;) { hid_scan(); Sleep(2000); }
    return 0;
}

static void hid_start(void)
{
    if (InterlockedCompareExchange(&s_hid_started, 1, 0) != 0) return;
    for (int k = 0; k < HID_MAX; k++) InitializeSRWLock(&s_hid[k].lock);
    if (getenv("PAD_NO_HID")) return;
    HANDLE th = CreateThread(NULL, 0, hid_manager, NULL, 0, NULL);
    if (th) CloseHandle(th);
}

/* The HID pads in pad_phys_poll's order (after the XInput ones): `a` and,
 * for a pair of Joy-Con, `b` (else -1) are s_hid slots. The first left and
 * the first right Joy-Con connected make one pad, in the left one's place.
 * Returns how many there are. */
static int hid_pads(int a[HID_MAX], int b[HID_MAX], PadPhys out[HID_MAX])
{
    PadPhys st[HID_MAX], half[HID_MAX];
    int jl = -1, jr = -1;
    for (int k = 0; k < HID_MAX; k++) {
        memset(&st[k], 0, sizeof st[k]);
        if (!s_hid[k].used) continue;
        AcquireSRWLockShared(&s_hid[k].lock); st[k] = s_hid[k].st; half[k] = s_hid[k].half; ReleaseSRWLockShared(&s_hid[k].lock);
        if (!st[k].connected) continue;
        if (s_hid[k].nin == NIN_JOY_L && jl < 0) jl = k;
        if (s_hid[k].nin == NIN_JOY_R && jr < 0) jr = k;
    }
    const int pair = jl >= 0 && jr >= 0;
    int n = 0;
    for (int k = 0; k < HID_MAX; k++) {
        if (!st[k].connected || (pair && k == jr)) continue;
        a[n] = k; b[n] = -1; out[n] = st[k];
        if (pair && k == jl) {
            PadPhys p = half[jl];
            const PadPhys* r = &half[jr];
            p.buttons |= r->buttons; p.rx = r->rx; p.ry = r->ry; p.r2 = r->r2;
            b[n] = jr; out[n] = p;
        }
        n++;
    }
    return n;
}

/* ---- Rumble ----------------------------------------------------------------
 * Sony controllers take it in an HID output report: DualShock 4 report 0x05
 * (USB) / 0x11 (Bluetooth), DualSense 0x02 (USB) / 0x31 (Bluetooth, in its
 * "compatible vibration" mode). Bluetooth reports end in a CRC-32 of 0xA2 plus
 * the report. Only the motor bits are flagged valid, so the light bar and the
 * rest keep their state. Nintendo pads: their reader thread sends it. */
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

static void sony_rumble(HidDev* d, uint8_t large, uint8_t weak)
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
    hid_write(d, b, len);
}

static void hid_rumble(int k, uint8_t large, uint8_t weak)
{
    HidDev* d = &s_hid[k];
    const int want = large << 8 | weak;
    if (d->nin) { InterlockedExchange(&d->want, want); return; }
    if (want != d->rumble && d->h) {
        d->rumble = want;
        sony_rumble(d, large, weak);
    }
}

/* PAD_RUMBLE_SCALE, a percentage: 0 turns the motors off, above 100 makes
 * weak rumble stronger (a motor tops out at 255). */
static uint8_t rumble_scaled(uint8_t v)
{
    const char* e = getenv("PAD_RUMBLE_SCALE");
    if (!v || !e || !*e) return v;
    const int pct = atoi(e), r = (v * (pct < 0 ? 0 : pct) + 50) / 100;
    return (uint8_t)(r > 255 ? 255 : r);
}

void pad_phys_rumble(int index, uint8_t large, uint8_t weak)
{
    hid_start();
    large = rumble_scaled(large); weak = rumble_scaled(weak);
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
    int a[HID_MAX], b[HID_MAX]; PadPhys p[HID_MAX];
    const int count = hid_pads(a, b, p);
    if (index - n < 0 || index - n >= count) return;
    hid_rumble(a[index - n], large, weak);
    if (b[index - n] >= 0) hid_rumble(b[index - n], large, weak);
}

void pad_phys_rumble_off(void)
{
    for (int i = 0; i < PAD_PHYS_MAX; i++) pad_phys_rumble(i, 0, 0);
}

int pad_phys_poll(int index, PadPhys* out)
{
    memset(out, 0, sizeof *out);
    hid_start();
    int n = 0;
    for (int i = 0; i < 4; i++) {
        PadPhys p; memset(&p, 0, sizeof p);
        if (xi_read(i, &p)) { if (n == index) { apply_360_layout(&p); *out = p; return 1; } n++; }
    }
    int a[HID_MAX], b[HID_MAX]; PadPhys p[HID_MAX];
    const int count = hid_pads(a, b, p);
    if (index - n < 0 || index - n >= count) return 0;
    *out = p[index - n];
    apply_360_layout(out);
    return 1;
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
int pad_phys_360_layout(int kind) { (void)kind; return 0; }
#endif
