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
#include "patch_files.h"
#include "cv_version.h"
#include "cv_srt.h"
#include "cv_srt_i18n.h"
#include "cv_launcher_controls.h"
#include "log_viewer.h"
#include "cv_test_tools.h"

/* button_icons.cpp: the keycaps' short labels and font, shared with the game. */
std::wstring cv_key_cap_label(int vk);
const wchar_t* cv_key_cap_font(void);

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
void  rsx_d3d12_set_color_adjust(int brightness, int contrast);
void  rsx_d3d12_set_saturation(int saturation);
void  rsx_d3d12_set_sharpness(int pct);
void  rsx_d3d12_close(void);
void* rsx_d3d12_window(void);
extern void (*g_rsx_menu_hook)(void);
extern void (*g_rsx_key_hook)(unsigned vk);
extern int g_pad_block;
void  perf_overlay_toast(const wchar_t* text, int on);
void  texpack_flags_changed(void);
void  cellpad_set_map(const int* btn, const int* key);
void  ps3_patch_set(const char* name, int on);
int   ps3_patch_writes(const char* id, const char* spec, int on);
}
namespace {

/* ---------------------------------------------------------------- palette */
const Color C_BG(255, 21, 17, 17), C_SIDE(255, 13, 11, 11), C_PANEL(255, 30, 25, 25),
            C_LINE(255, 70, 28, 28), C_TEXT(255, 225, 218, 210), C_DIM(255, 150, 140, 132),
            C_RED(255, 196, 28, 34), C_RED_HI(255, 230, 50, 50), C_HOVER(255, 38, 30, 30),
            C_FIELD(255, 26, 22, 22), C_FIELD_BD(255, 70, 60, 58), C_OK(255, 120, 190, 110);

/* The versions this port is built for, one executable each, side by side
 * (cv_version.h, build-release-jp.cmd). The launcher of either accepts both:
 * a folder of the other version starts that version's executable, which
 * reads the same settings and starts at once. */
struct GameVersion { const wchar_t* code; const char* elf_sha1; const wchar_t* exe; const wchar_t* name; };
const GameVersion k_versions[] = {
    { L"NPEB00553", "d0c9d3ab3a5ed12c80ab77fe73fa58d464083fa1", L"MyGameRecomp.exe",
      L"Resident Evil Code: Veronica X (PSN)" },
    { L"NPUB30467", "93d060e5019d877df39cd091c28efff920b98e49", L"MyGameRecomp_US.exe",
      L"Resident Evil Code: Veronica X (PSN US)" },
    { L"NPJB00135", "ad17bbfadf23937480ba0330616262e595dfaa78", L"MyGameRecomp_JP.exe",
      L"BIOHAZARD CODE: Veronica Kanzenban (PSN)" },
};
const wchar_t* GAME_CODE = L"" CV_TITLE_ID;
/* The text that names the compatible versions in the strings, replaced by
 * the one found where a message is about a particular folder. */
const wchar_t* CODES_TOKEN = L"NPEB00553 / NPJB00135 / NPUB30467";
const GameVersion* version_of(const std::wstring& code)
{
    for (const GameVersion& v : k_versions) if (code == v.code) return &v;
    return nullptr;
}
const GameVersion& own_version() { return *version_of(GAME_CODE); }
/* The executable for another version, beside this one; empty if absent. */
std::wstring sibling_exe(const GameVersion& v)
{
    wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL, self, MAX_PATH);
    const std::filesystem::path p = std::filesystem::path(self).parent_path() / v.exe;
    std::error_code ec;
    return std::filesystem::exists(p, ec) ? p.wstring() : L"";
}
std::wstring folder_code();   /* the chosen folder's TITLE_ID, defined below */
std::wstring with_code(std::wstring m, const std::wstring& code)
{
    for (size_t at; (at = m.find(CODES_TOKEN)) != std::wstring::npos; )
        m.replace(at, wcslen(CODES_TOKEN), code);
    return m;
}

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
    int fps = 0;           /* 0 the original 30; 1 unlocked 60 to measure maps (runs fast) */
    int aniso = 0;         /* anisotropic filtering: 0 off, 1-4 = 2x/4x/8x/16x */
    int brightness = 0;   /* -80..80, zero leaves the image unchanged */
    int contrast = 100;   /* 50..180 percent */
    int saturation = 100; /* 0..200 percent */
    int sharpness = 0;    /* FSR 1 RCAS, 0..100 percent in steps of 10; 0 off */
    bool patch[8] = {};    /* k_patches on/off */
    int layout = 2;        /* button icons: 0 PlayStation, 1 Xbox, 2 automatic, 3 keyboard */
    bool overlay = false;  /* the performance overlay (rsx_perf_overlay.h) */
    int overlay_items = 3; /* bits of k_overlay_items: FPS and frame time */
    int overlay_corner = 1, overlay_layout = 0;   /* 0 tl, 1 tr, 2 bl, 3 br; 0 a row, 1 a column */
    bool shader_opt[6] = { true, true, true, true, true, false };   /* k_shader_opts, Desempenho page */
    std::wstring tex_dir;              /* texture mods (Mods page); empty: "textures" beside the exe */
    std::wstring tex_dump_dir;         /* where dumps go; empty: <tex_dir>\<version>\dump */
    bool tex_replace = false, tex_dump = false;
    bool test_tools=false; // optional experimental map window, off by default
    bool srt=false;
    bool srt_parts[5]={true,true,true,true,false}; // name, health, time, stats, inventory
    int srt_enemies=3, srt_corner=0, srt_scale=100, srt_language=0, srt_opacity=70, srt_inventory_below=0, srt_window=0, srt_boss_bar=0; // 0 follow, 1 EN, 2 PT-BR; 3..13 other launcher languages
    std::wstring game, rap, fw, saves;
    std::wstring patch_dir;                 /* patch files (patch_files.h) */
    std::vector<std::string> patch_files_on; /* ids of the file patches turned on */
    int key[PA_COUNT][PAD_KEY_SLOTS];   /* 0: empty; the first is what the game's icons show */
    int btn[PA_COUNT][PAD_BTN_SLOTS];   /* -1: empty */
};
/* What the performance overlay can show, in its order; the ids are
 * PERF_OVERLAY's. The frame time now rides with FPS: "ms" keeps its bit so
 * saved settings still line up, but it has no box of its own. */
const struct { const char* id; const wchar_t* name; } k_overlay_items[] = {
    { "fps", L"FPS" }, { "ms", nullptr }, { "cpu", L"CPU" }, { "gpu", L"GPU" }, { "ram", L"RAM" }, { "vram", L"VRAM" } };
const int NOVERLAY = sizeof k_overlay_items / sizeof k_overlay_items[0];
struct ResOpt { const wchar_t* name; const char* scale; };
const ResOpt k_res[] = {
    { L"480p (854x480)", "0.6667" }, { L"Nativa (720p)", "1" }, { L"1080p (1920x1080)", "1.5" },
    { L"1440p (2560x1440)", "2" }, { L"4K (3840x2160)", "3" } };
const char* k_mode_env[] = { "window", "borderless", "fullscreen" };
std::wstring res_name(int i) { return i == 1 ? T(S_RES_NATIVE) : k_res[i].name; }
std::wstring mode_name(int i) { return T(S_MODE_WINDOW + i); }
std::wstring aa_name(int i) { return T(S_AA_OFF + i); }
std::wstring fps_name(int i) { return T(i ? S_FPS_60_DIAG : S_FPS_ORIG); }
std::wstring aniso_name(int i) { return i ? std::to_wstring(1 << i) + L"x" : T(S_AA_OFF); }
std::wstring sharp_name(int i) { return i ? std::to_wstring(i * 10) + L"%" : T(S_AA_OFF); }

/* Game patches (projects/code_veronica/patches.json, lifted as switchable
 * sites): id = the runtime flag name. */
struct PatchOpt { const char* id; int title, desc, credit; };
const PatchOpt k_patches[] = {
    { "cv_door_skip", S_PATCH_DOOR_T, S_PATCH_DOOR_D, S_PATCH_DOOR_C },
    { "cv_skip_logos", S_PATCH_LOGO_T, S_PATCH_LOGO_D, S_PATCH_LOGO_C },   /* boot_skip.cpp */
};
const int NPATCHES = sizeof k_patches / sizeof k_patches[0];

/* The SHADERS switches on the Desempenho page (read by the game at start):
 * the pipeline library (PSO_LIBRARY), loading the saved shaders while the
 * game runs or before it goes on (SHADER_PRELOAD), building new ones on the
 * shader workers (FP_ASYNC), loading the saved ones on several threads
 * (SHADER_PRELOAD_PARALLEL) and the note in the game's corner while they load
 * (SHADER_PRELOAD_NOTE, which the game reads again as it draws, so it changes
 * at once). All on by default. */
struct ShaderOpt { const wchar_t* key; int title, desc; };
const ShaderOpt k_shader_opts[] = {
    { L"cache_pipelines", S_SH_PSO_T, S_SH_PSO_D },
    { L"carregar_em_segundo_plano", S_SH_LOAD_T, S_SH_LOAD_D },
    { L"compilar_em_segundo_plano", S_SH_BUILD_T, S_SH_BUILD_D },
    { L"carregamento_paralelo", S_SH_PAR_T, S_SH_PAR_D },
    { L"mostrar_progresso", S_SH_NOTE_T, S_SH_NOTE_D },
    /* RSX_GPU_VFETCH: the vertex shader converts the guest's vertices. Off by
     * default until it has been through the whole game; read at start. */
    { L"vertices_na_gpu", S_VF_T, S_VF_D },
};
const int NSHADER_OPTS = sizeof k_shader_opts / sizeof k_shader_opts[0];

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
      L"Back", L"Start", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita", L"Guide", L"-" } };
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
    const auto path = ini_path();
    /* A fresh profile must be Unicode too, otherwise Windows converts a
     * selected save path to the active code page and can lose characters. */
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        const wchar_t bom = 0xFEFF; DWORD written = 0;
        WriteFile(file, &bom, sizeof bom, &written, NULL);
        CloseHandle(file);
    }
    WritePrivateProfileStringW(sec, key, v.c_str(), path.c_str());
}
std::wstring widen(const char* s) { return std::wstring(s, s + strlen(s)); }
std::wstring env_w(const char* n) { const char* e = getenv(n); return e ? fs::path(e).wstring() : L""; }

std::wstring find_rap(const std::wstring& dir, const std::wstring& code = GAME_CODE)
{
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return L"";
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::wstring n = e.path().filename().wstring();
        if (n.find(code) != std::wstring::npos && e.path().extension() == L".rap")
            return e.path().wstring();
    }
    return L"";
}

std::wstring rap_for_game(const Settings& s, const std::wstring& code)
{
    std::error_code ec;
    if (!s.rap.empty() && fs::exists(s.rap, ec) && fs::path(s.rap).filename().wstring().find(code) != std::wstring::npos)
        return s.rap;
    for (const std::wstring& dir : { fs::path(s.rap).parent_path().wstring(), s.game, env_w("PS3_RAP_DIR") }) {
        const auto found = find_rap(dir, code);
        if (!found.empty()) return found;
    }
    return s.rap;  /* also allow a manually chosen licence with a renamed file */
}

Settings load_settings()
{
    Settings s;
    s.mode   = _wtoi(ini_get(L"tela", L"modo", L"0").c_str());
    s.res    = _wtoi(ini_get(L"tela", L"resolucao", L"1").c_str());
    s.aa     = _wtoi(ini_get(L"tela", L"antialias", L"0").c_str());
    s.fps    = _wtoi(ini_get(L"tela", L"fps", L"0").c_str()) == 1;
    s.aniso  = std::clamp(_wtoi(ini_get(L"tela", L"anisotropico", L"0").c_str()), 0, 4);
    s.brightness = _wtoi(ini_get(L"tela", L"brilho", L"0").c_str());
    s.contrast = _wtoi(ini_get(L"tela", L"contraste", L"100").c_str());
    s.saturation = _wtoi(ini_get(L"tela", L"saturacao", L"100").c_str());
    s.sharpness = std::clamp(_wtoi(ini_get(L"tela", L"nitidez", L"0").c_str()) / 10 * 10, 0, 100);
    s.layout = _wtoi(ini_get(L"controles", L"layout", L"2").c_str());
    { std::wstring l = ini_get(L"launcher", L"idioma", L"system");
      int pick = -1;
      for (int i = 0; i < LANG_COUNT; i++) if (l == widen(k_lang_id[i])) pick = i;
      set_lang_pick(pick); }
    for (int i = 0; i < NPATCHES; i++)
        s.patch[i] = _wtoi(ini_get(L"patches", widen(k_patches[i].id).c_str(), L"0").c_str()) != 0;
    if (s.mode < 0 || s.mode > 2) s.mode = 0;
    if (s.res < 0 || s.res > 4) s.res = 1;
    if (s.aa < 0 || s.aa > 2) s.aa = 0;
    s.brightness = std::clamp(s.brightness, -80, 80);
    s.contrast = std::clamp(s.contrast, 50, 180);
    s.saturation = std::clamp(s.saturation, 0, 200);
    if (s.layout < 0 || s.layout > 3) s.layout = 2;
    /* First run: take the paths the old launcher script set, if any. */
    std::wstring def_game = env_w("PS3_VFS_ROOT");
    if (def_game.empty()) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
        std::error_code ec;
        for (fs::path d = fs::path(exe).parent_path(); !d.empty() && d != d.root_path(); d = d.parent_path()) {
            fs::path g = d / L"JOGO" / GAME_CODE;   /* this checkout keeps the game in <root>/JOGO */
            if (fs::exists(g / L"USRDIR" / L"EBOOT.BIN", ec) || fs::exists(g / L"USRDIR" / L"EBOOT.elf", ec))
            { def_game = g.wstring(); break; }
        }
    }
    s.srt = _wtoi(ini_get(L"srt", L"mostrar", L"0").c_str()) != 0;
    { const wchar_t* keys[]={L"nome",L"vida",L"tempo",L"contadores",L"inventario"};
      for(int i=0;i<5;i++)s.srt_parts[i]=_wtoi(ini_get(L"srt",keys[i],i<4?L"1":L"0").c_str())!=0; }
    s.srt_enemies=std::clamp(_wtoi(ini_get(L"srt",L"inimigos",L"3").c_str()),0,99);
    if(s.srt_enemies>5&&s.srt_enemies!=99)s.srt_enemies=5;
    s.srt_corner=std::clamp(_wtoi(ini_get(L"srt",L"posicao",L"0").c_str()),0,3);
    s.srt_scale=std::clamp(_wtoi(ini_get(L"srt",L"tamanho",L"100").c_str()),50,200);
    s.test_tools=_wtoi(ini_get(L"testes",L"mapas",L"0").c_str())!=0;
    s.srt_opacity=std::clamp(_wtoi(ini_get(L"srt",L"opacidade",L"70").c_str()),0,100);
    s.srt_inventory_below=std::clamp(_wtoi(ini_get(L"srt",L"inventario_embaixo",L"0").c_str()),0,1);
    s.srt_window=std::clamp(_wtoi(ini_get(L"srt",L"janela",L"0").c_str()),0,1);
    s.srt_boss_bar=std::clamp(_wtoi(ini_get(L"srt",L"barra_chefe",L"0").c_str()),0,2);
    s.srt_language=std::clamp(_wtoi(ini_get(L"srt",L"idioma",L"0").c_str()),0,int(LANG_COUNT));
    s.overlay = _wtoi(ini_get(L"overlay", L"mostrar", L"0").c_str()) != 0;
    for (int i = 0; i < NSHADER_OPTS; i++)
        s.shader_opt[i] = _wtoi(ini_get(L"shaders", k_shader_opts[i].key, i == 5 ? L"0" : L"1").c_str()) != 0;
    s.tex_dir = ini_get(L"texturas", L"pasta", L"");
    s.tex_dump_dir = ini_get(L"texturas", L"pasta_extraidas", L"");
    s.tex_replace = _wtoi(ini_get(L"texturas", L"substituir", L"0").c_str()) != 0;
    s.tex_dump = _wtoi(ini_get(L"texturas", L"extrair", L"0").c_str()) != 0;
    s.overlay_items = _wtoi(ini_get(L"overlay", L"itens", L"3").c_str()) & ((1 << NOVERLAY) - 1);
    s.overlay_corner = std::clamp(_wtoi(ini_get(L"overlay", L"posicao", L"1").c_str()), 0, 3);
    s.overlay_layout = std::clamp(_wtoi(ini_get(L"overlay", L"disposicao", L"0").c_str()), 0, 1);
    s.patch_dir = ini_get(L"pastas", L"patches", L"");
    {
        const std::wstring on = ini_get(L"patches", L"arquivos", L"");
        std::string cur;
        for (wchar_t c : on) {
            if (c == L',') { if (!cur.empty()) s.patch_files_on.push_back(cur); cur.clear(); }
            else cur += (char)c;
        }
        if (!cur.empty()) s.patch_files_on.push_back(cur);
    }
    s.game = ini_get(L"pastas", L"jogo", def_game);
    s.saves = ini_get(L"pastas", L"saves", L"");
    s.rap  = ini_get(L"pastas", L"rap", find_rap(env_w("PS3_RAP_DIR")));
    /* No page for it any more: without a firmware tree the runtime serves its
     * own flashATRAC.pic stand-in. A path in the ini still reaches PS3_DEV_FLASH. */
    s.fw   = ini_get(L"pastas", L"firmware", env_w("PS3_DEV_FLASH"));
    /* tecla_cross / botao_cross: the first slot; tecla_cross_2 ... the others */
    for (int a = 0; a < PA_COUNT; a++) {
        for (int k = 0; k < PAD_KEY_SLOTS; k++) {
            std::wstring kid = L"tecla_" + widen(k_pad_action_id[a]) + (k ? L"_" + std::to_wstring(k + 1) : L"");
            s.key[a][k] = _wtoi(ini_get(L"controles", kid.c_str(), std::to_wstring(k ? 0 : k_pad_default_key[a])).c_str());
        }
        for (int k = 0; k < PAD_BTN_SLOTS; k++) {
            std::wstring bid = L"botao_" + widen(k_pad_action_id[a]) + (k ? L"_" + std::to_wstring(k + 1) : L"");
            std::wstring b = ini_get(L"controles", bid.c_str(), widen(pad_phys_id(k ? -1 : k_pad_default_btn[a])));
            std::string nb(b.begin(), b.end());
            int pb = pad_phys_from_id(nb.c_str());
            s.btn[a][k] = pb >= 0 || nb == "none" ? pb : (k ? -1 : k_pad_default_btn[a]);
        }
    }
    return s;
}

void save_settings(const Settings& s)
{
    ini_set(L"tela", L"modo", std::to_wstring(s.mode));
    ini_set(L"tela", L"resolucao", std::to_wstring(s.res));
    ini_set(L"tela", L"antialias", std::to_wstring(s.aa));
    ini_set(L"tela", L"fps", std::to_wstring(s.fps));
    ini_set(L"tela", L"anisotropico", std::to_wstring(s.aniso));
    ini_set(L"tela", L"brilho", std::to_wstring(s.brightness));
    ini_set(L"tela", L"contraste", std::to_wstring(s.contrast));
    ini_set(L"tela", L"saturacao", std::to_wstring(s.saturation));
    ini_set(L"tela", L"nitidez", std::to_wstring(s.sharpness));
    ini_set(L"controles", L"layout", std::to_wstring(s.layout));
    ini_set(L"launcher", L"idioma", g_lang_pick < 0 ? std::wstring(L"system") : widen(k_lang_id[g_lang_pick]));
    for (int i = 0; i < NPATCHES; i++)
        ini_set(L"patches", widen(k_patches[i].id).c_str(), s.patch[i] ? L"1" : L"0");
    ini_set(L"testes",L"mapas",s.test_tools?L"1":L"0");
    ini_set(L"srt", L"mostrar",s.srt?L"1":L"0");
    { const wchar_t* keys[]={L"nome",L"vida",L"tempo",L"contadores",L"inventario"};
      for(int i=0;i<5;i++)ini_set(L"srt",keys[i],s.srt_parts[i]?L"1":L"0"); }
    ini_set(L"srt",L"inimigos",std::to_wstring(s.srt_enemies));
    ini_set(L"srt",L"posicao",std::to_wstring(s.srt_corner));
    ini_set(L"srt",L"tamanho",std::to_wstring(s.srt_scale));
    ini_set(L"srt",L"idioma",std::to_wstring(s.srt_language));
    ini_set(L"srt",L"opacidade",std::to_wstring(s.srt_opacity));
    ini_set(L"srt",L"inventario_embaixo",std::to_wstring(s.srt_inventory_below));
    ini_set(L"srt",L"janela",std::to_wstring(s.srt_window));
    ini_set(L"srt",L"barra_chefe",std::to_wstring(s.srt_boss_bar));
    ini_set(L"overlay", L"mostrar", s.overlay ? L"1" : L"0");
    ini_set(L"overlay", L"itens", std::to_wstring(s.overlay_items));
    ini_set(L"overlay", L"posicao", std::to_wstring(s.overlay_corner));
    ini_set(L"overlay", L"disposicao", std::to_wstring(s.overlay_layout));
    for (int i = 0; i < NSHADER_OPTS; i++)
        ini_set(L"shaders", k_shader_opts[i].key, s.shader_opt[i] ? L"1" : L"0");
    ini_set(L"texturas", L"pasta", s.tex_dir);
    ini_set(L"texturas", L"pasta_extraidas", s.tex_dump_dir);
    ini_set(L"texturas", L"substituir", s.tex_replace ? L"1" : L"0");
    ini_set(L"texturas", L"extrair", s.tex_dump ? L"1" : L"0");
    ini_set(L"pastas", L"patches", s.patch_dir);
    {
        std::wstring on;
        for (const std::string& id : s.patch_files_on) on += (on.empty() ? L"" : L",") + std::wstring(id.begin(), id.end());
        ini_set(L"patches", L"arquivos", on);
    }
    ini_set(L"pastas", L"jogo", s.game);
    ini_set(L"pastas", L"rap", s.rap);
    ini_set(L"pastas", L"saves", s.saves);
    ini_set(L"pastas", L"firmware", s.fw);
    for (int a = 0; a < PA_COUNT; a++) {
        for (int k = 0; k < PAD_KEY_SLOTS; k++)
            ini_set(L"controles", (L"tecla_" + widen(k_pad_action_id[a]) + (k ? L"_" + std::to_wstring(k + 1) : L"")).c_str(),
                    std::to_wstring(s.key[a][k]));
        for (int k = 0; k < PAD_BTN_SLOTS; k++)
            ini_set(L"controles", (L"botao_" + widen(k_pad_action_id[a]) + (k ? L"_" + std::to_wstring(k + 1) : L"")).c_str(),
                    widen(pad_phys_id(s.btn[a][k])));
    }
}

/* The recompiled code is the translation of one exact EBOOT: the PSN European
 * release NPEB00553, APP_VER 01.00. PARAM.SFO names the title and version
 * cheaply enough to check on every repaint; the ELF's SHA-1 is checked once
 * on Jogar (prepare_eboot), since an update can change the EBOOT alone. */
const char* GAME_VERSION = "01.00";
const char* GAME_ELF_SHA1 = own_version().elf_sha1;

/* One string field of a PARAM.SFO, or "" if the file or the key is missing. */
std::string sfo_field(const fs::path& sfo, const char* key)
{
    FILE* f = _wfopen(sfo.c_str(), L"rb");
    if (!f) return "";
    std::vector<unsigned char> d(64 * 1024);
    d.resize(fread(d.data(), 1, d.size(), f));
    fclose(f);
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? (uint32_t)(d[o] | d[o+1] << 8 | d[o+2] << 16 | d[o+3] << 24) : 0u; };
    if (d.size() < 20 || (d[0] || memcmp(&d[1], "PSF", 3))) return "";
    const uint32_t keys = u32(8), data = u32(12), n = u32(16);
    for (uint32_t i = 0; i < n && 20 + 16 * i + 16 <= d.size(); i++) {
        const size_t e = 20 + 16 * (size_t)i;
        const size_t ko = keys + (d[e] | d[e+1] << 8), vo = data + u32(e + 12), len = u32(e + 4);
        if (ko >= d.size() || vo + len > d.size()) continue;
        if (strncmp((const char*)&d[ko], key, d.size() - ko) == 0)
            return std::string((const char*)&d[vo], strnlen((const char*)&d[vo], len));
    }
    return "";
}

/* Validation of the three paths. */
std::wstring check_game(const std::wstring& g, bool* ok)
{
    std::error_code ec; *ok = false;
    if (g.empty()) return T(S_CHK_GAME_CHOOSE);
    const fs::path root(g);
    const bool bin = fs::exists(root / L"USRDIR" / L"EBOOT.BIN", ec);
    if (!bin && !fs::exists(root / L"USRDIR" / L"EBOOT.elf", ec)) return T(S_CHK_GAME_BAD);
    const std::string id = sfo_field(root / L"PARAM.SFO", "TITLE_ID");
    const std::string ver = sfo_field(root / L"PARAM.SFO", "APP_VER");
    if (id.empty()) return T(S_CHK_GAME_NO_SFO);
    const std::wstring wid = widen(id.c_str());
    const GameVersion* v = version_of(wid);
    if (!v || (wid != GAME_CODE && sibling_exe(*v).empty())) {
        std::wstring m = T(S_CHK_GAME_OTHER_FMT); m.replace(m.find(L"%s"), 2, wid); return m;
    }
    if (ver != GAME_VERSION) {
        std::wstring m = with_code(T(S_CHK_GAME_VER_FMT), wid); m.replace(m.find(L"%s"), 2, widen(ver.c_str())); return m;
    }
    *ok = true;
    return with_code(T(bin ? S_CHK_GAME_OK : S_CHK_GAME_ELF), wid);
}
std::wstring check_rap(const std::wstring& r, bool* ok)
{
    if (folder_code() == L"NPJB00135") {
        *ok = true;
        return T(S_CHK_RAP_NOT_REQUIRED);
    }
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
enum Page { P_INICIO, P_JOGO, P_CONTROLES, P_PATCHES, P_MODS, P_SRT, P_DESEMPENHO, P_AVANCADO, P_COUNT };
enum Capture { CAP_NONE, CAP_KEY, CAP_BTN };

struct Hit { RectF r; int id; };
struct UI {
    HWND hwnd = nullptr;
    Settings s;
    Page page = P_INICIO;
    int hover = -1;
    int srt_drag = -1; bool srt_drag_dirty = false;
    int open_dd = -1;            /* open dropdown id */
    Capture cap = CAP_NONE; int cap_action = -1, cap_slot = 0; ULONGLONG cap_since = 0;
    std::vector<FilePatch> fpatches;   /* read from s.patch_dir when it changes or the page opens */
    int fpatch_files = 0; float patch_scroll = 0, patch_scroll_max = 0;
    std::wstring log_msg;   /* after Copy / Save on the Advanced page */
    uint32_t pad_prev[PAD_PHYS_MAX] = {};
    uint32_t test_combo_prev=0;
    std::vector<Hit> hits;
    std::wstring play_msg;
    int result = 0;              /* 1 = play */
    Bitmap* banner = nullptr;
    std::wstring pad_status;       /* connected model ("" = none) */
    int pad_count = 0;
    int pad_kind = 0;              /* PAD_KIND_* of the first connected pad */
    bool running = false;          /* Jogar pressed: the window stays as the options menu */
    int applied_mode = -1, applied_res = -1, applied_aa = -1;
    uint32_t combo_prev = 0, tex_combo_prev = 0, srt_combo_prev = 0;
    std::string eboot;
    std::wstring cache_msg;        /* result of the last "clear shader cache" */
    int tex_rep = -1, tex_dumped = 0;   /* PNGs in the texture folder (-1: not counted yet) */
    std::wstring tex_root_name;    /* the folder replacements are read from */
    int dd_scroll = 0;             /* first visible row of the open dropdown list */
};
UI g;

/* ids */
enum {
    ID_NAV = 100,               /* + page */
    ID_DD_MODE = 200, ID_DD_RES = 201, ID_DD_AA = 202, ID_DD_LANG = 203, ID_DD_FPS = 204, ID_DD_ANISO = 205, ID_DD_SHARP = 206, ID_DD_ITEM = 300,  /* + item index */
    ID_DD_UP = 390, ID_DD_DOWN = 391,
    ID_PLAY = 400, ID_RESUME, ID_QUIT,
    ID_GAME_BROWSE = 500, ID_GAME_OPEN, ID_RAP_BROWSE, ID_RAP_OPEN, ID_SAVE_BROWSE, ID_SAVE_OPEN, ID_SAVE_DEFAULT,
    ID_LAYOUT_PS = 600, ID_LAYOUT_XB, ID_LAYOUT_AUTO, ID_LAYOUT_KB,
    ID_KEY = 700, ID_BTN = 800, ID_RESTORE = 900, ID_PATCH = 1000,  /* + index (keys and buttons: action * slots + slot) */
    ID_TEST_ENABLE=2500, ID_TEST_OPEN,
    ID_CACHE_CLEAR = 1100, ID_CACHE_OPEN,
    ID_KEY_CLEAR = 1200, ID_BTN_CLEAR = 1300,                      /* + action * slots + slot */
    ID_OVERLAY = 1400, ID_OVERLAY_ITEM = 1410,                     /* + item */
    ID_PATCH_DIR_BROWSE = 1500, ID_PATCH_DIR_OPEN, ID_FPATCH = 1600,  /* + file patch index */
    ID_LOG_VIEW = 2100, ID_LOG_COPY, ID_LOG_SAVE, ID_LOG_FOLDER,
    ID_SHADER_OPT = 2200,                                          /* + k_shader_opts index */
    ID_SRT=2400, ID_SRT_PART=2410, ID_DD_SRT_ENEMIES=2420, ID_DD_SRT_CORNER, ID_DD_SRT_LANGUAGE, ID_SRT_SCALE, ID_SRT_OPACITY,
    ID_DD_OV_CORNER = 2450, ID_DD_OV_LAYOUT, ID_DD_SRT_INVPOS, ID_DD_SRT_WINDOW, ID_DD_SRT_BOSSBAR,
    ID_TEX_BROWSE = 2300, ID_TEX_OPEN, ID_TEX_REPLACE, ID_TEX_DUMP, ID_TEX_DUMP_BROWSE, ID_TEX_DUMP_OPEN,
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
/* A keycap like the ones the game shows in keyboard mode (button_icons.cpp):
 * light grey-blue, dark border, the key's short label. */
void key_glyph(Graphics& gr, int vk, float x, float y, float s)
{
    const float r = 3.0f, in = 1.5f, w = s - 2 * in, h = s - 2 * in;
    GraphicsPath p;
    p.AddArc(x + in, y + in, 2 * r, 2 * r, 180, 90);
    p.AddArc(x + in + w - 2 * r, y + in, 2 * r, 2 * r, 270, 90);
    p.AddArc(x + in + w - 2 * r, y + in + h - 2 * r, 2 * r, 2 * r, 0, 90);
    p.AddArc(x + in, y + in + h - 2 * r, 2 * r, 2 * r, 90, 90);
    p.CloseFigure();
    SolidBrush fill(Color(255, 205, 211, 223)); gr.FillPath(&fill, &p);
    Pen border(Color(255, 90, 94, 104), 1.2f); gr.DrawPath(&border, &p);
    if (vk <= 0) return;
    const std::wstring t = cv_key_cap_label(vk);
    FontFamily ff(cv_key_cap_font());
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    for (float px = s * 0.6f; ; px -= 0.5f) {
        Font f(&ff, px, FontStyleRegular, UnitPixel);
        RectF m; gr.MeasureString(t.c_str(), -1, &f, PointF(0, 0), &sf, &m);
        if (m.Width <= w - 3 || px <= 6) {
            SolidBrush ink(Color(255, 20, 22, 28));
            gr.DrawString(t.c_str(), -1, &f, RectF(x + in, y + in, w, h), &sf, &ink);
            return;
        }
    }
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
void draw_sidebar(Graphics& gr, const Font& title, const Font& title2, const Font& nav, const Font& footer_font)
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
    text(gr, T(S_CREDIT_KASTIEL), 22, H - 42.0f, footer_font, C_DIM,
         StringAlignmentNear, SIDE - 44.0f, 22);
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

void draw_inicio(Graphics& gr, const Font& head, const Font& body, const Font& code, const Font& fsm)
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
    { const GameVersion* v = version_of(folder_code());
      if (!v) v = &own_version();
      text(gr, std::wstring(v->code) + L"  ·  " + v->name, cx + 32, bh - 46, code, C_RED_HI); }

    float x = cx + 32, y = bh + 14;
    section(gr, T(S_QUICK), x, y, cw - 64, head);
    y += 44;
    text(gr, T(S_DISPLAY_MODE), x, y, body, C_TEXT, StringAlignmentNear, 200, 30);
    dropdown(gr, RectF(x + 210, y, 200, 30), mode_name(g.s.mode), ID_DD_MODE, body);
    text(gr, T(S_FRAME_LIMIT), x + 450, y, body, C_TEXT, StringAlignmentNear, 180, 30);
    dropdown(gr, RectF(x + 630, y, 180, 30), fps_name(g.s.fps), g.running ? -1 : ID_DD_FPS, body, !g.running);
    y += 44;
    text(gr, T(S_RENDER_RES), x, y, body, C_TEXT, StringAlignmentNear, 205, 30);
    dropdown(gr, RectF(x + 210, y, 200, 30), res_name(g.s.res), ID_DD_RES, body);
    text(gr, T(S_AA), x + 450, y, body, C_TEXT, StringAlignmentNear, 180, 30);
    dropdown(gr, RectF(x + 630, y, 180, 30), aa_name(g.s.aa), ID_DD_AA, body);
    y += 44;   /* in the pipelines' samplers: chosen before the game starts */
    text(gr, T(S_ANISO), x, y, body, C_TEXT, StringAlignmentNear, 205, 30);
    dropdown(gr, RectF(x + 210, y, 200, 30), aniso_name(g.s.aniso), g.running ? -1 : ID_DD_ANISO, body, !g.running);
    text(gr, T(S_SHARPNESS), x + 450, y, body, C_TEXT, StringAlignmentNear, 180, 30);   /* live */
    dropdown(gr, RectF(x + 630, y, 180, 30), sharp_name(g.s.sharpness / 10), ID_DD_SHARP, body);
    y += 54;
    bool o1, o2;
    std::wstring m1 = check_game(g.s.game, &o1), m2 = check_rap(g.s.rap, &o2);
    const std::wstring* ms[2] = { &m1, &m2 }; bool oks[2] = { o1, o2 };
    for (int i = 0; i < 2; i++) {
        text(gr, (oks[i] ? L"✔  " : L"✖  ") + *ms[i], x, y + i * 24.0f, body, oks[i] ? C_OK : C_RED_HI,
             StringAlignmentNear, cw - 64, 22);
    }
    /* Keep the supported releases visible independently of the selected game. */
    text(gr, T(S_SUPPORTED_VERSIONS), x, H - 162.0f, fsm, C_DIM,
         StringAlignmentNear, cw - 64, 20);
    const std::wstring versions = std::wstring(T(S_REGION_EU)) + L": NPEB00553   ·   "
        + T(S_REGION_JP) + L": NPJB00135   ·   " + T(S_REGION_US) + L": NPUB30467";
    text(gr, versions, x, H - 136.0f, fsm, C_DIM,
         StringAlignmentNear, cw - 64, 38);
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


/* This is the existing save root relative to the launch directory, not the
 * game installation. Picking a folder changes only the next game session. */
fs::path save_folder()
{
    return fs::absolute(g.s.saves.empty()
        ? fs::path(L"gamedata/dev_hdd0/home/00000001/savedata") : fs::path(g.s.saves));
}
std::string save_utf8(const fs::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}
bool prepare_save_folder()
{
    const fs::path dir = save_folder();
    if (save_utf8(dir).size() > 800) return false;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec)) return false;
    /* An exclusive, automatically deleted probe: never overwrite a save. */
    const fs::path probe = dir / (L".cv-write-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
void save_note(Graphics& gr, const wchar_t* message, float x, float y, const Font& font)
{
    StringFormat sf;
    sf.SetLineAlignment(StringAlignmentNear);
    if (g_lang == LANG_AR) sf.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
    SolidBrush brush(C_DIM);
    gr.DrawString(message, -1, &font, RectF(x, y, 655, 52), &sf, &brush);
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
    /* Keep the regional licence note visible even with EU selected. Wrap
     * translations rather than truncating the Japanese/no-RAP explanation. */
    {
        StringFormat sf;
        sf.SetLineAlignment(StringAlignmentNear);
        sf.SetTrimming(StringTrimmingWord);
        if (g_lang == LANG_AR) sf.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
        SolidBrush brush(C_TEXT);
        gr.DrawString(T(S_LICENSE_JP_NOTICE), -1, &fsm,
                      RectF(x + 175, y + 84, W - x - 203, 72), &sf, &brush);
    }
    y += 170;
    path_row(gr, x, y, T(S_SAVE_FOLDER), save_folder().wstring(),
             T(g.s.saves.empty() ? S_SAVE_USING_DEFAULT : S_SAVE_SELECTED), true,
             ID_SAVE_BROWSE, ID_SAVE_OPEN, body, fsm);
    save_note(gr, T(S_SAVE_HINT), x + 175, y + 62, fsm);
    save_note(gr, T(S_SAVE_NO_MOVE), x + 175, y + 114, fsm);
    button(gr, RectF(x + 175, y + 168, 330, 32), T(S_SAVE_DEFAULT), ID_SAVE_DEFAULT,
           body, false, !g.s.saves.empty());
    button(gr,RectF(x,y+228,390,32),T(g.s.test_tools?S_TEST_ENABLED:S_TEST_ENABLE),ID_TEST_ENABLE,body);
    button(gr,RectF(x+404,y+228,260,32),T(S_TEST_OPEN),ID_TEST_OPEN,body,false,g.s.test_tools&&g.running);
    if (g.running)
        text(gr, T(S_PATHS_RESTART), x, y + 195, body, C_RED_HI,
             StringAlignmentNear, 820, 24);
}

bool file_patch_on(const FilePatch& p)
{
    for (const std::string& id : g.s.patch_files_on) if (id == p.id) return p.status == FilePatch::OK;
    return false;
}

/* A patch's card: title, one line of notes, a dim third line, the switch. */
void patch_card(Graphics& gr, float x, float y, float w, float h, bool on, bool usable, int id,
                const std::wstring& title, const std::wstring& desc, const std::wstring& foot, bool foot_warn,
                const Font& body, const Font& fsm)
{
    const bool hot = usable && g.hover == id;
    RectF card(x, y, w, h);
    GraphicsPath* cp = round_rect(card, 6);
    SolidBrush cb(hot ? C_HOVER : C_PANEL); gr.FillPath(&cb, cp);
    Pen cbd(on ? C_RED : C_FIELD_BD, 1); gr.DrawPath(&cbd, cp); delete cp;
    RectF sw(x + w - 70, y + (h - 26) / 2, 48, 26);
    GraphicsPath* sp = round_rect(sw, 13);
    SolidBrush sb(on ? C_RED : usable ? Color(255, 60, 52, 52) : Color(255, 40, 36, 36)); gr.FillPath(&sb, sp); delete sp;
    SolidBrush knob(usable ? C_TEXT : Color(255, 90, 84, 84));
    gr.FillEllipse(&knob, on ? sw.X + sw.Width - 23 : sw.X + 3, sw.Y + 3, 20.0f, 20.0f);
    text(gr, title, x + 18, y + 8, body, usable ? C_TEXT : C_DIM, StringAlignmentNear, w - 120, 24);
    text(gr, desc, x + 18, y + 32, fsm, C_DIM, StringAlignmentNear, w - 120, 20);
    text(gr, foot, x + 18, y + 52, fsm, foot_warn ? C_RED_HI : Color(255, 120, 110, 104), StringAlignmentNear, w - 120, 20);
    if (usable) hit(x, y, w, h, id);
}

void scan_patch_dir()
{
    g.fpatches = patch_files_scan(g.s.patch_dir, &g.fpatch_files);
    g.patch_scroll = 0;
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
        patch_card(gr, x, y, w, 84, g.s.patch[i], true, ID_PATCH + i, T(k_patches[i].title), T(k_patches[i].desc),
                   T(k_patches[i].credit), false, body, fsm);
        y += 96;
    }
    /* the folder of patch files */
    std::wstring note;
    if (!g.s.patch_dir.empty()) {
        wchar_t b[200];
        if (g.fpatches.empty()) note = T(S_PATCH_NONE);
        else { swprintf(b, 200, T(S_PATCH_FOUND_FMT), (int)g.fpatches.size(), g.fpatch_files); note = b; }
    }
    path_row(gr, x, y, T(S_PATCH_FOLDER), g.s.patch_dir, g.s.patch_dir.empty() ? std::wstring(T(S_PATCH_FOLDER_HINT)) : note,
             !g.fpatches.empty() || g.s.patch_dir.empty(), ID_PATCH_DIR_BROWSE, ID_PATCH_DIR_OPEN, body, fsm);
    y += 66;
    /* its patches, in a list that scrolls with the wheel */
    const float top = y, bottom = H - 96.0f, ch = 76, gap = 8;
    g.patch_scroll_max = std::max(0.0f, g.fpatches.size() * (ch + gap) - (bottom - top));
    g.patch_scroll = std::min(std::max(g.patch_scroll, 0.0f), g.patch_scroll_max);
    gr.SetClip(RectF(x - 2, top, w + 4, bottom - top));
    for (size_t i = 0; i < g.fpatches.size(); i++) {
        const FilePatch& p = g.fpatches[i];
        const float cy = top + i * (ch + gap) - g.patch_scroll;
        if (cy + ch < top || cy > bottom) continue;
        std::wstring foot;
        wchar_t b[400];
        if (p.status == FilePatch::CODE) foot = T(S_PATCH_CODE);
        else if (p.status == FilePatch::TYPE) { swprintf(b, 400, T(S_PATCH_TYPE_FMT), p.bad_type.c_str()); foot = b; }
        else { swprintf(b, 400, T(S_PATCH_FROM_FMT), p.author.empty() ? L"?" : p.author.c_str(), p.file.c_str()); foot = b; }
        const bool usable = p.status == FilePatch::OK && cy >= top && cy + ch <= bottom;
        patch_card(gr, x, cy, w, ch, file_patch_on(p), usable, ID_FPATCH + (int)i, p.title, p.notes, foot,
                   p.status != FilePatch::OK, body, fsm);
    }
    gr.ResetClip();
    if (g.patch_scroll_max > 0) {   /* where the list is */
        const float track = bottom - top, bar = std::max(30.0f, track * track / (track + g.patch_scroll_max));
        SolidBrush sb(Color(255, 70, 62, 62));
        gr.FillRectangle(&sb, x + w + 8, top + (track - bar) * g.patch_scroll / g.patch_scroll_max, 4.0f, bar);
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
    /* The card's colour and this dot carry the state, so the text gets the
     * whole width (the old CONECTADO pill left it ~100 px and cut it off). */
    SolidBrush dot(accent); gr.FillEllipse(&dot, x + 82, y + h / 2 - 15, 8.0f, 8.0f);
    text(gr, title, x + 96, y + h / 2 - 22, body, on ? C_TEXT : C_RED_HI, StringAlignmentNear, w - 104, 22);
    text(gr, T(on ? S_PAD_KB_TOO : S_PAD_PLUG),
         x + 82, y + h / 2, fsm, C_DIM, StringAlignmentNear, w - 90, 20);
}

/* The icon set in use: the chosen one, or the connected controller's. */
/* (Automatic and keyboard name the controller's own buttons in this table.) */
int icon_layout() { return g.s.layout >= 2 ? (g.pad_kind == PAD_KIND_XINPUT ? 1 : 0) : g.s.layout; }

/* The Xbox button that plays a PS3 role. Xbox icons come with the Xbox 360
 * layout (pad_phys_360_layout, set from set_icon_env): the L1/R1 roles sit on
 * the triggers and Select/Start on Start/Back, so they are drawn and named so. */
int xbox_button(int pb)
{
    switch (pb) {
    case PB_L1: return PB_L2;  case PB_L2: return PB_L1;
    case PB_R1: return PB_R2;  case PB_R2: return PB_R1;
    case PB_SELECT: return PB_START;  case PB_START: return PB_SELECT;
    default: return pb;
    }
}
int shown_button(int pb) { return icon_layout() == 1 ? xbox_button(pb) : pb; }

void draw_controles(Graphics& gr, const Font& head, const Font& body, const Font& fsm, const Font& col)
{
    float x = SIDE + 32.0f, y = 22;
    section(gr, T(S_CONTROLS_TITLE), x, y, W - SIDE - 64.0f, head);
    text(gr, T(S_CONTROLS_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, 820, 20);
    y += 72;
    text(gr, T(S_ICONS), x, y, body, C_TEXT, StringAlignmentNear, 160, 28);
    float card_x = 0;
    /* Each choice takes its label's measured width, so four fit beside the
     * controller card in every language. */
    {
        const struct { std::wstring t; int layout, id; } opts[] = {
            { T(S_ICONS_AUTO), 2, ID_LAYOUT_AUTO }, { L"PlayStation", 0, ID_LAYOUT_PS },
            { L"Xbox", 1, ID_LAYOUT_XB }, { T(S_ICONS_KB), 3, ID_LAYOUT_KB } };
        StringFormat sf(StringFormat::GenericTypographic());
        float rx = x + 160;
        for (const auto& o : opts) {
            RectF m; gr.MeasureString(o.t.c_str(), -1, &body, PointF(0, 0), &sf, &m);
            draw_radio(gr, rx, y, o.t, g.s.layout == o.layout, o.id, body);
            rx += 24 + m.Width + 16;
        }
        draw_pad_card(gr, rx, y - 14, x + W - SIDE - 64.0f - rx, 56, body, fsm);
        card_x = rx;
    }
    /* What the choice does, in one short line under it. */
    text(gr, T(S_ICONS_HINT), x, y + 24, fsm, C_DIM, StringAlignmentNear, card_x - x - 12, 18);
    y += 54;
    /* 28 px rows: with 16 of them the Restore row ends above the bottom bar
     * (Jogar / Voltar ao jogo at H - 78); 31 px ran the two into each other. */
    /* Keyboard: PAD_KEY_SLOTS keys per action; controller: PAD_BTN_SLOTS
     * buttons. Each box has a cross to empty it; the first is what the
     * game's icons show. */
    const float c1 = x, c2 = x + 176, c3 = x + 552, rowh = 28;
    const float kw = 118, kgap = 5, bw = 134, bgap = 6;
    const float kcol = PAD_KEY_SLOTS * kw + (PAD_KEY_SLOTS - 1) * kgap, bcol = PAD_BTN_SLOTS * bw + (PAD_BTN_SLOTS - 1) * bgap;
    /* The column whose icons the game shows is marked (red, underlined);
     * the other is dimmed. Automatic marks both. */
    const bool kb_on = g.s.layout >= 2, pad_on = g.s.layout != 3;
    text(gr, T(S_COL_GAME), c1, y, col, C_RED);
    text(gr, T(S_COL_KB), c2, y, col, kb_on ? C_RED : C_DIM);
    text(gr, T(S_COL_PAD), c3, y, col, pad_on ? C_RED : C_DIM);
    { SolidBrush ul(C_RED);
      if (kb_on)  gr.FillRectangle(&ul, c2, y + 24, kcol, 2.0f);
      if (pad_on) gr.FillRectangle(&ul, c3, y + 24, bcol, 2.0f); }
    y += 30;
    /* a box: the glyph, the name, and the cross when it holds something */
    auto slot_box = [&](RectF r, int id, int clear_id, bool active, bool empty, auto glyph, const std::wstring& name,
                        const std::wstring& prompt) {
        field(gr, r, g.hover == id, active);
        if (active) {
            text(gr, prompt, r.X + 6, r.Y, fsm, C_RED_HI, StringAlignmentNear, r.Width - 8, r.Height);
        } else if (!empty) {
            glyph(r.X + 3, r.Y + 2, 21.0f);
            text(gr, name, r.X + 27, r.Y, fsm, C_TEXT, StringAlignmentNear, r.Width - 27 - 12, r.Height);
            const float cx = r.X + r.Width - 9, cy = r.Y + r.Height / 2;
            Pen xp(g.hover == clear_id ? C_RED_HI : C_DIM, 1.5f);
            gr.DrawLine(&xp, cx - 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f);
            gr.DrawLine(&xp, cx - 3.5f, cy + 3.5f, cx + 3.5f, cy - 3.5f);
        } else {
            text(gr, L"+", r.X, r.Y, body, C_DIM, StringAlignmentCenter, r.Width, r.Height);
        }
        hit(r.X, r.Y, r.Width - (empty ? 0 : 18), r.Height, id);
        if (!empty && !active) hit(r.X + r.Width - 18, r.Y, 18, r.Height, clear_id);
    };
    for (int i = 0; i < NROWS; i++) {
        int a = k_rows[i];
        float ry = y + i * rowh;
        btn_glyph(gr, 0, k_pad_default_btn[a], c1, ry + 1, 26.0f);
        text(gr, action_name(a), c1 + 38, ry, body, C_TEXT, StringAlignmentNear, c2 - c1 - 44, 28);
        for (int k = 0; k < PAD_KEY_SLOTS; k++) {
            const int vk = g.s.key[a][k], n = a * PAD_KEY_SLOTS + k;
            slot_box(RectF(c2 + k * (kw + kgap), ry + 1, kw, 25), ID_KEY + n, ID_KEY_CLEAR + n,
                     g.cap == CAP_KEY && g.cap_action == a && g.cap_slot == k, vk <= 0,
                     [&](float gx, float gy, float sz) { key_glyph(gr, vk, gx, gy, sz); }, key_name(vk), T(S_PRESS_KEY));
        }
        for (int k = 0; k < PAD_BTN_SLOTS; k++) {
            const int pb = g.s.btn[a][k], n = a * PAD_BTN_SLOTS + k;
            slot_box(RectF(c3 + k * (bw + bgap), ry + 1, bw, 25), ID_BTN + n, ID_BTN_CLEAR + n,
                     g.cap == CAP_BTN && g.cap_action == a && g.cap_slot == k, pb < 0,
                     [&](float gx, float gy, float sz) { btn_glyph(gr, icon_layout(), shown_button(pb), gx, gy, sz); },
                     pb_name(icon_layout(), shown_button(pb)), T(S_PRESS_BTN));
        }
    }
    float by = y + NROWS * rowh + 8;
    button(gr, RectF(x, by, 160, 32), T(S_RESTORE), ID_RESTORE, body);
    text(gr, T(S_CONTROLS_FOOT), x + 175, by, fsm, C_DIM, StringAlignmentNear, W - 280.0f - (x + 175), 32);
}

/* The shader cache the D3D12 backend keeps under the working directory
 * (shader_cache/*.cso, pipelines.bin and pipelines.list). Only those files are removed. */
fs::path cache_dir()
{
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    return fs::path(cwd) / L"shader_cache";
}
bool is_cache_file(const fs::path& p)
{
    const std::wstring n = p.filename().wstring();
    return p.extension() == L".cso" || n == L"pipelines.bin" || n == L"pipelines.bin.tmp" ||
           n == L"pipelines.list" || n == L"pipelines.list.tmp";
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
    y += 76;
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
    y += 80;
    /* The performance overlay: a switch, then what it shows */
    section(gr, T(S_OV_TITLE), x, y, w, head);
    y += 46;
    {
        const bool on = g.s.overlay, hot = g.hover == ID_OVERLAY;
        RectF card(x, y, w, 64);
        GraphicsPath* cp = round_rect(card, 6);
        SolidBrush cb(hot ? C_HOVER : C_PANEL); gr.FillPath(&cb, cp);
        Pen cbd(on ? C_RED : C_FIELD_BD, 1); gr.DrawPath(&cbd, cp); delete cp;
        RectF sw(x + w - 70, y + 19, 48, 26);
        GraphicsPath* sp = round_rect(sw, 13);
        SolidBrush sb(on ? C_RED : Color(255, 60, 52, 52)); gr.FillPath(&sb, sp); delete sp;
        SolidBrush knob(C_TEXT);
        gr.FillEllipse(&knob, on ? sw.X + sw.Width - 23 : sw.X + 3, sw.Y + 3, 20.0f, 20.0f);
        text(gr, T(S_OV_SHOW), x + 18, y + 8, body, C_TEXT, StringAlignmentNear, w - 120, 24);
        text(gr, T(S_OV_DESC), x + 18, y + 34, fsm, C_DIM, StringAlignmentNear, w - 120, 20);
        hit(x, y, w, 64, ID_OVERLAY);
    }
    y += 78;
    /* one tick box per item, dimmed while the overlay is off */
    StringFormat sf(StringFormat::GenericTypographic());
    float cx = x + 4;
    for (int i = 0; i < NOVERLAY; i++) {
        if (!k_overlay_items[i].name) continue;   /* ms: shown with FPS */
        const std::wstring name = k_overlay_items[i].name;
        const bool ticked = (g.s.overlay_items >> i) & 1, hot = g.hover == ID_OVERLAY_ITEM + i;
        const Color ink = !g.s.overlay ? C_DIM : C_TEXT;
        RectF box(cx, y + 6, 16, 16);
        SolidBrush bb(ticked && g.s.overlay ? C_RED : C_FIELD); gr.FillRectangle(&bb, box);
        Pen bp(hot ? C_TEXT : (ticked && g.s.overlay ? C_RED : C_FIELD_BD), 1); gr.DrawRectangle(&bp, box);
        if (ticked) {
            Pen tick(ink, 2);
            PointF pts[3] = { PointF(cx + 3.5f, y + 14), PointF(cx + 7, y + 17.5f), PointF(cx + 13, y + 10) };
            gr.DrawLines(&tick, pts, 3);
        }
        RectF m; gr.MeasureString(name.c_str(), -1, &body, PointF(0, 0), &sf, &m);
        text(gr, name, cx + 24, y, body, ink, StringAlignmentNear, m.Width + 8, 28);
        hit(cx, y, 24 + m.Width + 8, 28, ID_OVERLAY_ITEM + i);
        cx += 24 + m.Width + 26;
    }
    y += 40;
    /* where it sits, and its numbers side by side or one under the other:
     * one row, each name followed by its list */
    {
        const float col = (w - 24) / 2;
        const int layouts[] = { S_OV_ROW, S_OV_COLUMN };
        const std::wstring names[] = { T(S_OV_POSITION), T(S_OV_LAYOUT) };
        const std::wstring values[] = { T(S_SRT_TL + g.s.overlay_corner), T(layouts[g.s.overlay_layout]) };
        const int ids[] = { ID_DD_OV_CORNER, ID_DD_OV_LAYOUT };
        for (int i = 0; i < 2; i++) {
            const float cx0 = x + i * (col + 24);
            RectF m; gr.MeasureString(names[i].c_str(), -1, &body, PointF(0, 0), &sf, &m);
            const float lw = std::min(m.Width + 12, col * 0.55f);
            text(gr, names[i], cx0, y, body, C_TEXT, StringAlignmentNear, lw, 32);
            dropdown(gr, RectF(cx0 + lw, y, col - lw, 32), values[i], ids[i], body);
        }
    }
    y += 50;
    /* The game's log: view it live, copy it, save a copy */
    section(gr, T(S_LOG_TITLE), x, y, w, head);
    y += 46;
    const std::wstring lp = log_file_path();
    button(gr, RectF(x, y, 150, 34), T(S_LOG_VIEW), ID_LOG_VIEW, body);
    button(gr, RectF(x + 160, y, 150, 34), T(S_LOG_COPY), ID_LOG_COPY, body, false, !lp.empty());
    button(gr, RectF(x + 320, y, 170, 34), T(S_LOG_SAVE), ID_LOG_SAVE, body, false, !lp.empty());
    button(gr, RectF(x + 500, y, 150, 34), T(S_OPEN_FOLDER), ID_LOG_FOLDER, body, false, !lp.empty());
    y += 44;
    std::wstring log_note = g.log_msg;
    if (log_note.empty()) {
        if (lp.empty()) log_note = T(log_to_console() ? S_LOG_CONSOLE : S_LOG_NONE);
        else { wchar_t b[1024]; swprintf(b, 1024, T(S_LOG_SIZE_FMT), lp.c_str(), log_size() / 1048576.0); log_note = b; }
    }
    text(gr, log_note, x, y, fsm, g.log_msg.empty() ? C_DIM : C_OK, StringAlignmentNear, w, 20);
}

/* A switch card whose description wraps (the options' texts run to two
 * lines): the patch cards' look. */
void option_card(Graphics& gr, float x, float y, float w, float h, bool on, int id,
                 const std::wstring& title, const std::wstring& desc, const Font& body, const Font& fsm)
{
    const bool hot = g.hover == id;
    RectF card(x, y, w, h);
    GraphicsPath* cp = round_rect(card, 6);
    SolidBrush cb(hot ? C_HOVER : C_PANEL); gr.FillPath(&cb, cp);
    Pen cbd(on ? C_RED : C_FIELD_BD, 1); gr.DrawPath(&cbd, cp); delete cp;
    RectF sw(x + w - 70, y + (h - 26) / 2, 48, 26);
    GraphicsPath* sp = round_rect(sw, 13);
    SolidBrush sb(on ? C_RED : Color(255, 60, 52, 52)); gr.FillPath(&sb, sp); delete sp;
    SolidBrush knob(C_TEXT);
    gr.FillEllipse(&knob, on ? sw.X + sw.Width - 23 : sw.X + 3, sw.Y + 3, 20.0f, 20.0f);
    text(gr, title, x + 18, y + 8, body, C_TEXT, StringAlignmentNear, w - 120, 24);
    {
        const std::wstring t = g_lang == LANG_AR ? L"\u202B" + desc + L"\u202C" : desc;
        StringFormat sf; sf.SetAlignment(StringAlignmentNear); sf.SetLineAlignment(StringAlignmentNear);
        sf.SetTrimming(StringTrimmingEllipsisWord);
        SolidBrush db(C_DIM);
        gr.DrawString(t.c_str(), -1, &fsm, RectF(x + 18, y + 34, w - 120, h - 40), &sf, &db);
    }
    hit(x, y, w, h, id);
}

std::wstring srt_enemy_value(int n) {return n==0?T(S_AA_OFF):n==99?T(S_SRT_ALL):std::to_wstring(n);}
std::wstring srt_language_value() {return g.s.srt_language?k_lang_name[cv_srt_i18n::selection(g.s.srt_language,g_lang)]:T(S_SRT_FOLLOW);}
RectF srt_slider_rect(int id) {
    const float w=W-SIDE-64.f,col=(w-24)/2,right=SIDE+32.f+col+24;
    return RectF(right,id==ID_SRT_SCALE?434.f:519.f,col-72,32);
}
void srt_slider(Graphics& gr,int id,int value,int minimum,int maximum,const Font& body) {
    const RectF r=srt_slider_rect(id);const float left=r.X+8,span=r.Width-16,cy=r.Y+16;
    const float thumb=left+span*cv_launcher_controls::slider_fraction(value,minimum,maximum);
    Pen track(C_FIELD_BD,5);track.SetStartCap(LineCapRound);track.SetEndCap(LineCapRound);
    gr.DrawLine(&track,left,cy,left+span,cy);
    Pen progress(C_RED,5);progress.SetStartCap(LineCapRound);progress.SetEndCap(LineCapRound);
    if(thumb>left)gr.DrawLine(&progress,left,cy,thumb,cy);
    if(g.srt_drag==id||g.hover==id) {SolidBrush glow(Color(45,230,50,50));gr.FillEllipse(&glow,thumb-11,cy-11,22.f,22.f);}
    SolidBrush fill(C_TEXT);Pen border(C_RED,2);
    gr.FillEllipse(&fill,thumb-7,cy-7,14.f,14.f);gr.DrawEllipse(&border,thumb-7,cy-7,14.f,14.f);
    text(gr,std::to_wstring(value)+L"%",r.X+r.Width+8,r.Y,body,C_TEXT,StringAlignmentFar,64,32);
    hit(r.X,r.Y,r.Width,r.Height,id);
}
void draw_srt(Graphics& gr,const Font& head,const Font& body,const Font& fsm)
{
    const float x=SIDE+32.f,w=W-SIDE-64.f,col=(w-24)/2,right=x+col+24;
    section(gr,T(S_SRT_TITLE),x,34,w,head);
    text(gr,T(S_SRT_INTRO),x,70,fsm,C_DIM,StringAlignmentNear,w,24);
    option_card(gr,x,104,w,58,g.s.srt,ID_SRT,T(S_SRT_ENABLE),L"",body,fsm);
    const int names[]={S_SRT_NAME,S_SRT_HEALTH,S_SRT_TIME,S_SRT_STATS,S_SRT_INVENTORY};
    for(int i=0;i<5;i++)option_card(gr,i%2?right:x,180+(i/2)*70.f,col,58,g.s.srt_parts[i],ID_SRT_PART+i,T(names[i]),L"",body,fsm);
    text(gr,T(S_SRT_ENEMIES),right,318,body,C_TEXT,StringAlignmentNear,col,24);
    dropdown(gr,RectF(right,347,col,32),srt_enemy_value(g.s.srt_enemies),ID_DD_SRT_ENEMIES,body);
    text(gr,T(S_SRT_CORNER),x,404,body,C_TEXT,StringAlignmentNear,col,24);
    dropdown(gr,RectF(x,434,col,32),T(S_SRT_TL+g.s.srt_corner),ID_DD_SRT_CORNER,body);
    text(gr,T(S_SRT_SCALE)+std::wstring(L"  (50–200%)"),right,404,body,C_TEXT,StringAlignmentNear,col,24);
    srt_slider(gr,ID_SRT_SCALE,g.s.srt_scale,50,200,body);
    text(gr,T(S_SRT_LANGUAGE),x,490,body,C_TEXT,StringAlignmentNear,col,24);
    dropdown(gr,RectF(x,519,col,32),srt_language_value(),ID_DD_SRT_LANGUAGE,body);
    text(gr,T(S_SRT_OPACITY)+std::wstring(L"  (0–100%)"),right,490,body,C_TEXT,StringAlignmentNear,col,24);
    srt_slider(gr,ID_SRT_OPACITY,g.s.srt_opacity,0,100,body);
    StringFormat sf;sf.SetLineAlignment(StringAlignmentNear);SolidBrush brush(C_DIM);
    const float third=(w-48)/3,middle=x+third+24,last=middle+third+24;
    text(gr,T(S_SRT_INV_POS),x,575,body,C_TEXT,StringAlignmentNear,third,24);
    dropdown(gr,RectF(x,604,third,32),T(g.s.srt_inventory_below?S_SRT_INV_BELOW:S_SRT_INV_SIDE),ID_DD_SRT_INVPOS,body);
    text(gr,T(S_SRT_BOSS_BAR),middle,575,body,C_TEXT,StringAlignmentNear,third,24);
    dropdown(gr,RectF(middle,604,third,32),T(S_SRT_BOSS_PANEL+g.s.srt_boss_bar),ID_DD_SRT_BOSSBAR,body);
    text(gr,T(S_SRT_SHOW_IN),last,575,body,C_TEXT,StringAlignmentNear,third,24);
    dropdown(gr,RectF(last,604,third,32),T(g.s.srt_window?S_SRT_IN_WINDOW:S_SRT_IN_GAME),ID_DD_SRT_WINDOW,body);
    gr.DrawString(T(S_SRT_HOTKEY),-1,&fsm,RectF(x,648,third*2+24,42),&sf,&brush);
    if(g.s.srt_window)gr.DrawString(T(S_SRT_WINDOW_HINT),-1,&fsm,RectF(last,648,third,60),&sf,&brush);
}

void draw_desempenho(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    const float w = W - SIDE - 64.0f;
    section(gr, T(S_SH_TITLE), x, y, w, head);
    text(gr, T(S_SH_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, w, 20);
    y += 74;
    for (int i = 0; i < NSHADER_OPTS; i++) {
        /* Six cards end above the Jogar button (its top is H - 78). */
        option_card(gr, x, y, w, 86, g.s.shader_opt[i], ID_SHADER_OPT + i,
                    T(k_shader_opts[i].title), T(k_shader_opts[i].desc), body, fsm);
        y += 94;
    }
}

/* The texture mods folder (rsx_texture_pack.h): chosen, or "textures" beside
 * the exe. Replacements are read from its subfolder named after the version
 * when there is one, dumps go to <folder>\<version>\dump. */
fs::path tex_folder()
{
    if (!g.s.tex_dir.empty()) return fs::path(g.s.tex_dir);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    return fs::path(exe).parent_path() / L"textures";
}
fs::path tex_dump_folder()
{
    return g.s.tex_dump_dir.empty() ? tex_folder() / GAME_CODE / L"dump" : fs::path(g.s.tex_dump_dir);
}
int count_png(const fs::path& dir, bool recurse)
{
    int n = 0; std::error_code ec;
    if (!recurse) {
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && _wcsicmp(e.path().extension().c_str(), L".png") == 0) n++;
        return n;
    }
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec)) { if (_wcsicmp(it->path().filename().c_str(), L"dump") == 0) it.disable_recursion_pending(); continue; }
        if (_wcsicmp(it->path().extension().c_str(), L".png") == 0) n++;
    }
    return n;
}
void scan_tex_dir()
{
    std::error_code ec;
    fs::path root = tex_folder() / GAME_CODE;
    if (!fs::is_directory(root, ec)) root = tex_folder();
    g.tex_root_name = root.filename().wstring();
    g.tex_rep = count_png(root, true);
    g.tex_dumped = count_png(tex_dump_folder(), false);
}

void draw_mods(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    const float w = W - SIDE - 64.0f;
    section(gr, T(S_MODS_TITLE), x, y, w, head);
    text(gr, T(S_MODS_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, w, 20);
    y += 74;
    if (g.tex_rep < 0) scan_tex_dir();
    wchar_t note[512];
    swprintf(note, 512, T(S_TEX_COUNT_FMT), g.tex_rep, g.tex_dumped, g.tex_root_name.c_str());
    path_row(gr, x, y, T(S_TEX_FOLDER), tex_folder().wstring(), note, true, ID_TEX_BROWSE, ID_TEX_OPEN, body, fsm);
    y += 66;
    swprintf(note, 512, T(S_TEX_DUMP_COUNT_FMT), g.tex_dumped);
    path_row(gr, x, y, T(S_TEX_DUMP_FOLDER), tex_dump_folder().wstring(), note, true, ID_TEX_DUMP_BROWSE, ID_TEX_DUMP_OPEN, body, fsm);
    y += 70;
    option_card(gr, x, y, w, 92, g.s.tex_replace, ID_TEX_REPLACE, T(S_TEX_REPLACE_T), T(S_TEX_REPLACE_D), body, fsm);
    y += 104;
    option_card(gr, x, y, w, 92, g.s.tex_dump, ID_TEX_DUMP, T(S_TEX_DUMP_T), T(S_TEX_DUMP_D), body, fsm);
    y += 108;
    text(gr, T(S_TEX_HOTKEY), x, y, fsm, C_TEXT, StringAlignmentNear, w, 20);
    y += 26;
    const std::wstring howto = g_lang == LANG_AR ? L"\u202B" + std::wstring(T(S_TEX_HOWTO)) + L"\u202C" : T(S_TEX_HOWTO);
    StringFormat sf; sf.SetAlignment(StringAlignmentNear); sf.SetLineAlignment(StringAlignmentNear);
    SolidBrush hb(C_DIM);
    gr.DrawString(howto.c_str(), -1, &fsm, RectF(x, y, w, 60), &sf, &hb);
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

    if (g.page == P_INICIO) draw_inicio(gr, fHead, fBody, fCode, fSmall);
    else if (g.page == P_JOGO) draw_jogo(gr, fHead, fBody, fSmall);
    else if (g.page == P_CONTROLES) draw_controles(gr, fHead, fBody, fSmall, fCol);
    else if (g.page == P_PATCHES) draw_patches(gr, fHead, fBody, fSmall);
    else if (g.page == P_DESEMPENHO) draw_desempenho(gr, fHead, fBody, fSmall);
    else if (g.page == P_MODS) draw_mods(gr, fHead, fBody, fSmall);
    else if (g.page == P_SRT) draw_srt(gr, fHead, fBody, fSmall);
    else draw_avancado(gr, fHead, fBody, fSmall);
    draw_sidebar(gr, fTitle, fTitle2, fNav, fSmall);
    draw_play(gr, fBig, fSmall);

    /* open dropdown list on top */
    if (g.open_dd >= 0) {
        std::vector<std::wstring> items; std::vector<int> item_lang; int sel = 0;
        const float x=g_dd_anchor.X,bw=g_dd_anchor.Width;float y=g_dd_anchor.Y+g_dd_anchor.Height;
        if (g.open_dd == ID_DD_MODE) { for (int i = 0; i < 3; i++) items.push_back(mode_name(i)); sel = g.s.mode; }
        else if (g.open_dd == ID_DD_RES) { for (int i = 0; i < 5; i++) items.push_back(res_name(i)); sel = g.s.res; }
        else if (g.open_dd == ID_DD_AA) { for (int i = 0; i < 3; i++) items.push_back(aa_name(i)); sel = g.s.aa; }
        else if (g.open_dd == ID_DD_FPS) { for (int i = 0; i < 2; i++) items.push_back(fps_name(i)); sel = g.s.fps; }
        else if (g.open_dd == ID_DD_ANISO) { for (int i = 0; i < 5; i++) items.push_back(aniso_name(i)); sel = g.s.aniso; }
        else if (g.open_dd == ID_DD_SHARP) { for (int i = 0; i <= 10; i++) items.push_back(sharp_name(i)); sel = g.s.sharpness / 10; }
        else if (g.open_dd == ID_DD_SRT_ENEMIES) { for(int i=0;i<=5;i++)items.push_back(srt_enemy_value(i));items.push_back(T(S_SRT_ALL));sel=g.s.srt_enemies==99?6:g.s.srt_enemies; }
        else if (g.open_dd == ID_DD_SRT_CORNER) {for(int i=0;i<4;i++)items.push_back(T(S_SRT_TL+i));sel=g.s.srt_corner;}
        else if (g.open_dd == ID_DD_SRT_INVPOS) { items = { T(S_SRT_INV_SIDE), T(S_SRT_INV_BELOW) }; sel = g.s.srt_inventory_below; }
        else if (g.open_dd == ID_DD_SRT_WINDOW) { items = { T(S_SRT_IN_GAME), T(S_SRT_IN_WINDOW) }; sel = g.s.srt_window; }
        else if (g.open_dd == ID_DD_SRT_BOSSBAR) { items = { T(S_SRT_BOSS_PANEL), T(S_SRT_BOSS_TOP), T(S_SRT_BOSS_BOTTOM) }; sel = g.s.srt_boss_bar; }
        else if (g.open_dd == ID_DD_OV_CORNER) { for (int i = 0; i < 4; i++) items.push_back(T(S_SRT_TL + i)); sel = g.s.overlay_corner; }
        else if (g.open_dd == ID_DD_OV_LAYOUT) { items = { T(S_OV_ROW), T(S_OV_COLUMN) }; sel = g.s.overlay_layout; }
        else if (g.open_dd == ID_DD_SRT_LANGUAGE) {
            items={T(S_SRT_FOLLOW),k_lang_name[LANG_EN],k_lang_name[LANG_PT]};item_lang={g_lang,LANG_EN,LANG_PT};
            for(int lang=2;lang<LANG_COUNT;lang++) {items.push_back(k_lang_name[lang]);item_lang.push_back(lang);}
            sel=g.s.srt_language;
        }
        else {   /* Sistema, then each language in its own name and script */
            items.push_back(T(S_LANG_SYSTEM)); item_lang.push_back(g_lang);
            for (int i = 0; i < LANG_COUNT; i++) {
                items.push_back(k_lang_name[k_lang_order[i]]); item_lang.push_back(k_lang_order[i]);
                if (k_lang_order[i] == g_lang_pick) sel = i + 1;
            }
        }
        const int n = (int)items.size(), vis = std::min(n, DD_VISIBLE);
        if(y+vis*28>H-12)y=std::max(12.f,g_dd_anchor.Y-vis*28.f);
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

/* Where to save a copy of the log: a Save dialog, a dated name to start from. */
std::wstring pick_log_save(HWND owner)
{
    std::wstring out;
    IFileSaveDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"code_veronica_log_%04d-%02d-%02d_%02d%02d.txt", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    dlg->SetFileName(name);
    dlg->SetDefaultExtension(L"txt");
    COMDLG_FILTERSPEC f[2] = { { L"Log (*.txt)", L"*.txt" }, { T(S_ALL_FILES), L"*.*" } };
    dlg->SetFileTypes(2, f);
    if (SUCCEEDED(dlg->Show(owner))) {
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

LogViewerText log_viewer_text()
{
    return { T(S_LOG_WINDOW), T(S_LOG_COPY_ALL), T(S_LOG_SAVE), T(S_OPEN_FOLDER), T(S_LOG_FOLLOW), T(S_LOG_CUT),
             T(S_LOG_NONE), T(S_LOG_CONSOLE), T(S_LOG_COPIED_FMT), T(S_LOG_SAVED) };
}

void open_in_explorer(const std::wstring& p)
{
    if (p.empty()) return;
    std::error_code ec;
    if (fs::is_directory(p, ec)) ShellExecuteW(g.hwnd, L"open", p.c_str(), NULL, NULL, SW_SHOWNORMAL);
    else { std::wstring a = L"/select,\"" + p + L"\""; ShellExecuteW(g.hwnd, L"open", L"explorer.exe", a.c_str(), NULL, SW_SHOWNORMAL); }
}

/* While the game runs: display mode, resolution and controls apply at once. */
void set_map_env();
void set_overlay_env();
void set_srt_env();
void set_env(const char* k, const std::string& v);
void set_texture_env();
void set_note_text_env();
void toggle_textures(const char* by);
void changed();
void finish_srt_drag();

void apply_live(bool restore_menu = true)
{
    cv_test_tools_enable(g.s.test_tools);
    if (!g.running) return;
    if (g.s.mode != g.applied_mode) {
        g.applied_mode = g.s.mode;
        rsx_d3d12_set_window_mode(g.s.mode);
        if (restore_menu) SetTimer(g.hwnd, 2, 200, NULL);   /* then put the menu back on top */
    }
    if (g.s.res != g.applied_res) {
        g.applied_res = g.s.res;
        rsx_d3d12_set_res_scale((float)atof(k_res[g.s.res].scale));
    }
    if (g.s.aa != g.applied_aa) {
        g.applied_aa = g.s.aa;
        rsx_d3d12_set_aa(g.s.aa);
    }
    rsx_d3d12_set_color_adjust(g.s.brightness, g.s.contrast);
    rsx_d3d12_set_saturation(g.s.saturation);
    rsx_d3d12_set_sharpness(g.s.sharpness);
    cellpad_set_map(&g.s.btn[0][0], &g.s.key[0][0]);
    set_map_env();
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
    finish_srt_drag();
    g.cap = CAP_NONE; g.open_dd = -1;
    SetWindowPos(g.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (HWND gw = (HWND)rsx_d3d12_window()) {
        if (g.s.mode != 0) ShowWindow(g.hwnd, SW_MINIMIZE);
        SetForegroundWindow(gw);
    }
}
void toggle_menu() { if (GetForegroundWindow() == g.hwnd) hide_menu(); else show_menu(); }
const UINT WM_TOGGLE_MENU = WM_APP + 1;
const UINT WM_PC_SETTINGS_READ = WM_APP + 2;
const UINT WM_PC_SETTINGS_CHANGE = WM_APP + 3;
const UINT WM_PC_SETTINGS_READ_SATURATION = WM_APP + 4;
void menu_hook() { PostMessageW(g.hwnd, WM_TOGGLE_MENU, 0, 0); }   /* game window thread */
const UINT WM_TOGGLE_TEXTURES = WM_APP + 5;
const UINT WM_TOGGLE_SRT = WM_APP + 6;
const UINT WM_TEST_MAPS = WM_APP + 20;
void key_hook(unsigned vk) { if (vk == VK_F9) PostMessageW(g.hwnd, WM_TOGGLE_TEXTURES, 0, 0);
    else if(vk==VK_F8)PostMessageW(g.hwnd,WM_TOGGLE_SRT,0,0);
    else if(vk==VK_F7)PostMessageW(g.hwnd,WM_TEST_MAPS,0,0); }   /* game window thread */
/* F9 in the game or R3+R2 on a controller: texture mods on or off at once,
 * with a notice in the game's corner. The replacements stay loaded while
 * off, so going back and forth to compare is immediate. */
void toggle_textures(const char* by)
{
    g.s.tex_replace = !g.s.tex_replace;
    set_texture_env();
    texpack_flags_changed();
    if (g.running) perf_overlay_toast(T(g.s.tex_replace ? S_TEX_TOAST_ON : S_TEX_TOAST_OFF), g.s.tex_replace);
    changed();
    fprintf(stderr, "[launcher] texture mods %s (%s)%c", g.s.tex_replace ? "on" : "off", by, 10);
}
void toggle_srt()
{
    g.s.srt=!g.s.srt;set_srt_env();
    if(g.running)perf_overlay_toast(T(g.s.srt?S_SRT_ON:S_SRT_OFF),g.s.srt);
    changed();
}
void quit_game()
{
    if (rsx_d3d12_window()) rsx_d3d12_close();
    else ExitProcess(0);   /* still booting: nothing to close yet */
}
void start_game();
int prepare_eboot();
void start_other_version();
void set_icon_env();

void changed() { save_settings(g.s); g.play_msg.clear(); apply_live(); InvalidateRect(g.hwnd, NULL, FALSE); }

void update_srt_slider(int id,int x) {
    const RectF r=srt_slider_rect(id);int& value=id==ID_SRT_SCALE?g.s.srt_scale:g.s.srt_opacity;
    const int next=cv_launcher_controls::slider_value(float(x),r.X+8,r.Width-16,id==ID_SRT_SCALE?50:0,id==ID_SRT_SCALE?200:100);
    if(next==value)return;
    value=next;g.srt_drag_dirty=true;set_srt_env();InvalidateRect(g.hwnd,NULL,FALSE);
}
void finish_srt_drag() {
    if(g.srt_drag<0)return;
    const bool dirty=g.srt_drag_dirty;g.srt_drag=-1;g.srt_drag_dirty=false;
    if(GetCapture()==g.hwnd)ReleaseCapture();
    if(dirty)changed();else InvalidateRect(g.hwnd,NULL,FALSE);
}

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
            else if (g.open_dd == ID_DD_FPS) g.s.fps = i;
            else if (g.open_dd == ID_DD_ANISO) g.s.aniso = i;
            else if (g.open_dd == ID_DD_SHARP) g.s.sharpness = i * 10;
            else if(g.open_dd==ID_DD_SRT_ENEMIES)g.s.srt_enemies=i==6?99:i;
            else if(g.open_dd==ID_DD_SRT_CORNER)g.s.srt_corner=i;
            else if(g.open_dd==ID_DD_SRT_LANGUAGE)g.s.srt_language=i;
            else if (g.open_dd == ID_DD_SRT_INVPOS) g.s.srt_inventory_below = i;
            else if (g.open_dd == ID_DD_SRT_WINDOW) g.s.srt_window = i;
            else if (g.open_dd == ID_DD_SRT_BOSSBAR) g.s.srt_boss_bar = i;
            else if (g.open_dd == ID_DD_OV_CORNER) { g.s.overlay_corner = i; set_overlay_env(); }
            else if (g.open_dd == ID_DD_OV_LAYOUT) { g.s.overlay_layout = i; set_overlay_env(); }
            else { set_lang_pick(i == 0 ? -1 : k_lang_order[i - 1]); set_note_text_env(); }
            set_srt_env();
            g.open_dd = -1; changed(); return;
        }
        g.open_dd = -1; InvalidateRect(g.hwnd, NULL, FALSE); return;
    }
    if (g.cap != CAP_NONE) { g.cap = CAP_NONE; g.cap_action = -1; }
    if (id >= ID_NAV && id < ID_NAV + P_COUNT) {
        g.page = (Page)(id - ID_NAV); g.cache_msg.clear(); g.log_msg.clear();
        if (g.page == P_PATCHES) scan_patch_dir();   /* the folder may have new files */
        if (g.page == P_MODS) g.tex_rep = -1;        /* count the textures again */
    }
    else if (id >= ID_PATCH && id < ID_PATCH + NPATCHES) { g.s.patch[id - ID_PATCH] ^= true; changed(); }
    else if (id == ID_DD_MODE || id == ID_DD_RES || id == ID_DD_AA || id == ID_DD_FPS || id == ID_DD_ANISO || id == ID_DD_SHARP || id == ID_DD_LANG || id==ID_DD_SRT_ENEMIES || id==ID_DD_SRT_CORNER || id==ID_DD_SRT_LANGUAGE || id == ID_DD_OV_CORNER || id == ID_DD_OV_LAYOUT || id == ID_DD_SRT_INVPOS || id == ID_DD_SRT_WINDOW || id == ID_DD_SRT_BOSSBAR) {
        g.open_dd = id; g.dd_scroll = 0;
        if (id == ID_DD_LANG && g_lang_pick >= 0)   /* open with the chosen language in view */
            for (int i = 0; i < LANG_COUNT; i++)
                if (k_lang_order[i] == g_lang_pick) g.dd_scroll = std::max(0, i + 1 - DD_VISIBLE / 2);
        if(id==ID_DD_SRT_LANGUAGE)g.dd_scroll=std::max(0,g.s.srt_language-DD_VISIBLE/2);
    }
    else if (id == ID_CACHE_CLEAR && !g.running) g.cache_msg = Tf(S_CACHE_CLEARED_FMT, cache_clear());
    else if (id == ID_CACHE_OPEN) { std::error_code ec; fs::create_directories(cache_dir(), ec); open_in_explorer(cache_dir().wstring()); }
    else if(id==ID_TEST_ENABLE){g.s.test_tools=!g.s.test_tools;cv_test_tools_enable(g.s.test_tools);changed();}
    else if(id==ID_TEST_OPEN&&g.running)cv_test_tools_show(g.hwnd,g.s.game,g_lang);
    else if (id == ID_GAME_BROWSE) { auto p = pick(true, g.s.game, nullptr, nullptr); if (!p.empty()) { g.s.game = p; changed(); } }
    else if (id == ID_RAP_BROWSE) { auto p = pick(false, g.s.rap, T(S_RAP_FILTER), L"*.rap"); if (!p.empty()) { g.s.rap = p; changed(); } }
    else if (id == ID_SAVE_BROWSE) { auto p = pick(true, save_folder().wstring(), nullptr, nullptr); if (!p.empty()) { g.s.saves = p; changed(); } }
    else if (id == ID_SAVE_DEFAULT) { g.s.saves.clear(); changed(); }
    else if (id == ID_SAVE_OPEN) { std::error_code ec; fs::create_directories(save_folder(), ec); open_in_explorer(save_folder().wstring()); }
    else if (id == ID_GAME_OPEN) open_in_explorer(g.s.game);
    else if (id == ID_RAP_OPEN) open_in_explorer(g.s.rap);
    else if (id == ID_LAYOUT_PS || id == ID_LAYOUT_XB || id == ID_LAYOUT_AUTO || id == ID_LAYOUT_KB) {
        g.s.layout = id == ID_LAYOUT_AUTO ? 2 : id == ID_LAYOUT_KB ? 3 : id - ID_LAYOUT_PS;
        set_icon_env();   /* the game reads it when it next loads a screen with icons */
        changed();
    }
    else if (id >= ID_KEY && id < ID_KEY + PA_COUNT * PAD_KEY_SLOTS) {
        g.cap = CAP_KEY; g.cap_action = (id - ID_KEY) / PAD_KEY_SLOTS; g.cap_slot = (id - ID_KEY) % PAD_KEY_SLOTS;
        g.cap_since = GetTickCount64(); SetFocus(g.hwnd);
    }
    else if (id >= ID_KEY_CLEAR && id < ID_KEY_CLEAR + PA_COUNT * PAD_KEY_SLOTS) {
        g.s.key[(id - ID_KEY_CLEAR) / PAD_KEY_SLOTS][(id - ID_KEY_CLEAR) % PAD_KEY_SLOTS] = 0; changed();
    }
    else if (id >= ID_BTN_CLEAR && id < ID_BTN_CLEAR + PA_COUNT * PAD_BTN_SLOTS) {
        g.s.btn[(id - ID_BTN_CLEAR) / PAD_BTN_SLOTS][(id - ID_BTN_CLEAR) % PAD_BTN_SLOTS] = -1; changed();
    }
    else if (id >= ID_BTN && id < ID_BTN + PA_COUNT * PAD_BTN_SLOTS) {
        g.cap = CAP_BTN; g.cap_action = (id - ID_BTN) / PAD_BTN_SLOTS; g.cap_slot = (id - ID_BTN) % PAD_BTN_SLOTS;
        g.cap_since = GetTickCount64();
        for (int i = 0; i < PAD_PHYS_MAX; i++) { PadPhys p; pad_phys_poll(i, &p); g.pad_prev[i] = p.buttons; }
    }
    else if (id == ID_LOG_VIEW) { g.log_msg.clear(); log_viewer_open(g.hwnd, log_viewer_text(), pick_log_save); }
    else if (id == ID_LOG_COPY) {
        const unsigned long long n = log_copy_to_clipboard(g.hwnd);
        if (n) { wchar_t b[200]; swprintf(b, 200, T(S_LOG_COPIED_FMT), n / 1048576.0); g.log_msg = b; }
    }
    else if (id == ID_LOG_SAVE) {
        const std::wstring d = pick_log_save(g.hwnd);
        if (!d.empty() && log_save_copy(d)) g.log_msg = T(S_LOG_SAVED);
    }
    else if (id == ID_LOG_FOLDER) open_in_explorer(log_file_path());
    else if (id == ID_PATCH_DIR_BROWSE) {
        auto p = pick(true, g.s.patch_dir, nullptr, nullptr);
        if (!p.empty()) { g.s.patch_dir = p; scan_patch_dir(); changed(); }
    }
    else if (id == ID_PATCH_DIR_OPEN && !g.s.patch_dir.empty()) open_in_explorer(g.s.patch_dir);
    else if (id == ID_TEX_BROWSE) {
        auto p = pick(true, tex_folder().wstring(), nullptr, nullptr);
        if (!p.empty()) { g.s.tex_dir = p; g.tex_rep = -1; set_texture_env(); changed(); }
    }
    else if (id == ID_TEX_OPEN) {   /* made on first use, with this version's subfolder */
        std::error_code ec;
        fs::create_directories(tex_folder() / GAME_CODE, ec);
        open_in_explorer((tex_folder() / GAME_CODE).wstring());
        g.tex_rep = -1;
    }
    else if (id == ID_TEX_DUMP_BROWSE) {
        auto p = pick(true, tex_dump_folder().wstring(), nullptr, nullptr);
        if (!p.empty()) { g.s.tex_dump_dir = p; g.tex_rep = -1; set_texture_env(); changed(); }
    }
    else if (id == ID_TEX_DUMP_OPEN) {
        std::error_code ec;
        fs::create_directories(tex_dump_folder(), ec);
        open_in_explorer(tex_dump_folder().wstring());
        g.tex_rep = -1;
    }
    else if (id == ID_TEX_REPLACE) toggle_textures("switch");
    else if (id == ID_TEX_DUMP) { g.s.tex_dump = !g.s.tex_dump; set_texture_env(); changed(); }
    else if (id >= ID_FPATCH && id < ID_FPATCH + (int)g.fpatches.size()) {
        const FilePatch& p = g.fpatches[(size_t)(id - ID_FPATCH)];
        auto& on = g.s.patch_files_on;
        const auto it = std::find(on.begin(), on.end(), p.id);
        const bool now = it == on.end();
        if (now) on.push_back(p.id); else on.erase(it);
        if (g.running) ps3_patch_writes(p.id.c_str(), p.spec.c_str(), now);   /* at once, in the running game */
        changed();
    }
    else if(id==ID_SRT)toggle_srt();
    else if(id>=ID_SRT_PART&&id<ID_SRT_PART+5) {g.s.srt_parts[id-ID_SRT_PART]^=true;set_srt_env();changed();}
    else if (id == ID_OVERLAY) { g.s.overlay = !g.s.overlay; set_overlay_env(); changed(); }
    else if (id >= ID_SHADER_OPT && id < ID_SHADER_OPT + NSHADER_OPTS) {
        if (id - ID_SHADER_OPT == 5 && g.running) return;   /* fixed once the game runs */
        g.s.shader_opt[id - ID_SHADER_OPT] ^= true;
        set_env("SHADER_PRELOAD_NOTE", g.s.shader_opt[4] ? "1" : "0");   /* the note, at once */
        changed();
    }
    else if (id >= ID_OVERLAY_ITEM && id < ID_OVERLAY_ITEM + NOVERLAY) {
        g.s.overlay_items ^= 1 << (id - ID_OVERLAY_ITEM); set_overlay_env(); changed();
    }
    else if (id == ID_RESTORE) {
        for (int a = 0; a < PA_COUNT; a++) {
            for (int k = 0; k < PAD_KEY_SLOTS; k++) g.s.key[a][k] = k ? 0 : k_pad_default_key[a];
            for (int k = 0; k < PAD_BTN_SLOTS; k++) g.s.btn[a][k] = k ? -1 : k_pad_default_btn[a];
        }
        changed();
    }
    else if (id == ID_PLAY) {
        bool ok; check_game(g.s.game, &ok);
        if (!ok) { g.page = P_JOGO; g.play_msg = T(S_SET_FOLDER_FIRST); }
        else if (folder_code() != GAME_CODE) { save_settings(g.s); start_other_version(); }
        else if (!prepare_save_folder()) { g.page = P_JOGO; g.play_msg = T(S_SAVE_ERROR); }
        else if (int e = prepare_eboot()) { g.page = P_JOGO; g.play_msg = with_code(T(e), GAME_CODE); }
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
    case WM_PC_SETTINGS_READ: return g.s.mode | (g.s.res << 4) | (g.s.aa << 8) | ((g.s.brightness + 80) << 10) | ((g.s.contrast - 50) << 18);
    case WM_PC_SETTINGS_READ_SATURATION: return g.s.saturation;
    case WM_PC_SETTINGS_CHANGE:
        if (wp == 0 && lp >= 0 && lp <= 4) g.s.res = int(lp);
        else if (wp == 1 && lp >= 0 && lp <= 2) g.s.aa = int(lp);
        else if (wp == 2 && lp >= 0 && lp <= 2) g.s.mode = int(lp);
        else if (wp == 3 && lp >= 0 && lp <= 160) g.s.brightness = -80 + int(lp);
        else if (wp == 4 && lp >= 0 && lp <= 130) g.s.contrast = 50 + int(lp);
        else if (wp == 5 && lp >= 0 && lp <= 200) g.s.saturation = int(lp);
        else return 0;
        save_settings(g.s); KillTimer(h, 2); apply_live(false);
        InvalidateRect(h, NULL, FALSE); return 1;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
    case WM_MOUSEMOVE: {
        if(g.srt_drag>=0) {update_srt_slider(g.srt_drag,GET_X_LPARAM(lp));SetCursor(LoadCursor(NULL,IDC_HAND));return 0;}
        int id = hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (id != g.hover) { g.hover = id; InvalidateRect(h, NULL, FALSE); }
        SetCursor(LoadCursor(NULL, id >= 0 ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSEWHEEL:
        if(g.page==P_SRT && g.open_dd<0) {
            POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);const int id=hit_at(p.x,p.y);
            if(id==ID_SRT_OPACITY) {
                const int step=GET_WHEEL_DELTA_WPARAM(wp)>0?1:-1;
                g.s.srt_opacity=std::clamp(g.s.srt_opacity+step,0,100);set_srt_env();changed();return 0;
            }
            if(id==ID_SRT_SCALE) {
                const int step=GET_WHEEL_DELTA_WPARAM(wp)>0?1:-1;
                g.s.srt_scale=std::clamp(g.s.srt_scale+step,50,200);set_srt_env();changed();return 0;
            }
        }
        if (g.open_dd >= 0) {
            g.dd_scroll += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
            InvalidateRect(h, NULL, FALSE);
        } else if (g.page == P_PATCHES && g.patch_scroll_max > 0) {
            g.patch_scroll += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -60.0f : 60.0f;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE: g.hover = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) return TRUE; break;
    case WM_LBUTTONDOWN: {
        const int id=hit_at(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
        if(g.page==P_SRT&&g.open_dd<0&&(id==ID_SRT_SCALE||id==ID_SRT_OPACITY)) {
            g.srt_drag=id;g.srt_drag_dirty=false;SetCapture(h);
            update_srt_slider(id,GET_X_LPARAM(lp));InvalidateRect(h,NULL,FALSE);return 0;
        }
        click(id);return 0;
    }
    case WM_LBUTTONUP:
        if(g.srt_drag>=0) {update_srt_slider(g.srt_drag,GET_X_LPARAM(lp));finish_srt_drag();return 0;}break;
    case WM_CAPTURECHANGED: case WM_CANCELMODE: finish_srt_drag();return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (g.cap == CAP_KEY) {
            if (wp == VK_ESCAPE) { g.cap = CAP_NONE; InvalidateRect(h, NULL, FALSE); return 0; }
            int vk = vk_from_msg(wp, lp);
            int* keys = g.s.key[g.cap_action];
            for (int k = 0; k < PAD_KEY_SLOTS; k++) if (keys[k] == vk) keys[k] = 0;   /* once per action */
            keys[g.cap_slot] = vk; g.cap = CAP_NONE; changed(); return 0;
        }
        if (g.cap == CAP_BTN && wp == VK_ESCAPE) { g.cap = CAP_NONE; InvalidateRect(h, NULL, FALSE); return 0; }
        if (wp == VK_ESCAPE && g.open_dd >= 0) { g.open_dd = -1; InvalidateRect(h, NULL, FALSE); return 0; }
        if (g.running && (wp == VK_INSERT || wp == VK_ESCAPE)) { hide_menu(); return 0; }
        if (m == WM_SYSKEYDOWN) break;
        return 0;
    case WM_ACTIVATE:
        if(LOWORD(wp)==WA_INACTIVE)finish_srt_drag();
        g_pad_block = g.running && LOWORD(wp) != WA_INACTIVE;   /* the game ignores input under the menu */
        /* However it was activated (taskbar, Alt+Tab, a click), come above a
         * fullscreen game, which is topmost; give that up when the game
         * takes the focus back. */
        if (g.running)
            SetWindowPos(h, LOWORD(wp) != WA_INACTIVE ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        break;
    case WM_CLOSE:
        finish_srt_drag();
        if (g.running) { ShowWindow(h, SW_HIDE); quit_game(); return 0; }
        break;
    case WM_TIMER: {
        if (wp == 2) {
            KillTimer(h, 2);
            if (GetForegroundWindow() == h || IsWindowVisible(h)) show_menu();
            return 0;
        }
        if (g.running) {   /* L2+R3+R1, all on the same controller, toggles the menu. */
            uint32_t test_combo=0;
            for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};if(pad_phys_poll(i,&p)&&cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::test_chord))test_combo|=1u<<i;}
            if(test_combo&~g.test_combo_prev)PostMessageW(g.hwnd,WM_TEST_MAPS,0,0);
            g.test_combo_prev=test_combo;
            uint32_t combo = 0;
            for (int i = 0; i < PAD_PHYS_MAX; i++) {
                PadPhys p; if (!pad_phys_poll(i, &p)) continue;
                if (cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::menu_chord)) combo |= 1u << i;
            }
            if (combo & ~g.combo_prev) toggle_menu();
            g.combo_prev = combo;
            uint32_t tcombo = 0;   /* R3+R2: texture mods on or off */
            for (int i = 0; i < PAD_PHYS_MAX; i++) {
                PadPhys p; if (!pad_phys_poll(i, &p)) continue;
                if ((p.buttons & (1u << PB_R3)) && (p.buttons & (1u << PB_R2))) tcombo |= 1u << i;
            }
            if (tcombo & ~g.tex_combo_prev) toggle_textures("R3+R2");
            g.tex_combo_prev = tcombo;
            uint32_t scombo=0;
            for(int i=0;i<PAD_PHYS_MAX;i++) {PadPhys p;if(!pad_phys_poll(i,&p))continue;
                if(cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::srt_chord))scombo|=1u<<i;}
            if(scombo&~g.srt_combo_prev)toggle_srt();g.srt_combo_prev=scombo;
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
                    if (pressed & (1u << b)) {
                        int* btns = g.s.btn[g.cap_action];
                        for (int k = 0; k < PAD_BTN_SLOTS; k++) if (btns[k] == b) btns[k] = -1;   /* once per action */
                        btns[g.cap_slot] = b; g.cap = CAP_NONE; changed(); return 0;
                    }
            }
            if (GetTickCount64() - g.cap_since > 10000) { g.cap = CAP_NONE; redraw = true; }
        }
        if (redraw) InvalidateRect(h, NULL, FALSE);
        return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    if (m == WM_TOGGLE_MENU) { toggle_menu(); return 0; }
    if (m == WM_TOGGLE_TEXTURES) { toggle_textures("F9"); return 0; }
    if(m==WM_TOGGLE_SRT) {toggle_srt();return 0;}
    if(m==WM_TEST_MAPS) {if(g.running)cv_test_tools_show(g.hwnd,g.s.game,g_lang);return 0;}
    return DefWindowProcW(h, m, wp, lp);
}

void set_env(const char* k, const std::string& v) { _putenv_s(k, v.c_str()); }

/* In-game icons (button_icons.cpp): auto follows the first controller. */
void set_icon_env()
{
    const char* v = g.s.layout == 3 ? "keyboard" : g.s.layout == 2 ? "auto" : g.s.layout == 1 ? "xbox" : "ps";
    set_env("CV_BUTTON_ICONS", v);
    /* The controls follow the icons, so the 360 manual shown is what the pad
     * does: Xbox icons, 360 layout (on any controller); auto, Xbox pads only. */
    set_env("PAD_360_LAYOUT", g.s.layout >= 2 ? "auto" : g.s.layout == 1 ? "on" : "off");
}
void set_env_default(const char* k, const char* v) { if (!getenv(k)) _putenv_s(k, v); }

/* The shader loading note's words, in the launcher's language. The game
 * reads them as it draws, so a language picked while it runs shows too. */
void set_note_text_env()
{
    set_env("SHADER_NOTE_TEXT", save_utf8(fs::path(std::wstring(T(S_SH_NOTE_LOADING)) + L"|" + T(S_SH_NOTE_LOADED))));
}

/* Texture mods (rsx_texture_pack.h). The game reads the switches again as it
 * runs, so turning them on or off here acts at once. */
void set_srt_env()
{
    const int lang=cv_srt_i18n::selection(g.s.srt_language,g_lang);
    set_env("SRT",g.s.srt?"1":"0");
    const char* keys[]={"SRT_NAME","SRT_HEALTH","SRT_TIME","SRT_STATS","SRT_INVENTORY"};
    for(int i=0;i<5;i++)set_env(keys[i],g.s.srt_parts[i]?"1":"0");
    set_env("SRT_OPACITY",std::to_string(g.s.srt_opacity));
    set_env("SRT_INVENTORY_POS",g.s.srt_inventory_below?"below":"side");
    set_env("SRT_WINDOW",g.s.srt_window?"1":"0");
    set_env("SRT_BOSS_BAR",g.s.srt_boss_bar==1?"top":g.s.srt_boss_bar==2?"bottom":"panel");
    set_env("SRT_ENEMIES",std::to_string(g.s.srt_enemies));set_env("SRT_SCALE",std::to_string(g.s.srt_scale));
    const char* corners[]={"tl","tr","bl","br"};set_env("SRT_CORNER",corners[g.s.srt_corner]);
    set_env("SRT_LANGUAGE",k_lang_id[lang]);
    const std::wstring credit=srt_credit_text(lang);
    set_env("SRT_CREDIT",save_utf8(fs::path(credit)));
    const int ids[]={S_SRT_HEALTH,S_SRT_TIME,S_SRT_SAVE,S_SRT_RETRY,S_SRT_ENEMIES,S_SRT_INVENTORY,S_SRT_EQUIPPED,
        S_SRT_FINE,S_SRT_CAUTION,S_SRT_DANGER,S_SRT_POISON,S_SRT_GAS,S_SRT_DEAD,S_SRT_WAIT,S_SRT_EMPTY,S_SRT_BOSS,S_SRT_PHASE};
    std::wstring words;for(int id:ids) {if(!words.empty())words+=L"|";words+=k_str[lang][id];}
    set_env("SRT_TEXT",save_utf8(fs::path(words)));cv_srt_configure_env();
}

void set_texture_env()
{
    set_env("TEXTURE_DIR", save_utf8(tex_folder()));
    set_env("TEXTURE_GAME", CV_TITLE_ID);
    set_env("TEXTURE_DUMP_DIR", save_utf8(tex_dump_folder()));
    set_env("TEXTURE_REPLACE", g.s.tex_replace ? "1" : "0");
    set_env("TEXTURE_DUMP", g.s.tex_dump ? "1" : "0");
}

/* PERF_OVERLAY from the settings: empty while the overlay is off. The game
 * reads it again twice a second, so a change shows at once. */
void set_overlay_env()
{
    std::string items;
    if (g.s.overlay)
        for (int i = 0; i < NOVERLAY; i++)
            if ((g.s.overlay_items >> i) & 1) items += (items.empty() ? "" : ",") + std::string(k_overlay_items[i].id);
    set_env("PERF_OVERLAY", items);
    const char* corners[] = { "tl", "tr", "bl", "br" };
    set_env("PERF_OVERLAY_CORNER", corners[g.s.overlay_corner]);
    set_env("PERF_OVERLAY_LAYOUT", g.s.overlay_layout ? "column" : "row");
}

/* PAD_BTNMAP / PAD_KEYMAP from the settings: read by the pad at start, and by
 * the button icons whenever they change (the keys drawn in the game). */
void set_map_env()
{
    std::string bm, km;
    for (int a = 0; a < PA_COUNT; a++) {
        bm += std::string(k_pad_action_id[a]) + "=";
        km += std::string(k_pad_action_id[a]) + "=";
        for (int k = 0; k < PAD_BTN_SLOTS; k++) bm += std::string(k ? "+" : "") + pad_phys_id(g.s.btn[a][k]);
        for (int k = 0; k < PAD_KEY_SLOTS; k++) km += (k ? "+" : "") + std::to_string(g.s.key[a][k]);
        if (a + 1 < PA_COUNT) { bm += ","; km += ","; }
    }
    set_env("PAD_BTNMAP", bm);
    set_env("PAD_KEYMAP", km);
}
std::string narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    return s;
}

HANDLE s_play_ev;

extern "C" int npdrm_self_decrypt(const char* self_path, const char* rap_path, const char* out_path);
extern "C" int edat_sha1_file(const char* path, unsigned char out[20]);

/* True if the ELF is the exact one the recompiled code came from. */
bool eboot_matches(const std::string& elf)
{
    unsigned char h[20];
    if (edat_sha1_file(elf.c_str(), h) != 0) return false;
    char hex[41];
    for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    return strcmp(hex, GAME_ELF_SHA1) == 0;
}

/* The game runs from the plain ELF. A PSN copy only has the encrypted
 * EBOOT.BIN, so it is decrypted once with the .rap (the same steps RPCS3
 * takes) into eboot_cache beside the shader cache, and reused while it is
 * newer than the EBOOT.BIN. A folder that already holds a decrypted
 * USRDIR/EBOOT.elf and no EBOOT.BIN is used as it is. */
int prepare_eboot()
{
    std::error_code ec;
    const fs::path usr = fs::path(g.s.game) / L"USRDIR", bin = usr / L"EBOOT.BIN";
    if (!fs::exists(bin, ec)) {
        g.eboot = narrow((usr / L"EBOOT.elf").wstring());
        return eboot_matches(g.eboot) ? 0 : S_EBOOT_MISMATCH;
    }
    /* A decrypted EBOOT.elf beside the EBOOT.BIN, if it is the right one. */
    if (fs::exists(usr / L"EBOOT.elf", ec) && eboot_matches(narrow((usr / L"EBOOT.elf").wstring()))) {
        g.eboot = narrow((usr / L"EBOOT.elf").wstring());
        return 0;
    }
    /* The licence is per version: the one chosen may be the other version's,
     * so take this version's from the same folder when the name says so. */
    const std::wstring rap = rap_for_game(g.s, GAME_CODE);
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    const fs::path out = fs::path(cwd) / L"eboot_cache" / GAME_CODE / L"EBOOT.elf";
    g.eboot = narrow(out.wstring());
    if (!fs::exists(out, ec) || fs::last_write_time(out, ec) < fs::last_write_time(bin, ec)) {
        /* An empty licence is tried too: a free-licence EBOOT needs none. */
        fs::create_directories(out.parent_path(), ec);
        if (npdrm_self_decrypt(narrow(bin.wstring()).c_str(), narrow(rap).c_str(), g.eboot.c_str()) != 0)
            return S_DECRYPT_FAIL;
    }
    if (eboot_matches(g.eboot)) return 0;
    fs::remove(out, ec);            /* not this version: do not keep it as a cache */
    return S_EBOOT_MISMATCH;
}

/* The title of the chosen folder (its PARAM.SFO), or "" if it has none. */
std::wstring folder_code()
{
    return widen(sfo_field(fs::path(g.s.game) / L"PARAM.SFO", "TITLE_ID").c_str());
}

/* The chosen folder is the other version: start its executable, which reads
 * the settings just saved and plays at once (CV_LAUNCHER_AUTOPLAY), and leave. */
void start_other_version()
{
    const GameVersion* v = version_of(folder_code());
    const std::wstring exe = v ? sibling_exe(*v) : L"";
    if (exe.empty()) { g.page = P_JOGO; g.play_msg = T(S_SET_FOLDER_FIRST); return; }
    set_env("CV_LAUNCHER_AUTOPLAY", "1");
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), &cmd[0], NULL, NULL, FALSE, 0, NULL, cwd, &si, &pi)) {
        set_env("CV_LAUNCHER_AUTOPLAY", "");
        g.page = P_JOGO; g.play_msg = T(S_SET_FOLDER_FIRST); return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    ExitProcess(0);
}

void start_game()
{
    const Settings& s = g.s;
    set_env("PS3_VFS_ROOT", narrow(s.game));
    set_env("PS3_SAVE_ROOT", s.saves.empty() ? "" : save_utf8(save_folder()));
    const auto rap = rap_for_game(s, GAME_CODE);
    if (!rap.empty()) set_env("PS3_RAP_DIR", narrow(fs::path(rap).parent_path().wstring()));
    if (!s.fw.empty()) set_env("PS3_DEV_FLASH", narrow(s.fw));
    set_env("RES_SCALE", k_res[s.res].scale);
    set_env("WINDOW_MODE", k_mode_env[s.mode]);
    set_env("AA_MODE", std::to_string(s.aa));
    set_env("RSX_ANISO", s.aniso ? std::to_string(1 << s.aniso) : "0");
    /* Unlocked 60 (cv_fps_experimental.cpp, raw benchmark): the game logic
     * still steps once per drawn frame, so it runs fast. Only this process
     * sees it; the ELF and the save stay as they are. 30 leaves the
     * environment alone, as before this option existed. */
    if (s.fps == 1) {
        set_env("CV_FPS_TEST_60", "1");
        set_env("CV_FPS_BENCHMARK_RAW", "1");
        set_env("CV_RENDER_INTERPOLATION", "");
    }
    set_env("CV_BRIGHTNESS", std::to_string(s.brightness));
    set_env("CV_CONTRAST", std::to_string(s.contrast));
    set_env("CV_SATURATION", std::to_string(s.saturation));
    rsx_d3d12_set_color_adjust(s.brightness, s.contrast);
    rsx_d3d12_set_saturation(s.saturation);
    set_env("CV_SHARPNESS", std::to_string(s.sharpness));
    rsx_d3d12_set_sharpness(s.sharpness);
    set_icon_env();
    std::string pl;
    for (int i = 0; i < NPATCHES; i++)
        if (s.patch[i]) pl += (pl.empty() ? "" : ",") + std::string(k_patches[i].id);
    set_env("PS3_PATCHES", pl);
    set_map_env();
    set_overlay_env();
    set_env("PSO_LIBRARY", s.shader_opt[0] ? "1" : "0");
    set_env("SHADER_PRELOAD", s.shader_opt[1] ? "bg" : "wait");
    set_env("FP_ASYNC", s.shader_opt[2] ? "1" : "0");
    set_env("SHADER_PRELOAD_PARALLEL", s.shader_opt[3] ? "1" : "0");
    set_env("SHADER_PRELOAD_NOTE", s.shader_opt[4] ? "1" : "0");
    if (s.shader_opt[5]) set_env("RSX_GPU_VFETCH", "1");   /* off: as before (a shortcut may still set it) */
    set_texture_env();
    set_srt_env();
    set_note_text_env();
    {   /* file patches: applied by the runtime once the game is loaded */
        if (g.fpatches.empty()) scan_patch_dir();
        std::string w;
        for (const FilePatch& p : g.fpatches)
            if (file_patch_on(p)) w += (w.empty() ? "" : ";") + p.id + "=" + p.spec;
        set_env("PS3_PATCH_WRITES", w);
    }
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
    g.running = true;
    g.applied_mode = s.mode; g.applied_res = s.res; g.applied_aa = s.aa;
    g_rsx_menu_hook = menu_hook;
    g_rsx_key_hook = key_hook;
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
    cv_test_tools_enable(g.s.test_tools);
    set_icon_env();   /* the button capture below reads the layout it implies */
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
    /* Started by the other version's launcher for this version's folder:
     * play at once with the settings it saved. Once only. */
    if (const char* a = getenv("CV_LAUNCHER_AUTOPLAY"); a && *a) {
        _putenv_s("CV_LAUNCHER_AUTOPLAY", "");
        click(ID_PLAY);
    }
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { if(cv_test_tools_message(&msg))continue;TranslateMessage(&msg); DispatchMessageW(&msg); }
    cv_test_tools_close();
    /* closed before Jogar: the main thread sees this thread end */
    delete g.banner; g.banner = nullptr;
    GdiplusShutdown(gtok);
    return 0;
}

}  /* namespace */

/* Settings bridge used only by the native in-game PC SETTINGS row. */
static int cv_pc_fallback_settings = -1;
extern "C" int cv_pc_settings_read(void)
{
    if (g.hwnd && IsWindow(g.hwnd)) return int(SendMessageW(g.hwnd, WM_PC_SETTINGS_READ, 0, 0));
    if (cv_pc_fallback_settings < 0) {
        int mode=0, res=1, aa=0;
        if (const char* v=getenv("WINDOW_MODE")) {
            if (!_stricmp(v,"borderless") || !strcmp(v,"1")) mode=1;
            else if (!_stricmp(v,"fullscreen") || !strcmp(v,"2")) mode=2;
        }
        if (const char* v=getenv("RES_SCALE")) {
            const float scale=float(atof(v)); const float scales[]={.6667f,1.f,1.5f,2.f,3.f};
            float best=100.f;
            for(int i=0;i<5;i++) { const float delta=scale>scales[i]?scale-scales[i]:scales[i]-scale;
                if(delta<best) {best=delta;res=i;} }
        }
        if (const char* v=getenv("AA_MODE")) aa=std::clamp(atoi(v),0,2);
        int bri=0,con=100;
        if (const char* v=getenv("CV_BRIGHTNESS")) bri=std::clamp(atoi(v),-80,80);
        if (const char* v=getenv("CV_CONTRAST")) con=std::clamp(atoi(v),50,180);
        cv_pc_fallback_settings=mode|(res<<4)|(aa<<8)|((bri+80)<<10)|((con-50)<<18);
    }
    return cv_pc_fallback_settings;
}
static int cv_pc_fallback_saturation = -1;
extern "C" int cv_pc_settings_saturation_read(void)
{
    if (g.hwnd && IsWindow(g.hwnd)) return int(SendMessageW(g.hwnd, WM_PC_SETTINGS_READ_SATURATION, 0, 0));
    if (cv_pc_fallback_saturation < 0) {
        const char* value=getenv("CV_SATURATION");
        cv_pc_fallback_saturation=std::clamp(value?atoi(value):_wtoi(ini_get(L"tela",L"saturacao",L"100").c_str()),0,200);
    }
    return cv_pc_fallback_saturation;
}
extern "C" void cv_pc_settings_change(int which,int value)
{
    if (which<0 || which>5 || value<0 || value>(which==0?4:which<=2?2:which==3?160:which==4?130:200)) return;
    if (g.hwnd && IsWindow(g.hwnd)) { SendMessageW(g.hwnd,WM_PC_SETTINGS_CHANGE,WPARAM(which),LPARAM(value)); return; }
    int packed=cv_pc_settings_read();
    if(which==0) {
        packed=(packed&~0x70)|(value<<4);
        ini_set(L"tela",L"resolucao",std::to_wstring(value));
        rsx_d3d12_set_res_scale(float(atof(k_res[value].scale)));
    } else if(which==1) {
        packed=(packed&~0x300)|(value<<8);
        ini_set(L"tela",L"antialias",std::to_wstring(value)); rsx_d3d12_set_aa(value);
    } else if(which==2) {
        packed=(packed&~3)|value;
        ini_set(L"tela",L"modo",std::to_wstring(value)); rsx_d3d12_set_window_mode(value);
    } else if(which==3) {
        const int brightness=-80+value;
        packed=(packed&~(255<<10))|(value<<10);
        ini_set(L"tela",L"brilho",std::to_wstring(brightness));
        rsx_d3d12_set_color_adjust(brightness,50+((packed>>18)&255));
    } else if(which==4) {
        const int contrast=50+value;
        packed=(packed&~(255<<18))|(value<<18);
        ini_set(L"tela",L"contraste",std::to_wstring(contrast));
        rsx_d3d12_set_color_adjust(-80+((packed>>10)&255),contrast);
    } else {
        ini_set(L"tela",L"saturacao",std::to_wstring(value));
        cv_pc_fallback_saturation=value;
        rsx_d3d12_set_saturation(value);
    }
    cv_pc_fallback_settings=packed;
}

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
