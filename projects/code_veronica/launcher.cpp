/*
 * Code Veronica launcher: the window shown before the game starts.
 *
 * Five pages -- Inicio (display mode, render resolution, anti-aliasing), Jogo
 * (game folder, licence .rap), Controles (keyboard and controller
 * mapping), Patches and Avancado (language, shader cache) -- drawn with GDI+ in
 * the Resident Evil palette, in Portuguese, English, Spanish or Japanese
 * (launcher_i18n.h). Every change is written to
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
#include "launcher_i18n.h"

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
void  ps3_patch_set(const char* name, int on);
}
namespace {

/* ---------------------------------------------------------------- palette */
const Color C_BG(255, 21, 17, 17), C_SIDE(255, 13, 11, 11), C_PANEL(255, 30, 25, 25),
            C_LINE(255, 70, 28, 28), C_TEXT(255, 225, 218, 210), C_DIM(255, 150, 140, 132),
            C_RED(255, 196, 28, 34), C_RED_HI(255, 230, 50, 50), C_HOVER(255, 38, 30, 30),
            C_FIELD(255, 26, 22, 22), C_FIELD_BD(255, 70, 60, 58), C_OK(255, 120, 190, 110);

const wchar_t* GAME_CODE = L"NPEB00553";

/* ---------------------------------------------------------------- language */
int g_lang = LANG_PT;        /* the language shown */
int g_lang_pick = -1;        /* the choice: -1 = Sistema (follow Windows), else a Lang */
const wchar_t* T(int id) { return k_str[g_lang][id]; }

/* Windows' display language -> ours; anything we lack shows in English. */
int system_lang()
{
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_PORTUGUESE: return LANG_PT;  case LANG_SPANISH: return LANG_ES;
    case LANG_JAPANESE:   return LANG_JA;  case LANG_FRENCH:  return LANG_FR;
    case LANG_GERMAN:     return LANG_DE;  case LANG_ITALIAN: return LANG_IT;
    case LANG_POLISH:     return LANG_PL;  case LANG_TURKISH: return LANG_TR;
    case LANG_ARABIC:     return LANG_AR;  case LANG_CHINESE: return LANG_ZH;
    case LANG_KOREAN:     return LANG_KO;  case LANG_RUSSIAN: return LANG_RU;
    default:              return LANG_EN;
    }
}
void set_lang_pick(int pick)
{
    g_lang_pick = pick;
    g_lang = pick < 0 ? system_lang() : pick;
}
std::wstring Tf(int id, int n)                   /* a text with one %d */
{
    wchar_t b[256]; swprintf(b, 256, T(id), n); return b;
}
/* Font families for a language. Georgia and Segoe UI cover Latin and
 * Cyrillic; Segoe UI also Arabic; Japanese, Chinese and Korean use the
 * system's faces for them (the first one installed wins). */
const wchar_t* family(bool serif, int lang)
{
    static const wchar_t* const ja_sans[] = { L"Yu Gothic UI", L"Meiryo UI", L"Meiryo", L"MS UI Gothic", nullptr };
    static const wchar_t* const ja_serif[] = { L"Yu Mincho", L"MS Mincho", L"Yu Gothic UI", L"Meiryo", nullptr };
    static const wchar_t* const zh[] = { L"Microsoft YaHei UI", L"Microsoft YaHei", L"SimHei", L"SimSun", nullptr };
    static const wchar_t* const ko[] = { L"Malgun Gothic", L"Gulim", L"Dotum", nullptr };
    const wchar_t* const* list = nullptr;
    if (lang == LANG_JA) list = serif ? ja_serif : ja_sans;
    else if (lang == LANG_ZH) list = zh;
    else if (lang == LANG_KO) list = ko;
    else if (lang == LANG_AR) return L"Segoe UI";
    else return serif ? L"Georgia" : L"Segoe UI";
    for (int i = 0; list[i]; i++) { FontFamily f(list[i]); if (f.IsAvailable()) return list[i]; }
    return L"Segoe UI";
}
const int W = 1120, H = 770, SIDE = 230;

/* ---------------------------------------------------------------- settings */
struct Settings {
    int mode = 0;          /* 0 janela, 1 sem borda, 2 tela cheia */
    int res = 1;           /* index into k_res */
    int aa = 0;            /* anti-aliasing: 0 off, 1 FXAA, 2 SMAA */
    bool patch[8] = {};    /* k_patches on/off */
    int layout = 0;        /* 0 PlayStation, 1 Xbox */
    std::wstring game, rap, fw;
    int key[PA_COUNT];
    int btn[PA_COUNT];
};
struct ResOpt { const wchar_t* name; const char* scale; };
const ResOpt k_res[] = {
    { L"480p (854x480)", "0.6667" }, { L"Nativa (720p)", "1" }, { L"1080p (1920x1080)", "1.5" },
    { L"1440p (2560x1440)", "2" }, { L"4K (3840x2160)", "3" } };
const char* k_mode_env[] = { "window", "borderless", "fullscreen" };
std::wstring res_name(int i) { return i == 1 ? T(S_RES_NATIVE) : k_res[i].name; }
std::wstring mode_name(int i) { return T(S_MODE_WINDOW + i); }
std::wstring aa_name(int i) { return T(S_AA_OFF + i); }

/* Game patches (projects/code_veronica/patches.json, lifted as switchable
 * sites): id = the runtime flag name. */
struct PatchOpt { const char* id; int title, desc, credit; };
const PatchOpt k_patches[] = {
    { "cv_door_skip", S_PATCH_DOOR_T, S_PATCH_DOOR_D, S_PATCH_DOOR_C },
};
const int NPATCHES = sizeof k_patches / sizeof k_patches[0];

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
/* The d-pad's names are translated; the rest are the buttons' own names. */
std::wstring action_name(int a)
{
    return (a >= PA_UP && a <= PA_RIGHT) ? T(S_DPAD_UP + (a - PA_UP)) : k_action_name[a];
}
std::wstring pb_name(int layout, int pb)
{
    if (pb < 0) return L"-";
    return (pb >= PB_UP && pb <= PB_RIGHT) ? T(S_DPAD_UP + (pb - PB_UP)) : k_pb_name[layout][pb];
}

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
    { std::wstring l = ini_get(L"launcher", L"idioma", L"system");
      int pick = -1;
      for (int i = 0; i < LANG_COUNT; i++) if (l == widen(k_lang_id[i])) pick = i;
      set_lang_pick(pick); }
    for (int i = 0; i < NPATCHES; i++)
        s.patch[i] = _wtoi(ini_get(L"patches", widen(k_patches[i].id).c_str(), L"0").c_str()) != 0;
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
    /* No page for it any more: without a firmware tree the runtime serves its
     * own flashATRAC.pic stand-in. A path in the ini still reaches PS3_DEV_FLASH. */
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
    ini_set(L"launcher", L"idioma", g_lang_pick < 0 ? std::wstring(L"system") : widen(k_lang_id[g_lang_pick]));
    for (int i = 0; i < NPATCHES; i++)
        ini_set(L"patches", widen(k_patches[i].id).c_str(), s.patch[i] ? L"1" : L"0");
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
    if (g.empty()) return T(S_CHK_GAME_CHOOSE);
    if (fs::exists(fs::path(g) / L"USRDIR" / L"EBOOT.elf", ec)) { *ok = true; return T(S_CHK_GAME_OK); }
    if (fs::exists(fs::path(g) / L"USRDIR" / L"EBOOT.BIN", ec)) return T(S_CHK_GAME_BIN);
    return T(S_CHK_GAME_BAD);
}
std::wstring check_rap(const std::wstring& r, bool* ok)
{
    std::error_code ec; *ok = !r.empty() && fs::is_regular_file(r, ec);
    return T(*ok ? S_CHK_RAP_OK : S_CHK_RAP_BAD);
}

/* ---------------------------------------------------------------- keys */
std::wstring key_name(int vk)
{
    switch (vk) {
    case 0: return L"-";
    case VK_SPACE: return T(S_KEY_SPACE); case VK_RETURN: return L"Enter"; case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab"; case VK_ESCAPE: return L"Esc";
    case VK_LSHIFT: return T(S_KEY_LSHIFT); case VK_RSHIFT: return T(S_KEY_RSHIFT);
    case VK_LCONTROL: return T(S_KEY_LCTRL); case VK_RCONTROL: return T(S_KEY_RCTRL);
    case VK_LMENU: return T(S_KEY_LALT); case VK_RMENU: return T(S_KEY_RALT);
    case VK_UP: return T(S_KEY_UP); case VK_DOWN: return T(S_KEY_DOWN);
    case VK_LEFT: return T(S_KEY_LEFT); case VK_RIGHT: return T(S_KEY_RIGHT);
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
    return Tf(S_KEY_N, vk);
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
enum Page { P_INICIO, P_JOGO, P_CONTROLES, P_PATCHES, P_AVANCADO, P_COUNT };
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
    std::wstring cache_msg;        /* result of the last "clear shader cache" */
    int dd_scroll = 0;             /* first visible row of the open dropdown list */
};
UI g;

/* ids */
enum {
    ID_NAV = 100,               /* + page */
    ID_DD_MODE = 200, ID_DD_RES = 201, ID_DD_AA = 202, ID_DD_LANG = 203, ID_DD_ITEM = 300,  /* + item index */
    ID_DD_UP = 390, ID_DD_DOWN = 391,
    ID_PLAY = 400, ID_RESUME, ID_QUIT,
    ID_GAME_BROWSE = 500, ID_GAME_OPEN, ID_RAP_BROWSE, ID_RAP_OPEN,
    ID_LAYOUT_PS = 600, ID_LAYOUT_XB,
    ID_KEY = 700, ID_BTN = 800, ID_RESTORE = 900, ID_PATCH = 1000,  /* + index */
    ID_CACHE_CLEAR = 1100, ID_CACHE_OPEN,
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
    /* Arabic reads right to left: embed each text as a right-to-left run so
     * mixed text like "(720p)" and the final full stop land where they belong. */
    if (g_lang == LANG_AR) {
        const std::wstring rtl = L"\u202B" + t + L"\u202C";
        StringFormat sf; sf.SetAlignment(h); sf.SetLineAlignment(StringAlignmentCenter);
        sf.SetTrimming(StringTrimmingEllipsisCharacter); sf.SetFormatFlags(StringFormatFlagsNoWrap);
        if (w > 0) gr.DrawString(rtl.c_str(), -1, &f, RectF(x, y, w, hh), &sf, &b);
        else { StringFormat s2; gr.DrawString(rtl.c_str(), -1, &f, PointF(x, y), &s2, &b); }
        return;
    }
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

RectF g_dd_anchor;   /* where the open dropdown was drawn this frame */
const int DD_VISIBLE = 9;  /* rows a dropdown list shows before it scrolls */
const int ID_DD_LIST = 392;   /* the list's background: clicks there do nothing */

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
    if (id >= 0 && id == g.open_dd) g_dd_anchor = r;
}

void section(Graphics& gr, const std::wstring& t, float x, float y, float w, const Font& f)
{
    text(gr, t, x, y, f, C_RED);
    Pen pen(C_LINE, 1);
    gr.DrawLine(&pen, x, y + 26, x + w, y + 26);
}


/* ---------------------------------------------------------------- pages */
void draw_sidebar(Graphics& gr, const Font& title, const Font& title2, const Font& nav)
{
    SolidBrush side(C_SIDE); gr.FillRectangle(&side, 0, 0, SIDE, H);
    text(gr, L"RESIDENT EVIL", 22, 26, title, C_TEXT);
    text(gr, L"CODE:VERONICA X", 22, 56, title2, C_RED);
    for (int i = 0; i < P_COUNT; i++) {
        float y = 120.0f + i * 46;
        bool act = (int)g.page == i, hot = g.hover == ID_NAV + i;
        if (act || hot) { SolidBrush hb(act ? Color(255, 30, 22, 22) : C_HOVER); gr.FillRectangle(&hb, 0.0f, y, (float)SIDE, 44.0f); }
        if (act) { SolidBrush rb(C_RED); gr.FillRectangle(&rb, 0.0f, y, 3.0f, 44.0f); }
        text(gr, T(S_NAV_HOME + i), 24, y, nav, act ? C_RED_HI : C_TEXT, StringAlignmentNear, SIDE - 30, 44);
        hit(0, y, SIDE, 44, ID_NAV + i);
    }
}

void draw_play(Graphics& gr, const Font& big, const Font& fsm)
{
    RectF r(W - 250.0f, H - 78.0f, 220.0f, 50.0f);
    if (g.running) {
        button(gr, RectF(W - 300.0f, H - 78.0f, 270.0f, 50.0f), T(S_RESUME), ID_RESUME, big, true, true);
        button(gr, RectF(W - 450.0f, H - 71.0f, 135.0f, 36.0f), T(S_QUIT), ID_QUIT, fsm);
        text(gr, T(S_MENU_HINT),
             SIDE + 32.0f, H - 78.0f, fsm, C_DIM, StringAlignmentNear, W - SIDE - 500.0f, 50);
        return;
    }
    bool ok_game; check_game(g.s.game, &ok_game);
    button(gr, r, T(S_PLAY), ID_PLAY, big, true, true);
    std::wstring msg = g.play_msg;
    if (msg.empty() && !ok_game) msg = T(S_SET_FOLDER_TAB);
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
    section(gr, T(S_QUICK), x, y, cw - 64, head);
    y += 44;
    text(gr, T(S_DISPLAY_MODE), x, y, body, C_TEXT, StringAlignmentNear, 200, 30);
    dropdown(gr, RectF(x + 210, y, 200, 30), mode_name(g.s.mode), ID_DD_MODE, body);
    text(gr, T(S_FRAME_LIMIT), x + 450, y, body, C_TEXT, StringAlignmentNear, 180, 30);
    dropdown(gr, RectF(x + 630, y, 150, 30), T(S_FPS_ORIG), -1, body, false);
    y += 44;
    text(gr, T(S_RENDER_RES), x, y, body, C_TEXT, StringAlignmentNear, 205, 30);
    dropdown(gr, RectF(x + 210, y, 200, 30), res_name(g.s.res), ID_DD_RES, body);
    text(gr, T(S_AA), x + 450, y, body, C_TEXT, StringAlignmentNear, 180, 30);
    dropdown(gr, RectF(x + 630, y, 150, 30), aa_name(g.s.aa), ID_DD_AA, body);
    y += 54;
    bool o1, o2;
    std::wstring m1 = check_game(g.s.game, &o1), m2 = check_rap(g.s.rap, &o2);
    const std::wstring* ms[2] = { &m1, &m2 }; bool oks[2] = { o1, o2 };
    for (int i = 0; i < 2; i++) {
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
    text(gr, value.empty() ? std::wstring(T(S_NOT_SET)) : value, fr.X + 8, fr.Y, body, value.empty() ? C_DIM : C_TEXT,
         StringAlignmentNear, fr.Width - 14, fr.Height);
    button(gr, RectF(x + 655, y, 110, 32), T(S_BROWSE), id_browse, body);
    button(gr, RectF(x + 773, y, 80, 32), T(S_OPEN), id_open, body, false, !value.empty());
    text(gr, (ok ? L"✔  " : L"✖  ") + note, x + 175, y + 36, fsm, ok ? C_OK : C_RED_HI,
         StringAlignmentNear, 680, 20);
}

void draw_jogo(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    section(gr, T(S_FOLDERS), x, y, W - SIDE - 64.0f, head);
    y += 52;
    bool ok;
    std::wstring n = check_game(g.s.game, &ok);
    path_row(gr, x, y, T(S_GAME_FOLDER), g.s.game, n, ok, ID_GAME_BROWSE, ID_GAME_OPEN, body, fsm);
    text(gr, T(S_GAME_FOLDER_HINT),
         x + 175, y + 58, fsm, C_DIM, StringAlignmentNear, 690, 20);
    y += 110;
    n = check_rap(g.s.rap, &ok);
    path_row(gr, x, y, T(S_LICENSE), g.s.rap, n, ok, ID_RAP_BROWSE, ID_RAP_OPEN, body, fsm);
    text(gr, T(S_LICENSE_HINT),
         x + 175, y + 58, fsm, C_DIM, StringAlignmentNear, 690, 20);
    y += 110;
    if (g.running)
        text(gr, T(S_PATHS_RESTART), x, y + 110, body, C_RED_HI,
             StringAlignmentNear, 690, 24);
}

void draw_patches(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    const float w = W - SIDE - 64.0f;
    section(gr, T(S_PATCHES_TITLE), x, y, w, head);
    text(gr, T(S_PATCHES_HINT),
         x, y + 32, fsm, C_DIM, StringAlignmentNear, w, 20);
    y += 74;
    for (int i = 0; i < NPATCHES; i++) {
        const bool on = g.s.patch[i], hot = g.hover == ID_PATCH + i;
        RectF card(x, y, w, 84);
        GraphicsPath* cp = round_rect(card, 6);
        SolidBrush cb(hot ? C_HOVER : C_PANEL); gr.FillPath(&cb, cp);
        Pen cbd(on ? C_RED : C_FIELD_BD, 1); gr.DrawPath(&cbd, cp); delete cp;
        /* switch */
        RectF sw(x + w - 70, y + 28, 48, 26);
        GraphicsPath* sp = round_rect(sw, 13);
        SolidBrush sb(on ? C_RED : Color(255, 60, 52, 52)); gr.FillPath(&sb, sp); delete sp;
        SolidBrush knob(C_TEXT);
        gr.FillEllipse(&knob, on ? sw.X + sw.Width - 23 : sw.X + 3, sw.Y + 3, 20.0f, 20.0f);
        text(gr, T(k_patches[i].title), x + 18, y + 10, body, C_TEXT, StringAlignmentNear, w - 120, 24);
        text(gr, T(k_patches[i].desc), x + 18, y + 34, fsm, C_DIM, StringAlignmentNear, w - 120, 20);
        text(gr, T(k_patches[i].credit), x + 18, y + 54, fsm, Color(255, 120, 110, 104), StringAlignmentNear, w - 120, 20);
        hit(x, y, w, 84, ID_PATCH + i);
        y += 96;
    }
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
    const std::wstring model = g.pad_kind == PAD_KIND_XINPUT ? std::wstring(T(S_PAD_XBOX)) : g.pad_status;
    std::wstring title = T(S_PAD_NONE);
    if (on) { title = T(S_PAD_CONNECTED_FMT); title.replace(title.find(L"%s"), 2, model); }
    if (on && g.pad_count > 1) title += L"  (+" + std::to_wstring(g.pad_count - 1) + L")";
    text(gr, title, x + 82, y + h / 2 - 22, body, on ? C_TEXT : C_RED_HI, StringAlignmentNear, w - 200, 22);
    text(gr, T(on ? S_PAD_KB_TOO : S_PAD_PLUG),
         x + 82, y + h / 2, fsm, C_DIM, StringAlignmentNear, w - 200, 20);
    /* status pill */
    RectF pr(x + w - 130, y + h / 2 - 11, 118, 22);
    GraphicsPath* pp = round_rect(pr, 11);
    SolidBrush pb(on ? Color(255, 34, 66, 32) : Color(255, 70, 26, 26)); gr.FillPath(&pb, pp); delete pp;
    SolidBrush dot(accent); gr.FillEllipse(&dot, pr.X + 10, pr.Y + 7, 8.0f, 8.0f);
    FontFamily ff(family(false, g_lang)); Font pf(&ff, 10.5f, FontStyleBold, UnitPixel);
    text(gr, T(on ? S_CONNECTED : S_DISCONNECTED), pr.X + 22, pr.Y, pf, accent, StringAlignmentNear, pr.Width - 24, pr.Height);
}

void draw_controles(Graphics& gr, const Font& head, const Font& body, const Font& fsm, const Font& col)
{
    float x = SIDE + 32.0f, y = 22;
    section(gr, T(S_CONTROLS_TITLE), x, y, W - SIDE - 64.0f, head);
    text(gr, T(S_CONTROLS_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, 820, 20);
    y += 72;
    text(gr, T(S_ICONS), x, y, body, C_TEXT, StringAlignmentNear, 160, 28);
    draw_radio(gr, x + 170, y, L"PlayStation", g.s.layout == 0, ID_LAYOUT_PS, body);
    draw_radio(gr, x + 310, y, L"Xbox", g.s.layout == 1, ID_LAYOUT_XB, body);
    draw_pad_card(gr, x + 420, y - 14, W - SIDE - 64.0f - 420, 56, body, fsm);
    y += 54;
    const float c1 = x, c2 = x + 250, c3 = x + 480, rowh = 31;
    text(gr, T(S_COL_GAME), c1, y, col, C_RED);
    text(gr, T(S_COL_KB), c2, y, col, C_RED);
    text(gr, T(S_COL_PAD), c3, y, col, C_RED);
    y += 30;
    for (int i = 0; i < NROWS; i++) {
        int a = k_rows[i];
        float ry = y + i * rowh;
        btn_glyph(gr, 0, k_pad_default_btn[a], c1 - 2, ry);
        text(gr, action_name(a), c1 + 38, ry, body, C_TEXT, StringAlignmentNear, 200, 30);
        /* keyboard cell */
        RectF kr(c2, ry + 1, 200, 28);
        bool kact = g.cap == CAP_KEY && g.cap_action == a;
        field(gr, kr, g.hover == ID_KEY + a, kact);
        text(gr, kact ? std::wstring(T(S_PRESS_KEY)) : key_name(g.s.key[a]), kr.X + 8, kr.Y, body,
             kact ? C_RED_HI : C_TEXT, StringAlignmentNear, kr.Width - 12, kr.Height);
        hit(kr.X, kr.Y, kr.Width, kr.Height, ID_KEY + a);
        /* controller cell */
        RectF br(c3 + 34, ry + 1, 228, 28);
        bool bact = g.cap == CAP_BTN && g.cap_action == a;
        int pb = g.s.btn[a];
        if (!bact && pb >= 0) btn_glyph(gr, g.s.layout, pb, c3 - 2, ry);
        field(gr, br, g.hover == ID_BTN + a, bact);
        text(gr, bact ? std::wstring(T(S_PRESS_BTN)) : pb_name(g.s.layout, pb),
             br.X + 8, br.Y, body, bact ? C_RED_HI : C_TEXT, StringAlignmentNear, br.Width - 30, br.Height);
        SolidBrush ab(C_DIM);
        PointF tri[3] = { PointF(br.X + br.Width - 18, br.Y + 11), PointF(br.X + br.Width - 8, br.Y + 11),
                          PointF(br.X + br.Width - 13, br.Y + 17) };
        gr.FillPolygon(&ab, tri, 3);
        hit(br.X, br.Y, br.Width, br.Height, ID_BTN + a);
    }
    float by = y + NROWS * rowh + 10;
    button(gr, RectF(x, by, 160, 32), T(S_RESTORE), ID_RESTORE, body);
    text(gr, T(S_CONTROLS_FOOT), x + 175, by, fsm, C_DIM, StringAlignmentNear, W - 280.0f - (x + 175), 32);
}

/* The shader cache the D3D12 backend keeps under the working directory
 * (shader_cache/*.cso and pipelines.bin). Only those files are removed. */
fs::path cache_dir()
{
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    return fs::path(cwd) / L"shader_cache";
}
bool is_cache_file(const fs::path& p)
{
    const std::wstring n = p.filename().wstring();
    return p.extension() == L".cso" || n == L"pipelines.bin" || n == L"pipelines.bin.tmp";
}
void cache_stats(int* n, double* mb)
{
    *n = 0; *mb = 0; std::error_code ec;
    for (auto& e : fs::directory_iterator(cache_dir(), ec))
        if (e.is_regular_file(ec) && is_cache_file(e.path())) { (*n)++; *mb += (double)e.file_size(ec) / (1024.0 * 1024.0); }
}
int cache_clear()
{
    int n = 0; std::error_code ec;
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(cache_dir(), ec))
        if (e.is_regular_file(ec) && is_cache_file(e.path())) files.push_back(e.path());
    for (auto& f : files) if (fs::remove(f, ec)) n++;
    return n;
}

std::wstring lang_choice_name()
{
    if (g_lang_pick >= 0) return k_lang_name[g_lang_pick];
    return std::wstring(T(S_LANG_SYSTEM)) + L" (" + k_lang_name[g_lang] + L")";
}

void draw_avancado(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    const float w = W - SIDE - 64.0f;
    section(gr, T(S_ADV_LAUNCHER), x, y, w, head);
    text(gr, T(S_ADV_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, w, 20);
    y += 70;
    text(gr, T(S_LANG), x, y, body, C_TEXT, StringAlignmentNear, 240, 30);
    dropdown(gr, RectF(x + 250, y, 260, 30), lang_choice_name(), ID_DD_LANG, body);
    text(gr, T(S_LANG_HINT), x, y + 28, fsm, C_DIM, StringAlignmentNear, 240, 20);
    y += 90;
    section(gr, T(S_ADV_FILES), x, y, w, head);
    y += 46;
    button(gr, RectF(x, y, 280, 34), T(S_CLEAR_CACHE), ID_CACHE_CLEAR, body, false, !g.running);
    button(gr, RectF(x + 292, y, 150, 34), T(S_OPEN_FOLDER), ID_CACHE_OPEN, body);
    y += 46;
    std::wstring st = g.cache_msg;
    if (g.running) st = T(S_CACHE_RUNNING);
    else if (st.empty()) {
        int n; double mb; cache_stats(&n, &mb);
        if (n) { wchar_t b[256]; swprintf(b, 256, T(S_CACHE_SIZE_FMT), n, mb); st = b; }
        else st = T(S_CACHE_EMPTY);
    }
    text(gr, st, x, y, body, g.running ? C_RED_HI : C_TEXT, StringAlignmentNear, w, 24);
    text(gr, cache_dir().wstring(), x, y + 26, fsm, C_DIM, StringAlignmentNear, w, 20);
    text(gr, T(S_CACHE_HINT), x, y + 48, fsm, C_DIM, StringAlignmentNear, w, 20);
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

    FontFamily serif(family(true, g_lang)), sans(family(false, g_lang));
    Font fTitle(&serif, 22, FontStyleBold, UnitPixel), fTitle2(&serif, 17, FontStyleBold, UnitPixel);
    Font fNav(&sans, 16, FontStyleRegular, UnitPixel), fHead(&serif, 18, FontStyleRegular, UnitPixel);
    Font fBody(&sans, 14, FontStyleRegular, UnitPixel), fSmall(&sans, 12, FontStyleRegular, UnitPixel);
    Font fCode(&serif, 18, FontStyleBold, UnitPixel), fBig(&serif, 22, FontStyleBold, UnitPixel);
    Font fCol(&serif, 15, FontStyleBold, UnitPixel);

    if (g.page == P_INICIO) draw_inicio(gr, fHead, fBody, fCode);
    else if (g.page == P_JOGO) draw_jogo(gr, fHead, fBody, fSmall);
    else if (g.page == P_CONTROLES) draw_controles(gr, fHead, fBody, fSmall, fCol);
    else if (g.page == P_PATCHES) draw_patches(gr, fHead, fBody, fSmall);
    else draw_avancado(gr, fHead, fBody, fSmall);
    draw_sidebar(gr, fTitle, fTitle2, fNav);
    draw_play(gr, fBig, fSmall);

    /* open dropdown list on top */
    if (g.open_dd >= 0) {
        std::vector<std::wstring> items; std::vector<int> item_lang; int sel = 0;
        const float x = g_dd_anchor.X, y = g_dd_anchor.Y + g_dd_anchor.Height, bw = g_dd_anchor.Width;
        if (g.open_dd == ID_DD_MODE) { for (int i = 0; i < 3; i++) items.push_back(mode_name(i)); sel = g.s.mode; }
        else if (g.open_dd == ID_DD_RES) { for (int i = 0; i < 5; i++) items.push_back(res_name(i)); sel = g.s.res; }
        else if (g.open_dd == ID_DD_AA) { for (int i = 0; i < 3; i++) items.push_back(aa_name(i)); sel = g.s.aa; }
        else {   /* Sistema, then each language in its own name and script */
            items.push_back(T(S_LANG_SYSTEM)); item_lang.push_back(g_lang);
            for (int i = 0; i < LANG_COUNT; i++) {
                items.push_back(k_lang_name[k_lang_order[i]]); item_lang.push_back(k_lang_order[i]);
                if (k_lang_order[i] == g_lang_pick) sel = i + 1;
            }
        }
        const int n = (int)items.size(), vis = std::min(n, DD_VISIBLE);
        g.dd_scroll = std::max(0, std::min(g.dd_scroll, n - vis));
        const bool scroll = n > vis;
        const float iw = scroll ? bw - 16 : bw;
        RectF box(x, y, bw, vis * 28.0f);
        SolidBrush lb(Color(255, 34, 28, 28)); gr.FillRectangle(&lb, box);
        /* remove underlying hits so clicks land on the list */
        g.hits.clear();
        hit(box.X, box.Y, box.Width, box.Height, ID_DD_LIST);
        for (int r = 0; r < vis; r++) {
            const int i = g.dd_scroll + r;
            RectF ir(x, y + r * 28.0f, iw, 28);
            bool hot = g.hover == ID_DD_ITEM + i;
            if (hot || i == sel) { SolidBrush hb(hot ? Color(255, 70, 24, 24) : Color(255, 46, 30, 30)); gr.FillRectangle(&hb, ir); }
            if (!item_lang.empty()) {
                FontFamily ff(family(false, item_lang[i])); Font f(&ff, 14, FontStyleRegular, UnitPixel);
                text(gr, items[i], ir.X + 8, ir.Y, f, C_TEXT, StringAlignmentNear, ir.Width - 12, ir.Height);
            } else
                text(gr, items[i], ir.X + 8, ir.Y, fBody, C_TEXT, StringAlignmentNear, ir.Width - 12, ir.Height);
            hit(ir.X, ir.Y, ir.Width, ir.Height, ID_DD_ITEM + i);
        }
        if (scroll) {   /* scroll bar: arrows at the ends, a thumb between */
            const float sx = x + bw - 16, top = y, bot = y + vis * 28.0f;
            SolidBrush tb(Color(255, 26, 22, 22)); gr.FillRectangle(&tb, sx, top, 16.0f, bot - top);
            SolidBrush ab(C_DIM), ah(C_TEXT);
            PointF up[3] = { PointF(sx + 4, top + 11), PointF(sx + 12, top + 11), PointF(sx + 8, top + 5) };
            PointF dn[3] = { PointF(sx + 4, bot - 11), PointF(sx + 12, bot - 11), PointF(sx + 8, bot - 5) };
            gr.FillPolygon(g.hover == ID_DD_UP ? &ah : &ab, up, 3);
            gr.FillPolygon(g.hover == ID_DD_DOWN ? &ah : &ab, dn, 3);
            hit(sx, top, 16, 16, ID_DD_UP);
            hit(sx, bot - 16, 16, 16, ID_DD_DOWN);
            const float track = (bot - top) - 32, th = std::max(20.0f, track * vis / n);
            const float ty = top + 16 + (track - th) * g.dd_scroll / (float)(n - vis);
            SolidBrush thumb(Color(255, 90, 80, 78)); gr.FillRectangle(&thumb, sx + 4, ty, 8.0f, th);
        }
        Pen lp(C_RED, 1); gr.DrawRectangle(&lp, box.X, box.Y, box.Width - 1, box.Height - 1);
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
    if (!folder && filter) { COMDLG_FILTERSPEC fs_[2] = { { filter_name, filter }, { T(S_ALL_FILES), L"*.*" } }; dlg->SetFileTypes(2, fs_); }
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
    for (int i = 0; i < NPATCHES; i++) ps3_patch_set(k_patches[i].id, g.s.patch[i]);
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
        if (id == ID_DD_UP || id == ID_DD_DOWN) { g.dd_scroll += id == ID_DD_UP ? -1 : 1; InvalidateRect(g.hwnd, NULL, FALSE); return; }
        if (id == ID_DD_LIST) return;
        if (id >= ID_DD_ITEM && id < ID_DD_ITEM + 64) {
            int i = id - ID_DD_ITEM;
            if (g.open_dd == ID_DD_MODE) g.s.mode = i;
            else if (g.open_dd == ID_DD_RES) g.s.res = i;
            else if (g.open_dd == ID_DD_AA) g.s.aa = i;
            else set_lang_pick(i == 0 ? -1 : k_lang_order[i - 1]);
            g.open_dd = -1; changed(); return;
        }
        g.open_dd = -1; InvalidateRect(g.hwnd, NULL, FALSE); return;
    }
    if (g.cap != CAP_NONE) { g.cap = CAP_NONE; g.cap_action = -1; }
    if (id >= ID_NAV && id < ID_NAV + P_COUNT) { g.page = (Page)(id - ID_NAV); g.cache_msg.clear(); }
    else if (id >= ID_PATCH && id < ID_PATCH + NPATCHES) { g.s.patch[id - ID_PATCH] ^= true; changed(); }
    else if (id == ID_DD_MODE || id == ID_DD_RES || id == ID_DD_AA || id == ID_DD_LANG) {
        g.open_dd = id; g.dd_scroll = 0;
        if (id == ID_DD_LANG && g_lang_pick >= 0)   /* open with the chosen language in view */
            for (int i = 0; i < LANG_COUNT; i++)
                if (k_lang_order[i] == g_lang_pick) g.dd_scroll = std::max(0, i + 1 - DD_VISIBLE / 2);
    }
    else if (id == ID_CACHE_CLEAR && !g.running) g.cache_msg = Tf(S_CACHE_CLEARED_FMT, cache_clear());
    else if (id == ID_CACHE_OPEN) { std::error_code ec; fs::create_directories(cache_dir(), ec); open_in_explorer(cache_dir().wstring()); }
    else if (id == ID_GAME_BROWSE) { auto p = pick(true, g.s.game, nullptr, nullptr); if (!p.empty()) { g.s.game = p; changed(); } }
    else if (id == ID_RAP_BROWSE) { auto p = pick(false, g.s.rap, T(S_RAP_FILTER), L"*.rap"); if (!p.empty()) { g.s.rap = p; changed(); } }
    else if (id == ID_GAME_OPEN) open_in_explorer(g.s.game);
    else if (id == ID_RAP_OPEN) open_in_explorer(g.s.rap);
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
        if (!ok) { g.page = P_JOGO; g.play_msg = T(S_SET_FOLDER_FIRST); }
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
    case WM_MOUSEWHEEL:
        if (g.open_dd >= 0) {
            g.dd_scroll += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
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
                st = (p.kind == PAD_KIND_XINPUT) ? L"Xbox"
                   : (p.kind == PAD_KIND_DS4) ? L"DualShock 4" : L"DualSense";
        }
        int kind = 0;
        for (int i = 0; i < PAD_PHYS_MAX && !kind; i++) { PadPhys p; if (pad_phys_poll(i, &p)) kind = p.kind; }
        if (const char* pv = getenv("CV_PAD_PREVIEW")) {   /* show a model without that controller (1..3) */
            kind = atoi(pv); cnt = kind ? 1 : 0;
            st = kind == PAD_KIND_XINPUT ? L"Xbox" : kind == PAD_KIND_DS4 ? L"DualShock 4" : kind ? L"DualSense" : L"";
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
    std::string pl;
    for (int i = 0; i < NPATCHES; i++)
        if (s.patch[i]) pl += (pl.empty() ? "" : ",") + std::string(k_patches[i].id);
    set_env("PS3_PATCHES", pl);
    std::string bm, km;
    for (int a = 0; a < PA_COUNT; a++) {
        bm += std::string(k_pad_action_id[a]) + "=" + pad_phys_id(s.btn[a]) + (a + 1 < PA_COUNT ? "," : "");
        km += std::string(k_pad_action_id[a]) + "=" + std::to_string(s.key[a]) + (a + 1 < PA_COUNT ? "," : "");
    }
    set_env("PAD_BTNMAP", bm);
    set_env("PAD_KEYMAP", km);
    /* Sound effects decode through the runtime's own ATRAC3 plug-in, so no
     * PS3 firmware is needed even when a dev_flash path is configured. */
    set_env_default("PS3_ATRAC_STANDIN", "1");
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
