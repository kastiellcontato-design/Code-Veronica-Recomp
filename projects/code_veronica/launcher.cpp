/*
 * Code Veronica launcher: the window shown before the game starts.
 *
 * Three pages -- Inicio (display mode, render resolution), Jogo (game folder,
 * licence .rap, firmware) and Controles (keyboard and controller mapping) --
 * drawn with GDI+ in the Resident Evil palette. Every change is written to
 * cv_launcher.ini beside the executable at once; "Jogar" turns the settings
 * into the environment the runtime reads (RES_SCALE, WINDOW_MODE, PS3_VFS_ROOT,
 * PS3_RAP_DIR, PS3_DEV_FLASH, PAD_BTNMAP, PAD_KEYMAP) and hands back the
 * EBOOT.elf path. CV_NO_LAUNCHER=1 skips it.
 *
 * Images come from RCDATA resources (launcher_art/), optional: a build
 * without them draws plain panels.
 */
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "pad_phys.h"
#include "pad_map.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;
using namespace Gdiplus;

/* runtime hooks for the live options menu (rsx_d3d12_backend.c, cellPad.c) */
extern "C" {
void  rsx_d3d12_set_window_mode(int mode);
void  rsx_d3d12_set_res_scale(float f);
void  rsx_d3d12_set_aa(int mode);
void  rsx_d3d12_close(void);
void* rsx_d3d12_window(void);
extern void (*g_rsx_menu_hook)(void);
extern int g_pad_block;
void  cellpad_set_map(const int* btn, const int* key);
}
namespace {

/* ---------------------------------------------------------------- palette */
const Color C_BG(255, 21, 17, 17), C_SIDE(255, 13, 11, 11), C_PANEL(255, 30, 25, 25),
            C_LINE(255, 70, 28, 28), C_TEXT(255, 225, 218, 210), C_DIM(255, 150, 140, 132),
            C_RED(255, 196, 28, 34), C_RED_HI(255, 230, 50, 50), C_HOVER(255, 38, 30, 30),
            C_FIELD(255, 26, 22, 22), C_FIELD_BD(255, 70, 60, 58), C_OK(255, 120, 190, 110);

const wchar_t* GAME_CODE = L"NPEB00553";
const int W = 1120, H = 770, SIDE = 230;

/* ---------------------------------------------------------------- settings */
struct Settings {
    int mode = 0;          /* 0 janela, 1 sem borda, 2 tela cheia */
    int res = 1;           /* index into k_res */
    int aa = 0;            /* anti-aliasing: 0 off, 1 FXAA, 2 SMAA */
    int layout = 0;        /* 0 PlayStation, 1 Xbox */
    std::wstring game, rap, fw;
    int key[PA_COUNT];
    int btn[PA_COUNT];
};
struct ResOpt { const wchar_t* name; const char* scale; };
const ResOpt k_res[] = {
    { L"480p (854x480)", "0.6667" }, { L"Nativa (720p)", "1" }, { L"1080p (1920x1080)", "1.5" },
    { L"1440p (2560x1440)", "2" }, { L"4K (3840x2160)", "3" } };
const wchar_t* k_modes[] = { L"Em janela", L"Janela sem borda", L"Tela cheia" };
const char* k_mode_env[] = { "window", "borderless", "fullscreen" };
const wchar_t* k_aa[] = { L"Desligado", L"FXAA (leve)", L"SMAA (melhor)" };

/* Actions shown on the Controles page. */
const int k_rows[] = { PA_CROSS, PA_CIRCLE, PA_SQUARE, PA_TRIANGLE, PA_L1, PA_R1, PA_L2, PA_R2,
                       PA_L3, PA_R3, PA_UP, PA_DOWN, PA_LEFT, PA_RIGHT, PA_SELECT, PA_START };
const int NROWS = sizeof k_rows / sizeof k_rows[0];
const wchar_t* k_action_name[PA_COUNT] = {
    L"Cross", L"Circle", L"Square", L"Triangle", L"L1", L"R1", L"L2", L"R2", L"L3", L"R3",
    L"Select", L"Start", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita" };
/* Physical buttons, by layout. */
const wchar_t* k_pb_name[2][PB_COUNT] = {
    { L"Cross", L"Circle", L"Square", L"Triangle", L"L1", L"R1", L"L2", L"R2", L"L3", L"R3",
      L"Select", L"Start", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita", L"PS", L"Touchpad" },
    { L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT", L"RT", L"LS", L"RS",
      L"Select", L"Start", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita", L"Guide", L"-" } };
std::wstring ini_path()
{
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    return (fs::path(exe).parent_path() / L"cv_launcher.ini").wstring();
}
std::wstring ini_get(const wchar_t* sec, const wchar_t* key, const std::wstring& def)
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(sec, key, def.c_str(), buf, 1024, ini_path().c_str());
    return buf[0] ? buf : def;   /* an empty saved value means "not chosen yet" */
}
void ini_set(const wchar_t* sec, const wchar_t* key, const std::wstring& v)
{
    WritePrivateProfileStringW(sec, key, v.c_str(), ini_path().c_str());
}
std::wstring widen(const char* s) { return std::wstring(s, s + strlen(s)); }
std::wstring env_w(const char* n) { const char* e = getenv(n); return e ? fs::path(e).wstring() : L""; }

std::wstring find_rap(const std::wstring& dir)
{
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return L"";
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::wstring n = e.path().filename().wstring();
        if (n.find(GAME_CODE) != std::wstring::npos && e.path().extension() == L".rap")
            return e.path().wstring();
    }
    return L"";
}

Settings load_settings()
{
    Settings s;
    s.mode   = _wtoi(ini_get(L"tela", L"modo", L"0").c_str());
    s.res    = _wtoi(ini_get(L"tela", L"resolucao", L"1").c_str());
    s.aa     = _wtoi(ini_get(L"tela", L"antialias", L"0").c_str());
    s.layout = _wtoi(ini_get(L"controles", L"layout", L"0").c_str());
    if (s.mode < 0 || s.mode > 2) s.mode = 0;
    if (s.res < 0 || s.res > 4) s.res = 1;
    if (s.aa < 0 || s.aa > 2) s.aa = 0;
    if (s.layout < 0 || s.layout > 1) s.layout = 0;
    /* First run: take the paths the old launcher script set, if any. */
    std::wstring def_game = env_w("PS3_VFS_ROOT");
    if (def_game.empty()) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
        std::error_code ec;
        for (fs::path d = fs::path(exe).parent_path(); !d.empty() && d != d.root_path(); d = d.parent_path()) {
            fs::path g = d / L"JOGO" / GAME_CODE;   /* this checkout keeps the game in <root>/JOGO */
            if (fs::exists(g / L"USRDIR" / L"EBOOT.elf", ec)) { def_game = g.wstring(); break; }
        }
    }
    s.game = ini_get(L"pastas", L"jogo", def_game);
    s.rap  = ini_get(L"pastas", L"rap", find_rap(env_w("PS3_RAP_DIR")));
    s.fw   = ini_get(L"pastas", L"firmware", env_w("PS3_DEV_FLASH"));
    for (int a = 0; a < PA_COUNT; a++) {
        std::wstring kid = L"tecla_" + widen(k_pad_action_id[a]);
        std::wstring bid = L"botao_" + widen(k_pad_action_id[a]);
        s.key[a] = _wtoi(ini_get(L"controles", kid.c_str(), std::to_wstring(k_pad_default_key[a])).c_str());
        std::wstring b = ini_get(L"controles", bid.c_str(), widen(pad_phys_id(k_pad_default_btn[a])));
        std::string nb(b.begin(), b.end());
        int pb = pad_phys_from_id(nb.c_str());
        s.btn[a] = pb >= 0 ? pb : k_pad_default_btn[a];
    }
    return s;
}

void save_settings(const Settings& s)
{
    ini_set(L"tela", L"modo", std::to_wstring(s.mode));
    ini_set(L"tela", L"resolucao", std::to_wstring(s.res));
    ini_set(L"tela", L"antialias", std::to_wstring(s.aa));
    ini_set(L"controles", L"layout", std::to_wstring(s.layout));
    ini_set(L"pastas", L"jogo", s.game);
    ini_set(L"pastas", L"rap", s.rap);
    ini_set(L"pastas", L"firmware", s.fw);
    for (int a = 0; a < PA_COUNT; a++) {
        ini_set(L"controles", (L"tecla_" + widen(k_pad_action_id[a])).c_str(), std::to_wstring(s.key[a]));
        ini_set(L"controles", (L"botao_" + widen(k_pad_action_id[a])).c_str(), widen(pad_phys_id(s.btn[a])));
    }
}

/* Validation of the three paths. */
std::wstring check_game(const std::wstring& g, bool* ok)
{
    std::error_code ec; *ok = false;
    if (g.empty()) return L"Escolha a pasta do jogo (" + std::wstring(GAME_CODE) + L").";
    if (fs::exists(fs::path(g) / L"USRDIR" / L"EBOOT.elf", ec)) { *ok = true; return L"Jogo encontrado."; }
    if (fs::exists(fs::path(g) / L"USRDIR" / L"EBOOT.BIN", ec))
        return L"Falta o EBOOT.elf descriptografado em USRDIR (gere pelo RPCS3).";
    return L"Esta pasta não tem USRDIR/EBOOT.elf.";
}
std::wstring check_rap(const std::wstring& r, bool* ok)
{
    std::error_code ec; *ok = !r.empty() && fs::is_regular_file(r, ec);
    return *ok ? L"Licença encontrada." : L"Escolha o arquivo .rap do jogo.";
}
std::wstring check_fw(const std::wstring& f, bool* ok)
{
    std::error_code ec;
    *ok = !f.empty() && fs::exists(fs::path(f) / L"sys" / L"external" / L"flashATRAC.pic", ec);
    return *ok ? L"Firmware encontrado." : L"Escolha a pasta dev_flash (sem ela o jogo fica sem som).";
}

/* ---------------------------------------------------------------- keys */
std::wstring key_name(int vk)
{
    switch (vk) {
    case 0: return L"-";
    case VK_SPACE: return L"Espaço"; case VK_RETURN: return L"Enter"; case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab"; case VK_ESCAPE: return L"Esc";
    case VK_LSHIFT: return L"Shift esquerdo"; case VK_RSHIFT: return L"Shift direito";
    case VK_LCONTROL: return L"Ctrl esquerdo"; case VK_RCONTROL: return L"Ctrl direito";
    case VK_LMENU: return L"Alt esquerdo"; case VK_RMENU: return L"Alt direito";
    case VK_UP: return L"Seta cima"; case VK_DOWN: return L"Seta baixo";
    case VK_LEFT: return L"Seta esquerda"; case VK_RIGHT: return L"Seta direita";
    case VK_INSERT: return L"Insert"; case VK_DELETE: return L"Delete"; case VK_HOME: return L"Home";
    case VK_END: return L"End"; case VK_PRIOR: return L"Page Up"; case VK_NEXT: return L"Page Down";
    case VK_CAPITAL: return L"Caps Lock";
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_F1 && vk <= VK_F12) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"Num " + std::to_wstring(vk - VK_NUMPAD0);
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t buf[64];
    if (sc && GetKeyNameTextW((LONG)(sc << 16), buf, 64) > 0) return buf;
    return L"Tecla " + std::to_wstring(vk);
}

/* ---------------------------------------------------------------- images */
Bitmap* load_png(const char* name)
{
    HRSRC r = FindResourceA(NULL, name, MAKEINTRESOURCEA(10) /* RT_RCDATA */);
    if (!r) return nullptr;
    HGLOBAL h = LoadResource(NULL, r);
    DWORD sz = SizeofResource(NULL, r);
    void* p = LockResource(h);
    if (!p || !sz) return nullptr;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sz);
    memcpy(GlobalLock(mem), p, sz); GlobalUnlock(mem);
    IStream* st = nullptr;
    if (CreateStreamOnHGlobal(mem, TRUE, &st) != S_OK) { GlobalFree(mem); return nullptr; }
    Bitmap* b = Bitmap::FromStream(st);
    st->Release();
    if (b && b->GetLastStatus() != Ok) { delete b; return nullptr; }
    return b;
}

/* ---------------------------------------------------------------- UI state */
enum Page { P_INICIO, P_JOGO, P_CONTROLES };
enum Capture { CAP_NONE, CAP_KEY, CAP_BTN };

struct Hit { RectF r; int id; };
struct UI {
    HWND hwnd = nullptr;
    Settings s;
    Page page = P_INICIO;
    int hover = -1;
    int open_dd = -1;            /* open dropdown id */
    Capture cap = CAP_NONE; int cap_action = -1; ULONGLONG cap_since = 0;
    uint32_t pad_prev[PAD_PHYS_MAX] = {};
    std::vector<Hit> hits;
    std::wstring play_msg;
    int result = 0;              /* 1 = play */
    Bitmap* banner = nullptr;
    std::wstring pad_status;       /* connected model ("" = none) */
    int pad_count = 0;
    int pad_kind = 0;              /* PAD_KIND_* of the first connected pad */
    bool running = false;          /* Jogar pressed: the window stays as the options menu */
    int applied_mode = -1, applied_res = -1, applied_aa = -1;
    uint32_t combo_prev = 0;
    std::string eboot;
};
UI g;

/* ids */
enum {
    ID_NAV = 100,               /* + page */
    ID_DD_MODE = 200, ID_DD_RES = 201, ID_DD_AA = 202, ID_DD_ITEM = 300,  /* + item index */
    ID_PLAY = 400, ID_RESUME, ID_QUIT,
    ID_GAME_BROWSE = 500, ID_GAME_OPEN, ID_RAP_BROWSE, ID_RAP_OPEN, ID_FW_BROWSE, ID_FW_OPEN,
    ID_LAYOUT_PS = 600, ID_LAYOUT_XB,
    ID_KEY = 700, ID_BTN = 800, ID_RESTORE = 900,
};

void hit(float x, float y, float w, float h, int id) { g.hits.push_back({ RectF(x, y, w, h), id }); }

GraphicsPath* round_rect(RectF r, float rad)
{
    auto* p = new GraphicsPath();
    float d = rad * 2;
    p->AddArc(r.X, r.Y, d, d, 180, 90);
    p->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
}

void text(Graphics& gr, const std::wstring& t, float x, float y, const Font& f, const Color& c,
          StringAlignment h = StringAlignmentNear, float w = 0, float hh = 0)
{
    SolidBrush b(c);
    StringFormat sf; sf.SetAlignment(h); sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisPath);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    if (w > 0) gr.DrawString(t.c_str(), -1, &f, RectF(x, y, w, hh), &sf, &b);
    else { StringFormat s2; gr.DrawString(t.c_str(), -1, &f, PointF(x, y), &s2, &b); }
}

/* Controller button icons, drawn as vectors on a transparent background in
 * one style for both layouts: face buttons are a ring with the symbol or
 * letter in the controller's colour, shoulders/triggers are light shapes with
 * dark labels, sticks are rings, the d-pad is a cross with the pressed arm
 * lit. layout 0 = PlayStation, 1 = Xbox; pb is a PB_* position. */
const Color G_LINE(255, 205, 198, 190), G_FILL(255, 218, 212, 204), G_INK(255, 28, 22, 22),
            G_DIM(255, 92, 84, 80);

/* Centred on the glyphs' own outline: DrawString pads text with side bearings,
 * which pushed short labels like "R3" or "A" visibly off centre. */
void g_label(Graphics& gr, const wchar_t* t, RectF r, float px, const Color& c)
{
    FontFamily ff(L"Segoe UI");
    StringFormat sf(StringFormat::GenericTypographic());
    GraphicsPath p;
    p.AddString(t, -1, &ff, FontStyleBold, px, PointF(0, 0), &sf);
    RectF bb; p.GetBounds(&bb);
    Matrix m; m.Translate(r.X + (r.Width - bb.Width) / 2 - bb.X, r.Y + (r.Height - bb.Height) / 2 - bb.Y);
    p.Transform(&m);
    SolidBrush b(c);
    gr.FillPath(&b, &p);
}
void btn_glyph(Graphics& gr, int layout, int pb, float x, float y, float s = 30.0f)
{
    const float cx = x + s / 2, cy = y + s / 2, k = s / 26.0f;
    /* every icon sits on the same dark disc with a light ring */
    {
        const float r = 12.0f * k;
        SolidBrush disc(Color(255, 40, 34, 34)); gr.FillEllipse(&disc, cx - r, cy - r, 2 * r, 2 * r);
        Pen ring(G_LINE, 1.5f * k); gr.DrawEllipse(&ring, cx - r, cy - r, 2 * r, 2 * r);
    }
    SolidBrush fill(G_FILL);
    switch (pb) {
    case PB_SOUTH: case PB_EAST: case PB_WEST: case PB_NORTH: {
        static const Color ps[4] = { Color(255, 124, 178, 240), Color(255, 255, 102, 110),
                                     Color(255, 222, 140, 210), Color(255, 70, 220, 170) };
        static const Color xb[4] = { Color(255, 110, 200, 80), Color(255, 240, 80, 70),
                                     Color(255, 70, 150, 245), Color(255, 245, 200, 50) };
        const int i = pb - PB_SOUTH;
        if (layout == 1) {
            static const wchar_t* L[4] = { L"A", L"B", L"X", L"Y" };
            /* B's bowls sit right of its stem: centred by outline it still
             * reads left, so it gets a small optical nudge */
            const float nudge = (pb == PB_EAST) ? 0.7f * k : 0.0f;
            g_label(gr, L[i], RectF(x + nudge, y, s, s), 12.0f * k, xb[i]);
            break;
        }
        Pen p(ps[i], 1.9f * k); p.SetLineJoin(LineJoinRound); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
        const float a = 5.2f * k;
        if (pb == PB_SOUTH) { gr.DrawLine(&p, cx - a, cy - a, cx + a, cy + a); gr.DrawLine(&p, cx - a, cy + a, cx + a, cy - a); }
        else if (pb == PB_EAST) gr.DrawEllipse(&p, cx - a, cy - a, 2 * a, 2 * a);
        else if (pb == PB_WEST) gr.DrawRectangle(&p, cx - a + 0.5f * k, cy - a + 0.5f * k, 2 * a - k, 2 * a - k);
        else { PointF tri[3] = { PointF(cx, cy - a - 0.5f * k), PointF(cx + a + 0.5f * k, cy + a * 0.8f),
                                 PointF(cx - a - 0.5f * k, cy + a * 0.8f) };
               gr.DrawPolygon(&p, tri, 3); }
        break; }
    case PB_L1: case PB_R1: {        /* bumper: a low rounded bar */
        RectF r(cx - 8.5f * k, cy - 4.5f * k, 17 * k, 9 * k);
        GraphicsPath* p = round_rect(r, 4 * k); gr.FillPath(&fill, p); delete p;
        g_label(gr, layout ? (pb == PB_L1 ? L"LB" : L"RB") : (pb == PB_L1 ? L"L1" : L"R1"), r, 7.5f * k, G_INK);
        break; }
    case PB_L2: case PB_R2: {        /* trigger: tall, rounded on top */
        RectF r(cx - 6 * k, cy - 7.5f * k, 12 * k, 15 * k);
        GraphicsPath p;
        const float d = 10 * k;
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
        p.AddLine(r.X + r.Width, r.Y + r.Height - 2 * k, r.X + r.Width - 2 * k, r.Y + r.Height);
        p.AddLine(r.X + 2 * k, r.Y + r.Height, r.X, r.Y + r.Height - 2 * k);
        p.CloseFigure();
        gr.FillPath(&fill, &p);
        g_label(gr, layout ? (pb == PB_L2 ? L"LT" : L"RT") : (pb == PB_L2 ? L"L2" : L"R2"),
                RectF(r.X, r.Y + 2 * k, r.Width, r.Height - 2 * k), 7.0f * k, G_INK);
        break; }
    case PB_L3: case PB_R3: {        /* stick: an inner ring */
        const float r2 = 8.5f * k;
        Pen in(G_DIM, 1.2f * k); gr.DrawEllipse(&in, cx - r2, cy - r2, 2 * r2, 2 * r2);
        g_label(gr, layout ? (pb == PB_L3 ? L"LS" : L"RS") : (pb == PB_L3 ? L"L3" : L"R3"),
                RectF(x, y, s, s), 8.5f * k, G_LINE);
        break; }
    case PB_UP: case PB_DOWN: case PB_LEFT: case PB_RIGHT: {
        const float w = 5.6f * k, h = 8.5f * k;   /* arm width, arm length from centre */
        GraphicsPath p;
        PointF pts[12] = {
            PointF(cx - w / 2, cy - h), PointF(cx + w / 2, cy - h), PointF(cx + w / 2, cy - w / 2),
            PointF(cx + h, cy - w / 2), PointF(cx + h, cy + w / 2), PointF(cx + w / 2, cy + w / 2),
            PointF(cx + w / 2, cy + h), PointF(cx - w / 2, cy + h), PointF(cx - w / 2, cy + w / 2),
            PointF(cx - h, cy + w / 2), PointF(cx - h, cy - w / 2), PointF(cx - w / 2, cy - w / 2) };
        p.AddPolygon(pts, 12);
        SolidBrush body(Color(255, 74, 66, 66)); gr.FillPath(&body, &p);
        RectF arm = pb == PB_UP    ? RectF(cx - w / 2, cy - h, w, h - w / 2)
                  : pb == PB_DOWN  ? RectF(cx - w / 2, cy + w / 2, w, h - w / 2)
                  : pb == PB_LEFT  ? RectF(cx - h, cy - w / 2, h - w / 2, w)
                  :                  RectF(cx + w / 2, cy - w / 2, h - w / 2, w);
        GraphicsPath* ap = round_rect(arm, 1.2f * k); gr.FillPath(&fill, ap); delete ap;
        break; }
    case PB_SELECT: {
        if (layout == 1) {           /* Xbox View: two overlapping windows */
            Pen p(G_LINE, 1.4f * k);
            gr.DrawRectangle(&p, cx - 5 * k, cy - 4.5f * k, 7 * k, 6 * k);
            SolidBrush bk(Color(255, 40, 34, 34)); gr.FillRectangle(&bk, cx - 2 * k, cy - 1.5f * k, 7 * k, 6 * k);
            gr.DrawRectangle(&p, cx - 2 * k, cy - 1.5f * k, 7 * k, 6 * k);
        } else {                     /* PS Create: a pill with three rays above it */
            Pen p(G_LINE, 1.5f * k); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
            GraphicsPath* pp = round_rect(RectF(cx - 3 * k, cy - 1 * k, 6 * k, 8.5f * k), 3 * k);
            gr.DrawPath(&p, pp); delete pp;
            gr.DrawLine(&p, cx, cy - 3.5f * k, cx, cy - 7 * k);
            gr.DrawLine(&p, cx - 3.8f * k, cy - 2.8f * k, cx - 6 * k, cy - 5.6f * k);
            gr.DrawLine(&p, cx + 3.8f * k, cy - 2.8f * k, cx + 6 * k, cy - 5.6f * k);
        }
        break; }
    case PB_START: {                 /* Options / Menu: three lines */
        Pen p(G_LINE, 1.8f * k); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
        for (int i = -1; i <= 1; i++) gr.DrawLine(&p, cx - 5 * k, cy + i * 3.6f * k, cx + 5 * k, cy + i * 3.6f * k);
        break; }
    default:                         /* home / touchpad */
        g_label(gr, pb == PB_HOME ? (layout ? L"\u2302" : L"PS") : L"T", RectF(x, y, s, s), 9.0f * k, G_LINE);
        break;
    }
}
void field(Graphics& gr, RectF r, bool hot, bool active)
{
    SolidBrush bg(active ? Color(255, 48, 20, 20) : (hot ? Color(255, 36, 30, 30) : C_FIELD));
    gr.FillRectangle(&bg, r);
    Pen pen(active ? C_RED : C_FIELD_BD, 1);
    gr.DrawRectangle(&pen, r.X, r.Y, r.Width - 1, r.Height - 1);
}

void button(Graphics& gr, RectF r, const std::wstring& label, int id, const Font& f, bool primary = false,
            bool enabled = true)
{
    bool hot = g.hover == id && enabled;
    GraphicsPath* p = round_rect(r, 3);
    SolidBrush bg(primary ? (hot ? C_RED_HI : C_RED) : (hot ? C_HOVER : C_FIELD));
    gr.FillPath(&bg, p);
    Pen pen(primary ? C_RED_HI : C_FIELD_BD, 1); gr.DrawPath(&pen, p);
    delete p;
    text(gr, label, r.X, r.Y, f, enabled ? C_TEXT : C_DIM, StringAlignmentCenter, r.Width, r.Height);
    if (enabled) hit(r.X, r.Y, r.Width, r.Height, id);
}

void dropdown(Graphics& gr, RectF r, const std::wstring& value, int id, const Font& f, bool enabled = true)
{
    bool hot = g.hover == id && enabled;
    field(gr, r, hot, id >= 0 && g.open_dd == id);
    text(gr, value, r.X + 8, r.Y, f, enabled ? C_TEXT : C_DIM, StringAlignmentNear, r.Width - 30, r.Height);
    /* arrow */
    SolidBrush ab(enabled ? C_DIM : Color(255, 80, 72, 70));
    PointF tri[3] = { PointF(r.X + r.Width - 18, r.Y + r.Height / 2 - 3),
                      PointF(r.X + r.Width - 8, r.Y + r.Height / 2 - 3),
                      PointF(r.X + r.Width - 13, r.Y + r.Height / 2 + 3) };
    gr.FillPolygon(&ab, tri, 3);
    if (enabled) hit(r.X, r.Y, r.Width, r.Height, id);
}

void section(Graphics& gr, const std::wstring& t, float x, float y, float w, const Font& f)
{
    text(gr, t, x, y, f, C_RED);
    Pen pen(C_LINE, 1);
    gr.DrawLine(&pen, x, y + 26, x + w, y + 26);
}

/* An open dropdown's list, drawn last so it sits on top. */
struct DDList { int id = -1; RectF anchor; std::vector<std::wstring> items; int sel = 0; };
DDList g_dd;

/* ---------------------------------------------------------------- pages */
void draw_sidebar(Graphics& gr, const Font& title, const Font& title2, const Font& nav)
{
    SolidBrush side(C_SIDE); gr.FillRectangle(&side, 0, 0, SIDE, H);
    text(gr, L"RESIDENT EVIL", 22, 26, title, C_TEXT);
    text(gr, L"CODE:VERONICA X", 22, 56, title2, C_RED);
    const wchar_t* items[] = { L"Início", L"Jogo", L"Controles" };
    for (int i = 0; i < 3; i++) {
        float y = 120.0f + i * 46;
        bool act = (int)g.page == i, hot = g.hover == ID_NAV + i;
        if (act || hot) { SolidBrush hb(act ? Color(255, 30, 22, 22) : C_HOVER); gr.FillRectangle(&hb, 0.0f, y, (float)SIDE, 44.0f); }
        if (act) { SolidBrush rb(C_RED); gr.FillRectangle(&rb, 0.0f, y, 3.0f, 44.0f); }
        text(gr, items[i], 24, y, nav, act ? C_RED_HI : C_TEXT, StringAlignmentNear, SIDE - 30, 44);
        hit(0, y, SIDE, 44, ID_NAV + i);
    }
}

void draw_play(Graphics& gr, const Font& big, const Font& fsm)
{
    RectF r(W - 250.0f, H - 78.0f, 220.0f, 50.0f);
    if (g.running) {
        button(gr, RectF(W - 300.0f, H - 78.0f, 270.0f, 50.0f), L"VOLTAR AO JOGO", ID_RESUME, big, true, true);
        button(gr, RectF(W - 450.0f, H - 71.0f, 135.0f, 36.0f), L"Fechar jogo", ID_QUIT, fsm);
        text(gr, L"No jogo: Insert ou R3+L2 abre este menu.",
             SIDE + 32.0f, H - 78.0f, fsm, C_DIM, StringAlignmentNear, W - SIDE - 500.0f, 50);
        return;
    }
    bool ok_game; check_game(g.s.game, &ok_game);
    button(gr, r, L"JOGAR", ID_PLAY, big, true, true);
    std::wstring msg = g.play_msg;
    if (msg.empty() && !ok_game) msg = L"Configure a pasta do jogo na aba Jogo.";
    if (!msg.empty()) text(gr, msg, SIDE + 32.0f, H - 78.0f, fsm, C_RED_HI, StringAlignmentNear, W - SIDE - 320.0f, 50);
}

void draw_inicio(Graphics& gr, const Font& head, const Font& body, const Font& code)
{
    const float cx = (float)SIDE, cw = (float)(W - SIDE), bh = 330;
    if (g.banner) {
        /* cover-crop the art into the banner box, weighted to the right */
        float iw = (float)g.banner->GetWidth(), ih = (float)g.banner->GetHeight();
        float sc = std::max(cw / iw, bh / ih);
        float sw = cw / sc, sh = bh / sc;
        float sx = (iw - sw) * 0.62f, sy = (ih - sh) * 0.25f;
        gr.DrawImage(g.banner, RectF(cx, 0, cw, bh), sx, sy, sw, sh, UnitPixel);
    } else {
        LinearGradientBrush lg(PointF(cx, 0), PointF(cx + cw, bh), Color(255, 40, 14, 14), Color(255, 18, 12, 12));
        gr.FillRectangle(&lg, cx, 0.0f, cw, bh);
    }
    /* fade the bottom of the banner into the page */
    LinearGradientBrush fade(PointF(0, bh - 120), PointF(0, bh + 1), Color(0, 21, 17, 17), C_BG);
    gr.FillRectangle(&fade, cx, bh - 120, cw, 121.0f);
    LinearGradientBrush fadeL(PointF(cx, 0), PointF(cx + 120, 0), Color(200, 13, 11, 11), Color(0, 13, 11, 11));
    gr.FillRectangle(&fadeL, cx, 0.0f, 120.0f, bh);
    text(gr, std::wstring(GAME_CODE) + L"  ·  Resident Evil Code: Veronica X (PSN)", cx + 32, bh - 46, code, C_RED_HI);

    float x = cx + 32, y = bh + 14;
    section(gr, L"AJUSTES RÁPIDOS", x, y, cw - 64, head);
    y += 44;
    text(gr, L"Modo de exibição", x, y, body, C_TEXT, StringAlignmentNear, 200, 30);
    dropdown(gr, RectF(x + 210, y, 200, 30), k_modes[g.s.mode], ID_DD_MODE, body);
    text(gr, L"Limite de quadros", x + 450, y, body, C_TEXT, StringAlignmentNear, 180, 30);
    dropdown(gr, RectF(x + 630, y, 150, 30), L"30 FPS (original)", -1, body, false);
    y += 44;
    text(gr, L"Resolução de renderização", x, y, body, C_TEXT, StringAlignmentNear, 205, 30);
    dropdown(gr, RectF(x + 210, y, 200, 30), k_res[g.s.res].name, ID_DD_RES, body);
    text(gr, L"Anti-serrilhado", x + 450, y, body, C_TEXT, StringAlignmentNear, 180, 30);
    dropdown(gr, RectF(x + 630, y, 150, 30), k_aa[g.s.aa], ID_DD_AA, body);
    y += 54;
    bool o1, o2, o3;
    std::wstring m1 = check_game(g.s.game, &o1), m2 = check_rap(g.s.rap, &o2), m3 = check_fw(g.s.fw, &o3);
    const std::wstring* ms[3] = { &m1, &m2, &m3 }; bool oks[3] = { o1, o2, o3 };
    for (int i = 0; i < 3; i++) {
        text(gr, (oks[i] ? L"✔  " : L"✖  ") + *ms[i], x, y + i * 24.0f, body, oks[i] ? C_OK : C_RED_HI,
             StringAlignmentNear, cw - 64, 22);
    }
}

void path_row(Graphics& gr, float x, float y, const std::wstring& label, const std::wstring& value,
              const std::wstring& note, bool ok, int id_browse, int id_open, const Font& body, const Font& fsm)
{
    text(gr, label, x, y, body, C_TEXT, StringAlignmentNear, 170, 32);
    RectF fr(x + 175, y, 470, 32);
    field(gr, fr, false, false);
    text(gr, value.empty() ? L"(não configurado)" : value, fr.X + 8, fr.Y, body, value.empty() ? C_DIM : C_TEXT,
         StringAlignmentNear, fr.Width - 14, fr.Height);
    button(gr, RectF(x + 655, y, 110, 32), L"Procurar...", id_browse, body);
    button(gr, RectF(x + 773, y, 80, 32), L"Abrir", id_open, body, false, !value.empty());
    text(gr, (ok ? L"✔  " : L"✖  ") + note, x + 175, y + 36, fsm, ok ? C_OK : C_RED_HI,
         StringAlignmentNear, 680, 20);
}

void draw_jogo(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    section(gr, L"PASTAS E ARQUIVOS", x, y, W - SIDE - 64.0f, head);
    y += 52;
    bool ok;
    std::wstring n = check_game(g.s.game, &ok);
    path_row(gr, x, y, L"Pasta do jogo", g.s.game, n, ok, ID_GAME_BROWSE, ID_GAME_OPEN, body, fsm);
    text(gr, std::wstring(L"Sua cópia de ") + GAME_CODE + L" (com PARAM.SFO e USRDIR). O EBOOT.elf precisa estar "
         L"descriptografado (o RPCS3 gera).",
         x + 175, y + 58, fsm, C_DIM, StringAlignmentNear, 690, 20);
    y += 110;
    n = check_rap(g.s.rap, &ok);
    path_row(gr, x, y, L"Licença (.rap)", g.s.rap, n, ok, ID_RAP_BROWSE, ID_RAP_OPEN, body, fsm);
    text(gr, L"O arquivo .rap da conta que comprou o jogo (no RPCS3: dev_hdd0\\home\\00000001\\exdata).",
         x + 175, y + 58, fsm, C_DIM, StringAlignmentNear, 690, 20);
    y += 110;
    n = check_fw(g.s.fw, &ok);
    path_row(gr, x, y, L"Firmware (dev_flash)", g.s.fw, n, ok, ID_FW_BROWSE, ID_FW_OPEN, body, fsm);
    text(gr, L"A pasta dev_flash do firmware do PS3 (no RPCS3, ao lado do rpcs3.exe). O jogo usa dela o "
         L"decodificador de áudio.", x + 175, y + 58, fsm, C_DIM, StringAlignmentNear, 690, 20);
    if (g.running)
        text(gr, L"Mudanças nesta aba valem quando o jogo for aberto de novo.", x, y + 110, body, C_RED_HI,
             StringAlignmentNear, 690, 24);
}

void draw_radio(Graphics& gr, float x, float y, const std::wstring& t, bool on, int id, const Font& f)
{
    Pen pen(on ? C_TEXT : C_DIM, 1.5f);
    gr.DrawEllipse(&pen, x, y + 6, 16.0f, 16.0f);
    if (on) { SolidBrush b(C_TEXT); gr.FillEllipse(&b, x + 4, y + 10, 8.0f, 8.0f); }
    text(gr, t, x + 24, y, f, C_TEXT, StringAlignmentNear, 120, 28);
    hit(x, y, 140, 28, id);
}

/* The connected controller, drawn as a vector in a 100 x 68 design box and
 * scaled into r: DualSense (light shell, dark centre band), DualShock 4 (one
 * tone, dark touchpad) or Xbox (rounder, offset sticks, logo button). */
void draw_controller(Graphics& gr, int kind, RectF r)
{
    GraphicsState st = gr.Save();
    gr.TranslateTransform(r.X, r.Y);
    gr.ScaleTransform(r.Width / 100.0f, r.Height / 68.0f);
    const Color shell = kind == PAD_KIND_DUALSENSE ? Color(255, 236, 232, 226) : Color(255, 200, 196, 190);
    const Color dark(255, 34, 30, 30), mid(255, 92, 86, 84);
    SolidBrush bShell(shell), bDark(dark), bMid(mid);
    Pen pDark(dark, 1.8f), pShell(shell, 1.8f);
    pDark.SetLineJoin(LineJoinRound); pShell.SetLineJoin(LineJoinRound);
    auto ring = [&](Pen& p, float cx, float cy, float rr) { gr.DrawEllipse(&p, cx - rr, cy - rr, 2 * rr, 2 * rr); };
    auto disc = [&](Brush& b, float cx, float cy, float rr) { gr.FillEllipse(&b, cx - rr, cy - rr, 2 * rr, 2 * rr); };
    auto cross = [&](Brush& b, float cx, float cy, float a, float w) {
        gr.FillRectangle(&b, cx - w / 2, cy - a, w, 2 * a); gr.FillRectangle(&b, cx - a, cy - w / 2, 2 * a, w); };
    GraphicsPath body;
    if (kind == PAD_KIND_XINPUT) {
        body.StartFigure();
        body.AddLine(24.0f, 9.0f, 76.0f, 9.0f);
        body.AddBezier(76.0f, 9.0f, 88.0f, 9.0f, 95.0f, 16.0f, 96.0f, 28.0f);
        body.AddBezier(96.0f, 28.0f, 98.0f, 44.0f, 98.0f, 58.0f, 92.0f, 64.0f);
        body.AddBezier(92.0f, 64.0f, 88.0f, 68.0f, 80.0f, 66.0f, 76.0f, 60.0f);
        body.AddBezier(76.0f, 60.0f, 72.0f, 54.0f, 66.0f, 52.0f, 60.0f, 52.0f);
        body.AddLine(60.0f, 52.0f, 40.0f, 52.0f);
        body.AddBezier(40.0f, 52.0f, 34.0f, 52.0f, 28.0f, 54.0f, 24.0f, 60.0f);
        body.AddBezier(24.0f, 60.0f, 20.0f, 66.0f, 12.0f, 68.0f, 8.0f, 64.0f);
        body.AddBezier(8.0f, 64.0f, 2.0f, 58.0f, 2.0f, 44.0f, 4.0f, 28.0f);
        body.AddBezier(4.0f, 28.0f, 5.0f, 16.0f, 12.0f, 9.0f, 24.0f, 9.0f);
        body.CloseFigure();
        gr.FillPath(&bShell, &body);
        ring(pDark, 26, 27, 7.5f); ring(pDark, 26, 27, 4.5f);        /* left stick, high */
        ring(pDark, 62, 44, 7.5f); ring(pDark, 62, 44, 4.5f);        /* right stick, low */
        cross(bDark, 38, 44, 6.5f, 3.6f);                            /* d-pad */
        ring(pDark, 76, 19, 3.2f); ring(pDark, 83, 26, 3.2f);        /* A B X Y */
        ring(pDark, 76, 33, 3.2f); ring(pDark, 69, 26, 3.2f);
        ring(pDark, 50, 17, 4.6f);                                   /* Xbox button */
        Pen x(dark, 1.4f); gr.DrawLine(&x, 47.6f, 14.6f, 52.4f, 19.4f); gr.DrawLine(&x, 47.6f, 19.4f, 52.4f, 14.6f);
        disc(bDark, 43, 27, 1.6f); disc(bDark, 57, 27, 1.6f);        /* view / menu */
    } else {
        body.StartFigure();
        body.AddLine(20.0f, 8.0f, 80.0f, 8.0f);
        body.AddBezier(80.0f, 8.0f, 88.0f, 8.0f, 94.0f, 12.0f, 95.0f, 22.0f);
        body.AddBezier(95.0f, 22.0f, 97.0f, 36.0f, 99.0f, 52.0f, 96.0f, 62.0f);
        body.AddBezier(96.0f, 62.0f, 94.0f, 68.0f, 84.0f, 68.0f, 80.0f, 62.0f);
        body.AddBezier(80.0f, 62.0f, 76.0f, 56.0f, 70.0f, 48.0f, 62.0f, 48.0f);
        body.AddLine(62.0f, 48.0f, 38.0f, 48.0f);
        body.AddBezier(38.0f, 48.0f, 30.0f, 48.0f, 24.0f, 56.0f, 20.0f, 62.0f);
        body.AddBezier(20.0f, 62.0f, 16.0f, 68.0f, 6.0f, 68.0f, 4.0f, 62.0f);
        body.AddBezier(4.0f, 62.0f, 1.0f, 52.0f, 3.0f, 36.0f, 5.0f, 22.0f);
        body.AddBezier(5.0f, 22.0f, 6.0f, 12.0f, 12.0f, 8.0f, 20.0f, 8.0f);
        body.CloseFigure();
        gr.FillPath(&bShell, &body);
        if (kind == PAD_KIND_DUALSENSE) {
            /* the dark centre band that holds the sticks */
            GraphicsPath band;
            band.StartFigure();
            band.AddBezier(22.0f, 30.0f, 34.0f, 27.0f, 66.0f, 27.0f, 78.0f, 30.0f);
            band.AddBezier(78.0f, 30.0f, 74.0f, 38.0f, 68.0f, 48.0f, 62.0f, 48.0f);
            band.AddLine(62.0f, 48.0f, 38.0f, 48.0f);
            band.AddBezier(38.0f, 48.0f, 32.0f, 48.0f, 26.0f, 38.0f, 22.0f, 30.0f);
            band.CloseFigure();
            gr.FillPath(&bDark, &band);
            GraphicsPath* tp = round_rect(RectF(33, 6, 34, 20), 3);   /* touchpad */
            gr.FillPath(&bShell, tp); Pen tpe(mid, 1.2f); gr.DrawPath(&tpe, tp); delete tp;
            ring(pShell, 38, 39, 5.5f); ring(pShell, 62, 39, 5.5f);  /* sticks on the band */
            disc(bShell, 50, 41, 1.8f);                              /* PS button */
        } else {
            GraphicsPath* tp = round_rect(RectF(34, 8, 32, 18), 3);   /* touchpad */
            gr.FillPath(&bDark, tp); delete tp;
            disc(bDark, 36, 41, 6.5f); disc(bDark, 64, 41, 6.5f);   /* sticks */
            disc(bMid, 36, 41, 3.2f); disc(bMid, 64, 41, 3.2f);
            disc(bDark, 50, 36, 2.2f);                               /* PS button */
        }
        cross(bDark, 18, 26, 6.0f, 3.4f);                            /* d-pad */
        ring(pDark, 82, 19, 2.8f); ring(pDark, 88, 26, 2.8f);        /* face buttons */
        ring(pDark, 82, 33, 2.8f); ring(pDark, 76, 26, 2.8f);
    }
    gr.Restore(st);
}

/* Controller status: a card with the connected controller, a coloured dot
 * and the model; a struck-through generic pad when there is none. */
void draw_pad_card(Graphics& gr, float x, float y, float w, float h, const Font& body, const Font& fsm)
{
    const bool on = !g.pad_status.empty();
    const Color accent = on ? Color(255, 110, 200, 100) : Color(255, 200, 70, 70);
    GraphicsPath* card = round_rect(RectF(x, y, w, h), 6);
    SolidBrush bg(on ? Color(255, 24, 34, 24) : Color(255, 36, 22, 22)); gr.FillPath(&bg, card);
    Pen bd(on ? Color(255, 60, 110, 56) : Color(255, 110, 40, 40), 1); gr.DrawPath(&bd, card); delete card;
    if (on) draw_controller(gr, g.pad_kind, RectF(x + 10, y + (h - 40) / 2, 59, 40));
    /* no controller: a generic gamepad, struck through */
    const float gx = x + 22, gy = y + h / 2 - 11;
    if (!on) {
    SolidBrush pad(on ? accent : Color(255, 110, 100, 98));
    GraphicsPath gp;
    gp.AddEllipse(gx, gy + 4, 14.0f, 18.0f);
    gp.AddEllipse(gx + 22, gy + 4, 14.0f, 18.0f);
    gp.AddRectangle(RectF(gx + 6, gy + 2, 24, 12));
    gp.SetFillMode(FillModeWinding);
    gr.FillPath(&pad, &gp);
    SolidBrush hole(on ? Color(255, 24, 34, 24) : Color(255, 36, 22, 22));
    gr.FillEllipse(&hole, gx + 8, gy + 6, 5.0f, 5.0f);
    gr.FillEllipse(&hole, gx + 23, gy + 6, 5.0f, 5.0f);
        Pen slash(Color(255, 200, 70, 70), 2.2f); slash.SetStartCap(LineCapRound); slash.SetEndCap(LineCapRound);
        gr.DrawLine(&slash, gx + 2, gy + 22, gx + 34, gy);
    }
    /* model + hint */
    std::wstring title = on ? g.pad_status + L" conectado" : L"Nenhum controle conectado";
    if (on && g.pad_count > 1) title += L"  (+" + std::to_wstring(g.pad_count - 1) + L")";
    text(gr, title, x + 82, y + h / 2 - 22, body, on ? C_TEXT : C_RED_HI, StringAlignmentNear, w - 200, 22);
    text(gr, on ? L"O teclado também funciona junto." : L"Conecte por USB ou Bluetooth.",
         x + 82, y + h / 2, fsm, C_DIM, StringAlignmentNear, w - 200, 20);
    /* status pill */
    RectF pr(x + w - 130, y + h / 2 - 11, 118, 22);
    GraphicsPath* pp = round_rect(pr, 11);
    SolidBrush pb(on ? Color(255, 34, 66, 32) : Color(255, 70, 26, 26)); gr.FillPath(&pb, pp); delete pp;
    SolidBrush dot(accent); gr.FillEllipse(&dot, pr.X + 10, pr.Y + 7, 8.0f, 8.0f);
    FontFamily ff(L"Segoe UI"); Font pf(&ff, 10.5f, FontStyleBold, UnitPixel);
    text(gr, on ? L"CONECTADO" : L"DESCONECTADO", pr.X + 22, pr.Y, pf, accent, StringAlignmentNear, pr.Width - 24, pr.Height);
}

void draw_controles(Graphics& gr, const Font& head, const Font& body, const Font& fsm, const Font& col)
{
    float x = SIDE + 32.0f, y = 22;
    section(gr, L"CONTROLES", x, y, W - SIDE - 64.0f, head);
    text(gr, L"Qual tecla ou botão do controle aciona cada botão do jogo. Clique numa caixa e aperte a "
         L"tecla ou o botão desejado; salva na hora.", x, y + 32, fsm, C_DIM, StringAlignmentNear, 820, 20);
    y += 72;
    text(gr, L"Ícones do controle", x, y, body, C_TEXT, StringAlignmentNear, 160, 28);
    draw_radio(gr, x + 170, y, L"PlayStation", g.s.layout == 0, ID_LAYOUT_PS, body);
    draw_radio(gr, x + 310, y, L"Xbox", g.s.layout == 1, ID_LAYOUT_XB, body);
    draw_pad_card(gr, x + 420, y - 14, W - SIDE - 64.0f - 420, 56, body, fsm);
    y += 54;
    const float c1 = x, c2 = x + 250, c3 = x + 480, rowh = 31;
    text(gr, L"BOTÃO DO JOGO", c1, y, col, C_RED);
    text(gr, L"TECLADO", c2, y, col, C_RED);
    text(gr, L"CONTROLE", c3, y, col, C_RED);
    y += 30;
    for (int i = 0; i < NROWS; i++) {
        int a = k_rows[i];
        float ry = y + i * rowh;
        btn_glyph(gr, 0, k_pad_default_btn[a], c1 - 2, ry);
        text(gr, k_action_name[a], c1 + 38, ry, body, C_TEXT, StringAlignmentNear, 200, 30);
        /* keyboard cell */
        RectF kr(c2, ry + 1, 200, 28);
        bool kact = g.cap == CAP_KEY && g.cap_action == a;
        field(gr, kr, g.hover == ID_KEY + a, kact);
        text(gr, kact ? L"Aperte uma tecla..." : key_name(g.s.key[a]), kr.X + 8, kr.Y, body,
             kact ? C_RED_HI : C_TEXT, StringAlignmentNear, kr.Width - 12, kr.Height);
        hit(kr.X, kr.Y, kr.Width, kr.Height, ID_KEY + a);
        /* controller cell */
        RectF br(c3 + 34, ry + 1, 228, 28);
        bool bact = g.cap == CAP_BTN && g.cap_action == a;
        int pb = g.s.btn[a];
        if (!bact && pb >= 0) btn_glyph(gr, g.s.layout, pb, c3 - 2, ry);
        field(gr, br, g.hover == ID_BTN + a, bact);
        text(gr, bact ? L"Aperte um botão no controle..." : (pb >= 0 ? k_pb_name[g.s.layout][pb] : L"-"),
             br.X + 8, br.Y, body, bact ? C_RED_HI : C_TEXT, StringAlignmentNear, br.Width - 30, br.Height);
        SolidBrush ab(C_DIM);
        PointF tri[3] = { PointF(br.X + br.Width - 18, br.Y + 11), PointF(br.X + br.Width - 8, br.Y + 11),
                          PointF(br.X + br.Width - 13, br.Y + 17) };
        gr.FillPolygon(&ab, tri, 3);
        hit(br.X, br.Y, br.Width, br.Height, ID_BTN + a);
    }
    float by = y + NROWS * rowh + 10;
    button(gr, RectF(x, by, 160, 32), L"Restaurar padrões", ID_RESTORE, body);
    text(gr, L"Teclado e controle funcionam juntos. "
         L"Esc cancela.", x + 175, by, fsm, C_DIM, StringAlignmentNear, W - 280.0f - (x + 175), 32);
}

void paint(HDC hdc)
{
    Bitmap back(W, H, PixelFormat32bppARGB);
    Graphics gr(&back);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gr.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    SolidBrush bg(C_BG); gr.FillRectangle(&bg, 0, 0, W, H);
    g.hits.clear();

    FontFamily serif(L"Georgia"), sans(L"Segoe UI");
    Font fTitle(&serif, 22, FontStyleBold, UnitPixel), fTitle2(&serif, 17, FontStyleBold, UnitPixel);
    Font fNav(&sans, 16, FontStyleRegular, UnitPixel), fHead(&serif, 18, FontStyleRegular, UnitPixel);
    Font fBody(&sans, 14, FontStyleRegular, UnitPixel), fSmall(&sans, 12, FontStyleRegular, UnitPixel);
    Font fCode(&serif, 18, FontStyleBold, UnitPixel), fBig(&serif, 22, FontStyleBold, UnitPixel);
    Font fCol(&serif, 15, FontStyleBold, UnitPixel);

    if (g.page == P_INICIO) draw_inicio(gr, fHead, fBody, fCode);
    else if (g.page == P_JOGO) draw_jogo(gr, fHead, fBody, fSmall);
    else draw_controles(gr, fHead, fBody, fSmall, fCol);
    draw_sidebar(gr, fTitle, fTitle2, fNav);
    draw_play(gr, fBig, fSmall);

    /* open dropdown list on top */
    if (g.open_dd >= 0) {
        std::vector<std::wstring> items; int sel = 0;
        float x = SIDE + 32.0f + 210, y = 330 + 14 + 44 + 30, bw = 200;
        if (g.open_dd == ID_DD_MODE) { for (auto m : k_modes) items.push_back(m); sel = g.s.mode; }
        else if (g.open_dd == ID_DD_RES) { for (auto& r : k_res) items.push_back(r.name); sel = g.s.res; y += 44; }
        else { for (auto a : k_aa) items.push_back(a); sel = g.s.aa; x = SIDE + 32.0f + 630; y += 44; bw = 150; }
        RectF box(x, y, bw, items.size() * 28.0f);
        SolidBrush lb(Color(255, 34, 28, 28)); gr.FillRectangle(&lb, box);
        Pen lp(C_RED, 1); gr.DrawRectangle(&lp, box.X, box.Y, box.Width - 1, box.Height - 1);
        /* remove underlying hits so clicks land on the list */
        g.hits.clear();
        for (size_t i = 0; i < items.size(); i++) {
            RectF ir(x, y + i * 28.0f, bw, 28);
            bool hot = g.hover == ID_DD_ITEM + (int)i;
            if (hot || (int)i == sel) { SolidBrush hb(hot ? Color(255, 70, 24, 24) : Color(255, 46, 30, 30)); gr.FillRectangle(&hb, ir); }
            text(gr, items[i], ir.X + 8, ir.Y, fBody, C_TEXT, StringAlignmentNear, ir.Width - 12, ir.Height);
            hit(ir.X, ir.Y, ir.Width, ir.Height, ID_DD_ITEM + (int)i);
        }
    }

    Graphics screen(hdc);
    screen.DrawImage(&back, 0, 0);
}

int hit_at(int x, int y)
{
    for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it)
        if (it->r.Contains((REAL)x, (REAL)y)) return it->id;
    return -1;
}

std::wstring pick(bool folder, const std::wstring& start, const wchar_t* filter_name, const wchar_t* filter)
{
    std::wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0; dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    if (!folder && filter) { COMDLG_FILTERSPEC fs_[2] = { { filter_name, filter }, { L"Todos", L"*.*" } }; dlg->SetFileTypes(2, fs_); }
    if (!start.empty()) {
        IShellItem* si = nullptr;
        std::wstring d = folder ? start : fs::path(start).parent_path().wstring();
        if (SUCCEEDED(SHCreateItemFromParsingName(d.c_str(), NULL, IID_PPV_ARGS(&si)))) { dlg->SetFolder(si); si->Release(); }
    }
    if (SUCCEEDED(dlg->Show(g.hwnd))) {
        IShellItem* r = nullptr;
        if (SUCCEEDED(dlg->GetResult(&r))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(r->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; CoTaskMemFree(p); }
            r->Release();
        }
    }
    dlg->Release();
    return out;
}

void open_in_explorer(const std::wstring& p)
{
    if (p.empty()) return;
    std::error_code ec;
    if (fs::is_directory(p, ec)) ShellExecuteW(g.hwnd, L"open", p.c_str(), NULL, NULL, SW_SHOWNORMAL);
    else { std::wstring a = L"/select,\"" + p + L"\""; ShellExecuteW(g.hwnd, L"open", L"explorer.exe", a.c_str(), NULL, SW_SHOWNORMAL); }
}

/* While the game runs: display mode, resolution and controls apply at once. */
void apply_live()
{
    if (!g.running) return;
    if (g.s.mode != g.applied_mode) {
        g.applied_mode = g.s.mode;
        rsx_d3d12_set_window_mode(g.s.mode);
        SetTimer(g.hwnd, 2, 200, NULL);   /* then put the menu back on top */
    }
    if (g.s.res != g.applied_res) {
        g.applied_res = g.s.res;
        rsx_d3d12_set_res_scale((float)atof(k_res[g.s.res].scale));
    }
    if (g.s.aa != g.applied_aa) {
        g.applied_aa = g.s.aa;
        rsx_d3d12_set_aa(g.s.aa);
    }
    cellpad_set_map(g.s.btn, g.s.key);
}

void show_menu()
{
    ShowWindow(g.hwnd, SW_RESTORE);
    SetWindowPos(g.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g.hwnd);
}
void hide_menu()
{
    g.cap = CAP_NONE; g.open_dd = -1;
    SetWindowPos(g.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (HWND gw = (HWND)rsx_d3d12_window()) {
        if (g.s.mode != 0) ShowWindow(g.hwnd, SW_MINIMIZE);
        SetForegroundWindow(gw);
    }
}
void toggle_menu() { if (GetForegroundWindow() == g.hwnd) hide_menu(); else show_menu(); }
const UINT WM_TOGGLE_MENU = WM_APP + 1;
void menu_hook() { PostMessageW(g.hwnd, WM_TOGGLE_MENU, 0, 0); }   /* game window thread */
void quit_game()
{
    if (rsx_d3d12_window()) rsx_d3d12_close();
    else ExitProcess(0);   /* still booting: nothing to close yet */
}
void start_game();

void changed() { save_settings(g.s); g.play_msg.clear(); apply_live(); InvalidateRect(g.hwnd, NULL, FALSE); }

void click(int id)
{
    if (g.open_dd >= 0) {
        if (id >= ID_DD_ITEM && id < ID_DD_ITEM + 10) {
            int i = id - ID_DD_ITEM;
            if (g.open_dd == ID_DD_MODE) g.s.mode = i;
            else if (g.open_dd == ID_DD_RES) g.s.res = i;
            else g.s.aa = i;
            g.open_dd = -1; changed(); return;
        }
        g.open_dd = -1; InvalidateRect(g.hwnd, NULL, FALSE); return;
    }
    if (g.cap != CAP_NONE) { g.cap = CAP_NONE; g.cap_action = -1; }
    if (id >= ID_NAV && id < ID_NAV + 3) { g.page = (Page)(id - ID_NAV); }
    else if (id == ID_DD_MODE || id == ID_DD_RES || id == ID_DD_AA) { g.open_dd = id; }
    else if (id == ID_GAME_BROWSE) { auto p = pick(true, g.s.game, nullptr, nullptr); if (!p.empty()) { g.s.game = p; changed(); } }
    else if (id == ID_RAP_BROWSE) { auto p = pick(false, g.s.rap, L"Licença PS3 (*.rap)", L"*.rap"); if (!p.empty()) { g.s.rap = p; changed(); } }
    else if (id == ID_FW_BROWSE) { auto p = pick(true, g.s.fw, nullptr, nullptr); if (!p.empty()) { g.s.fw = p; changed(); } }
    else if (id == ID_GAME_OPEN) open_in_explorer(g.s.game);
    else if (id == ID_RAP_OPEN) open_in_explorer(g.s.rap);
    else if (id == ID_FW_OPEN) open_in_explorer(g.s.fw);
    else if (id == ID_LAYOUT_PS || id == ID_LAYOUT_XB) { g.s.layout = id - ID_LAYOUT_PS; changed(); }
    else if (id >= ID_KEY && id < ID_KEY + PA_COUNT) { g.cap = CAP_KEY; g.cap_action = id - ID_KEY; g.cap_since = GetTickCount64(); SetFocus(g.hwnd); }
    else if (id >= ID_BTN && id < ID_BTN + PA_COUNT) {
        g.cap = CAP_BTN; g.cap_action = id - ID_BTN; g.cap_since = GetTickCount64();
        for (int i = 0; i < PAD_PHYS_MAX; i++) { PadPhys p; pad_phys_poll(i, &p); g.pad_prev[i] = p.buttons; }
    }
    else if (id == ID_RESTORE) {
        memcpy(g.s.key, k_pad_default_key, sizeof g.s.key);
        memcpy(g.s.btn, k_pad_default_btn, sizeof g.s.btn);
        changed();
    }
    else if (id == ID_PLAY) {
        bool ok; check_game(g.s.game, &ok);
        if (!ok) { g.page = P_JOGO; g.play_msg = L"Configure a pasta do jogo antes de jogar."; }
        else { save_settings(g.s); start_game(); }
    }
    else if (id == ID_RESUME) { hide_menu(); return; }
    else if (id == ID_QUIT) { quit_game(); return; }
    InvalidateRect(g.hwnd, NULL, FALSE);
}

int vk_from_msg(WPARAM wp, LPARAM lp)
{
    UINT sc = (lp >> 16) & 0xFF; bool ext = (lp >> 24) & 1;
    switch (wp) {
    case VK_SHIFT:   return (int)MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
    case VK_CONTROL: return ext ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:    return ext ? VK_RMENU : VK_LMENU;
    default:         return (int)wp;
    }
}

LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
    case WM_MOUSEMOVE: {
        int id = hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (id != g.hover) { g.hover = id; InvalidateRect(h, NULL, FALSE); }
        SetCursor(LoadCursor(NULL, id >= 0 ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSELEAVE: g.hover = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) return TRUE; break;
    case WM_LBUTTONDOWN: click(hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (g.cap == CAP_KEY) {
            if (wp == VK_ESCAPE) { g.cap = CAP_NONE; InvalidateRect(h, NULL, FALSE); return 0; }
            int vk = vk_from_msg(wp, lp);
            g.s.key[g.cap_action] = vk; g.cap = CAP_NONE; changed(); return 0;
        }
        if (g.cap == CAP_BTN && wp == VK_ESCAPE) { g.cap = CAP_NONE; InvalidateRect(h, NULL, FALSE); return 0; }
        if (wp == VK_ESCAPE && g.open_dd >= 0) { g.open_dd = -1; InvalidateRect(h, NULL, FALSE); return 0; }
        if (g.running && (wp == VK_INSERT || wp == VK_ESCAPE)) { hide_menu(); return 0; }
        if (m == WM_SYSKEYDOWN) break;
        return 0;
    case WM_ACTIVATE:
        g_pad_block = g.running && LOWORD(wp) != WA_INACTIVE;   /* the game ignores input under the menu */
        /* However it was activated (taskbar, Alt+Tab, a click), come above a
         * fullscreen game, which is topmost; give that up when the game
         * takes the focus back. */
        if (g.running)
            SetWindowPos(h, LOWORD(wp) != WA_INACTIVE ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        break;
    case WM_CLOSE:
        if (g.running) { ShowWindow(h, SW_HIDE); quit_game(); return 0; }
        break;
    case WM_TIMER: {
        if (wp == 2) {
            KillTimer(h, 2);
            if (GetForegroundWindow() == h || IsWindowVisible(h)) show_menu();
            return 0;
        }
        if (g.running) {   /* R3+L2 on any controller toggles the menu */
            uint32_t combo = 0;
            for (int i = 0; i < PAD_PHYS_MAX; i++) {
                PadPhys p; if (!pad_phys_poll(i, &p)) continue;
                if ((p.buttons & (1u << PB_R3)) && (p.buttons & (1u << PB_L2))) combo |= 1u << i;
            }
            if (combo & ~g.combo_prev) toggle_menu();
            g.combo_prev = combo;
        }
        /* controller status + button capture */
        std::wstring st; int cnt = 0;
        for (int i = 0; i < PAD_PHYS_MAX; i++) {
            PadPhys p; if (!pad_phys_poll(i, &p)) continue;
            if (cnt++ == 0)
                st = (p.kind == PAD_KIND_XINPUT) ? L"Controle Xbox"
                   : (p.kind == PAD_KIND_DS4) ? L"DualShock 4" : L"DualSense";
        }
        int kind = 0;
        for (int i = 0; i < PAD_PHYS_MAX && !kind; i++) { PadPhys p; if (pad_phys_poll(i, &p)) kind = p.kind; }
        if (const char* pv = getenv("CV_PAD_PREVIEW")) {   /* show a model without that controller (1..3) */
            kind = atoi(pv); cnt = kind ? 1 : 0;
            st = kind == PAD_KIND_XINPUT ? L"Controle Xbox" : kind == PAD_KIND_DS4 ? L"DualShock 4" : kind ? L"DualSense" : L"";
        }
        bool redraw = st != g.pad_status || cnt != g.pad_count || kind != g.pad_kind;
        g.pad_status = st; g.pad_count = cnt; g.pad_kind = kind;
        if (g.cap == CAP_BTN) {
            for (int i = 0; i < PAD_PHYS_MAX; i++) {
                PadPhys p; pad_phys_poll(i, &p);
                uint32_t pressed = p.buttons & ~g.pad_prev[i];
                g.pad_prev[i] = p.buttons;
                for (int b = 0; b < PB_COUNT && pressed; b++)
                    if (pressed & (1u << b)) { g.s.btn[g.cap_action] = b; g.cap = CAP_NONE; changed(); return 0; }
            }
            if (GetTickCount64() - g.cap_since > 10000) { g.cap = CAP_NONE; redraw = true; }
        }
        if (redraw) InvalidateRect(h, NULL, FALSE);
        return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    if (m == WM_TOGGLE_MENU) { toggle_menu(); return 0; }
    return DefWindowProcW(h, m, wp, lp);
}

void set_env(const char* k, const std::string& v) { _putenv_s(k, v.c_str()); }
void set_env_default(const char* k, const char* v) { if (!getenv(k)) _putenv_s(k, v); }
std::string narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    return s;
}

HANDLE s_play_ev;

/* Jogar: hand the choices to the runtime as environment, then keep this
 * window as the in-game options menu. */
void start_game()
{
    const Settings& s = g.s;
    set_env("PS3_VFS_ROOT", narrow(s.game));
    if (!s.rap.empty()) set_env("PS3_RAP_DIR", narrow(fs::path(s.rap).parent_path().wstring()));
    if (!s.fw.empty()) set_env("PS3_DEV_FLASH", narrow(s.fw));
    set_env("RES_SCALE", k_res[s.res].scale);
    set_env("WINDOW_MODE", k_mode_env[s.mode]);
    set_env("AA_MODE", std::to_string(s.aa));
    std::string bm, km;
    for (int a = 0; a < PA_COUNT; a++) {
        bm += std::string(k_pad_action_id[a]) + "=" + pad_phys_id(s.btn[a]) + (a + 1 < PA_COUNT ? "," : "");
        km += std::string(k_pad_action_id[a]) + "=" + std::to_string(s.key[a]) + (a + 1 < PA_COUNT ? "," : "");
    }
    set_env("PAD_BTNMAP", bm);
    set_env("PAD_KEYMAP", km);
    /* The tuning the launcher script carries, for a direct start. */
    set_env_default("SPU_INTERP_UNLIFTED", "1");
    set_env_default("SPU_MAX_RUNNING", "10");
    set_env_default("PPU_BOOST_NAMES", "snddrv");
    set_env_default("CV_AV_CONTINUOUS_PTS", "1");
    set_env_default("PS3_VERBOSE", "0");
    set_env_default("SCREEN_AS_TEX", "0");
    set_env_default("VP_NOFIXPROJ", "1");
    set_env_default("GCM_PARK_GRACE_MS", "5000");
    set_env_default("PS3_TITLE", "Resident Evil Code: Veronica X");
    g.eboot = narrow((fs::path(s.game) / L"USRDIR" / L"EBOOT.elf").wstring());
    g.running = true;
    g.applied_mode = s.mode; g.applied_res = s.res; g.applied_aa = s.aa;
    g_rsx_menu_hook = menu_hook;
    g.page = P_INICIO;
    InvalidateRect(g.hwnd, NULL, FALSE);
    SetEvent(s_play_ev);
}

DWORD WINAPI launcher_thread(void*)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi; ULONG_PTR gtok;
    GdiplusStartup(&gtok, &gsi, NULL);
    g.s = load_settings();
    g.banner = load_png("BANNER");

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"cv_launcher";
    wc.hIcon = LoadIcon(GetModuleHandleW(NULL), MAKEINTRESOURCE(1));
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g.hwnd = CreateWindowExW(0, L"cv_launcher", L"Resident Evil Code: Veronica X (PC)", style,
                             wa.left + ((wa.right - wa.left) - ww) / 2, wa.top + ((wa.bottom - wa.top) - wh) / 2,
                             ww, wh, NULL, NULL, wc.hInstance, NULL);
    BOOL dark = TRUE; DwmSetWindowAttribute(g.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    ShowWindow(g.hwnd, SW_SHOW);
    SetForegroundWindow(g.hwnd);
    SetTimer(g.hwnd, 1, 30, NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    /* closed before Jogar: the main thread sees this thread end */
    delete g.banner; g.banner = nullptr;
    GdiplusShutdown(gtok);
    return 0;
}

}  /* namespace */

/* Opens the launcher on its own thread and returns 1 with the EBOOT.elf path
 * once the player pressed Jogar, or 0 if the window was closed first. After
 * Jogar the window stays open as the options menu for the running game. */
extern "C" int cv_launcher_run(char* eboot_out, size_t cap)
{
    s_play_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, launcher_thread, NULL, 0, NULL);
    if (!th) return 0;
    HANDLE hs[2] = { s_play_ev, th };
    DWORD w = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
    if (w != WAIT_OBJECT_0) return 0;
    snprintf(eboot_out, cap, "%s", g.eboot.c_str());
    return 1;
}
#endif
