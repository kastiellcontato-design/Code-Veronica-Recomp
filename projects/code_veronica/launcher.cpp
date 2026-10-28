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
#include "cv_enhanced_settings.h"
#include "cv_combat_hud.h"
#include "cv_manual_reload.h"
#include "cv_quick_select.h"
#include "quick_i18n.h"
#include "cv_shader_i18n.h"
#include "quick_edit_text.h"
#include "cv_enhanced_i18n.h"
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "pad_phys.h"
#include "../../libs/video/rsx_render_optimizations.h"
#include "pad_map.h"
#include "analog_bindings.h"
#include "analog_i18n.h"
#include "launcher_i18n.h"
#include "launcher_design_i18n.h"
#include "launcher_maps_i18n.h"
#include "cv_trophies.h"
#include "cv_trophy_popup.h"
#include "cv_trophy_i18n.h"
#include <map>
#include "patch_files.h"
#include "cv_version.h"
#include "cv_srt.h"
#include "cv_srt_layout.h"
#include "launcher_srt_layout_i18n.h"
#include "cv_srt_i18n.h"
#include "cv_launcher_controls.h"
#include "log_viewer.h"
#include "cv_test_tools.h"
#include "cv_item_menu.h"
#include "cv_keycap_style.h"
#include "cv_cheats.h"
#include "cv_texture_export.h"
#include "cv_texture_paths.h"
#include "../../libs/video/rsx_texture_pack.h"
#include "cv_updater.h"
#include "cv_launcher_cache.h"
#include "launcher_update_i18n.h"
#include <thread>
#include <memory>

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
void  rsx_d3d12_set_vsync(int on);
void  rsx_d3d12_close(void);
void* rsx_d3d12_window(void);
extern void (*g_rsx_menu_hook)(void);
extern void (*g_rsx_key_hook)(unsigned vk);
extern "C" void rsx_d3d12_request_screenshot(void);
extern int g_pad_block;
extern int g_pad_shortcut_chords;
void  perf_overlay_toast(const wchar_t* text, int on);
void  cv_focus_pause_enable(int on);               /* cv_focus_pause.cpp */
void  cv_focus_pause_label(const wchar_t* text);
void  texpack_flags_changed(void);
void  cellpad_set_map(const int* btn, const int* key);
void  ps3_patch_set(const char* name, int on);
int   ps3_patch_writes(const char* id, const char* spec, int on);
}
namespace {
static bool opt30_comparison() {return rsx_opt30_parse(std::getenv("CV30_OPTIMIZATIONS"))>=0;}


/* ---------------------------------------------------------------- palette */
const Color C_BG(255,16,16,18),C_SIDE(255,11,11,13),C_PANEL(255,24,23,25),
 C_LINE(255,98,77,52),C_TEXT(255,235,226,210),C_DIM(255,163,154,143),
 C_RED(255,133,26,40),C_RED_HI(255,192,42,55),C_HOVER(255,36,29,31),
 C_FIELD(255,25,24,26),C_FIELD_BD(255,74,64,51),C_OK(255,120,192,140);

/* The versions this port is built for, one executable each, side by side
 * (cv_version.h, build-release-jp.cmd). The launcher of either accepts both:
 * a folder of the other version starts that version's executable, which
 * reads the same settings and starts at once. */
struct GameVersion { const wchar_t* code; const char* elf_sha1; const wchar_t* exe; const wchar_t* name; };
const GameVersion k_versions[] = {
    { L"NPEB00553", "d0c9d3ab3a5ed12c80ab77fe73fa58d464083fa1", L"CodeVeronicaPort.exe",
      L"Resident Evil Code: Veronica X (PSN)" },
    { L"NPUB30467", "93d060e5019d877df39cd091c28efff920b98e49", L"CodeVeronicaPort_US.exe",
      L"Resident Evil Code: Veronica X (PSN US)" },
    { L"NPJB00135", "ad17bbfadf23937480ba0330616262e595dfaa78", L"CodeVeronicaPort_JP.exe",
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
    std::filesystem::path regional(v.exe);
    const std::filesystem::path p = std::filesystem::path(self).parent_path() / regional;
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
bool s_shortcut_xbox=false;
const wchar_t* T(int id) {
    /* Controller hints name every pad, whatever is connected: "R1+R2+L1+L2+□"
     * becomes "PS: R1+R2+L1+L2+□ · Xbox: RB+RT+LB+LT+X · Switch: R+ZR+L+ZL+Y". */
    const wchar_t* original=k_str[g_lang][id];
    if(!wcsstr(original,L"R1+R2+L1+L2+")&&!wcsstr(original,L"L1/R1")&&!wcsstr(original,L"/A")&&!wcsstr(original,L"/X"))return original;
    static std::wstring hints[LANG_COUNT][S_COUNT];
    auto& hint=hints[g_lang][id];
    if(hint.empty()) {
        hint=original;
        const wchar_t* ps[]={L"R1+R2+L1+L2+L3",L"R1+R2+L1+L2+R3",L"R1+R2+L1+L2+△",L"R1+R2+L1+L2+○",L"R1+R2+L1+L2+✕",L"R1+R2+L1+L2+□",L"L1/R1",L"✕/A",L"□/X"};
        const wchar_t* xb[]={L"RB+RT+LB+LT+LS",L"RB+RT+LB+LT+RS",L"RB+RT+LB+LT+Y",L"RB+RT+LB+LT+B",L"RB+RT+LB+LT+A",L"RB+RT+LB+LT+X",L"LB/RB",L"A",L"X"};
        const wchar_t* sw[]={L"R+ZR+L+ZL+LS",L"R+ZR+L+ZL+RS",L"R+ZR+L+ZL+X",L"R+ZR+L+ZL+A",L"R+ZR+L+ZL+B",L"R+ZR+L+ZL+Y",L"L/R",L"B",L"Y"};
        const wchar_t* ps_name[]={nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,L"✕",L"□"};
        for(int i=0;i<9;i++)for(size_t at=0;(at=hint.find(ps[i],at))!=std::wstring::npos;){
            const std::wstring both=L"PS: "+std::wstring(ps_name[i]?ps_name[i]:ps[i])+L" · Xbox: "+xb[i]+L" · Switch: "+sw[i];
            hint.replace(at,wcslen(ps[i]),both);at+=both.size();
        }
    }
    return hint.c_str();
}

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
    cv_enhanced_language_set(g_lang);
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
const int W = 1280, H = 880, SIDE = 224;

/* ---------------------------------------------------------------- settings */

static const wchar_t* ptbr_ui(int column) {
 static const wchar_t* const rows[LANG_COUNT][4]={
{L"Textos do jogo",L"Original",L"Portugu\u00eas (Brasil)",L"Vers\u00f5es europeia e americana. Reinicie o jogo para aplicar."},
{L"Game text",L"Original",L"Portugu\u00eas (Brasil)",L"European and American versions. Restart the game to apply."},
{L"Textos del juego",L"Original",L"Portugu\u00eas (Brasil)",L"Versiones europea y americana. Reinicia el juego para aplicar."},
{L"\u30b2\u30fc\u30e0\u306e\u30c6\u30ad\u30b9\u30c8",L"\u30aa\u30ea\u30b8\u30ca\u30eb",L"Portugu\u00eas (Brasil)",L"\u6b27\u5dde\u7248\u3068\u7c73\u56fd\u7248\u306b\u5bfe\u5fdc\u3002\u9069\u7528\u3059\u308b\u306b\u306f\u30b2\u30fc\u30e0\u3092\u518d\u8d77\u52d5\u3057\u3066\u304f\u3060\u3055\u3044\u3002"},
{L"Textes du jeu",L"D\u2019origine",L"Portugu\u00eas (Brasil)",L"Versions europ\u00e9enne et am\u00e9ricaine. Relancez le jeu pour appliquer."},
{L"Spieltexte",L"Original",L"Portugu\u00eas (Brasil)",L"Europ\u00e4ische und amerikanische Version. Zum Anwenden das Spiel neu starten."},
{L"Testi di gioco",L"Originale",L"Portugu\u00eas (Brasil)",L"Versioni europea e americana. Riavvia il gioco per applicare."},
{L"Teksty gry",L"Oryginalne",L"Portugu\u00eas (Brasil)",L"Wersje europejska i ameryka\u0144ska. Uruchom gr\u0119 ponownie, aby zastosowa\u0107."},
{L"Oyun metinleri",L"Orijinal",L"Portugu\u00eas (Brasil)",L"Avrupa ve Amerika s\u00fcr\u00fcmleri. Uygulamak i\u00e7in oyunu yeniden ba\u015flat\u0131n."},
{L"\u0646\u0635\u0648\u0635 \u0627\u0644\u0644\u0639\u0628\u0629",L"\u0627\u0644\u0623\u0635\u0644\u064a\u0629",L"Portugu\u00eas (Brasil)",L"\u0644\u0644\u0646\u0633\u062e\u062a\u064a\u0646 \u0627\u0644\u0623\u0648\u0631\u0648\u0628\u064a\u0629 \u0648\u0627\u0644\u0623\u0645\u0631\u064a\u0643\u064a\u0629. \u0623\u0639\u062f \u062a\u0634\u063a\u064a\u0644 \u0627\u0644\u0644\u0639\u0628\u0629 \u0644\u062a\u0637\u0628\u064a\u0642 \u0627\u0644\u062a\u063a\u064a\u064a\u0631."},
{L"\u6e38\u620f\u6587\u672c",L"\u539f\u7248",L"Portugu\u00eas (Brasil)",L"\u652f\u6301\u6b27\u6d32\u7248\u548c\u7f8e\u6d32\u7248\u3002\u91cd\u542f\u6e38\u620f\u540e\u751f\u6548\u3002"},
{L"\uac8c\uc784 \ud14d\uc2a4\ud2b8",L"\uc6d0\ubcf8",L"Portugu\u00eas (Brasil)",L"\uc720\ub7fd\ud310\uacfc \ubbf8\uad6d\ud310\uc744 \uc9c0\uc6d0\ud569\ub2c8\ub2e4. \uc801\uc6a9\ud558\ub824\uba74 \uac8c\uc784\uc744 \ub2e4\uc2dc \uc2dc\uc791\ud558\uc138\uc694."},
{L"\u0422\u0435\u043a\u0441\u0442\u044b \u0438\u0433\u0440\u044b",L"\u041e\u0440\u0438\u0433\u0438\u043d\u0430\u043b",L"Portugu\u00eas (Brasil)",L"\u0415\u0432\u0440\u043e\u043f\u0435\u0439\u0441\u043a\u0430\u044f \u0438 \u0430\u043c\u0435\u0440\u0438\u043a\u0430\u043d\u0441\u043a\u0430\u044f \u0432\u0435\u0440\u0441\u0438\u0438. \u041f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u0442\u0438\u0442\u0435 \u0438\u0433\u0440\u0443 \u0434\u043b\u044f \u043f\u0440\u0438\u043c\u0435\u043d\u0435\u043d\u0438\u044f."}
 }; return rows[g_lang][column];
}

struct Settings {
    int game_text_ptbr=0;
    int mode = 0;          /* 0 janela, 1 sem borda, 2 tela cheia */
    int res = 1;           /* index into k_res */
    int aa = 0;            /* anti-aliasing: 0 off, 1 FXAA, 2 SMAA */
    int frame_generation = 0; /* opt-in FSR 3.1, restart required */
    int vsync = 0;         /* 60 FPS only, never with frame generation; switches live */
    int pose_device = 0; /* CPU default; GPU compute requires restart, 60 FPS only */
    int fps = 0;           /* 0 original 30; 1 experimental 60 with render-only interpolation */
    int brightness = 0;   /* -80..80, zero leaves the image unchanged */
    int contrast = 100;   /* 50..180 percent */
    int saturation = 100; /* 0..200 percent */
    int sharpness = 0;    /* FSR 1 RCAS, 0..100 percent in steps of 10; 0 off */
    bool patch[8] = {};    /* k_patches on/off */
    int layout = 2;        /* button icons: 0 PlayStation, 1 Xbox, 2 automatic, 3 keyboard, 4 Nintendo Switch */
    int rumble = 100;      /* vibration strength, percent (PAD_RUMBLE_SCALE): 0 off .. 200 */
    bool overlay = false;  /* the performance overlay (rsx_perf_overlay.h) */
    int overlay_items = 3; /* bits of k_overlay_items: FPS and frame time */
    int overlay_corner = 1, overlay_layout = 0;   /* 0 tl, 1 tr, 2 bl, 3 br; 0 a row, 1 a column */
    int overlay_size = 1, overlay_text_only = 0;  /* 0 small, 1 medium, 2 large; 1: no box behind the numbers */
    int shader_compiler=0; // 0 DXC default, 1 FXC compatibility; restart required
    bool shader_opt[6] = { true, true, true, true, true, true };   /* k_shader_opts, Desempenho page */
    std::wstring tex_dir;              /* texture mods (Mods page); empty: "mods/textures" beside the exe */
    std::wstring tex_dump_dir;         /* where dumps go; empty: <tex_dir>\<version>\dump */
    bool tex_replace = false, tex_dump = false, tex_dump_all = false, tex_dds = false;
    bool test_tools=false; // optional experimental map window, off by default
    int music_volume=100,sfx_volume=100;
    bool battle_unlocked=false;
    bool film_filter=true;
    bool dynamic_shadows=true;
    bool combat_hud=false;
    bool manual_reload=false;cv_quick::Config quick;
    int reload_key='R',reload_button=1;
    bool trophies=true,trophy_notifications=true;
    int trophy_corner=1;
    bool focus_pause=true; // pause while the game window is out of focus (cv_focus_pause.cpp)
    bool items_menu=false; // F6 item menu in the game (cv_item_menu.cpp), off by default
    bool check_updates=true; // look for a newer GitHub release when the launcher opens (cv_updater.h)
    bool srt=false;
    bool srt_parts[5]={true,true,true,true,false}; // name, health, time, stats, inventory
    bool srt_border=true;
    bool srt_free=false,srt_separate=false;
    int srt_inventory_scale=100,srt_main_x=0,srt_main_y=0,srt_inv_x=1000,srt_inv_y=0;
    int srt_enemies=3, srt_corner=0, srt_scale=100, srt_language=0, srt_opacity=70, srt_inventory_below=0, srt_window=0, srt_boss_bar=0; // 0 follow, 1 EN, 2 PT-BR; 3..13 other launcher languages
    std::wstring game, rap, fw, saves;
    std::wstring region_game[3],region_rap[3];int region_selected=0;
    std::wstring patch_dir;                 /* patch files (patch_files.h) */
    std::vector<std::string> patch_files_on; /* ids of the file patches turned on */
    int key[PA_COUNT][PAD_KEY_SLOTS];   /* 0: empty; the first is what the game's icons show */
    int btn[PA_COUNT][PAD_BTN_SLOTS];   /* -1: empty */
};
/* What the performance overlay can show, in its order; the ids are
 * PERF_OVERLAY's. The frame time now rides with FPS: "ms" keeps its bit so
 * saved settings still line up, but it has no box of its own. */
const struct { const char* id; const wchar_t* name; } k_overlay_items[] = {
    { "fps", L"FPS" }, { "ms", nullptr }, { "cpu", L"CPU" }, { "gpu", L"GPU" }, { "ram", L"RAM" }, { "vram", L"VRAM" },
    { "spu", L"SPU" }, { "rsx", L"RSX" } };
const int NOVERLAY = sizeof k_overlay_items / sizeof k_overlay_items[0];
struct ResOpt { const wchar_t* name; const char* scale; };
const ResOpt k_res[] = {
    { L"480p (854x480)", "0.6667" }, { L"Nativa (720p)", "1" }, { L"1080p (1920x1080)", "1.5" },
    { L"1440p (2560x1440)", "2" }, { L"4K (3840x2160)", "3" } };
const char* k_mode_env[] = { "window", "borderless", "fullscreen" };
std::wstring res_name(int i) { return i == 1 ? T(S_RES_NATIVE) : k_res[i].name; }
std::wstring mode_name(int i) { return T(S_MODE_WINDOW + i); }
std::wstring aa_name(int i) { return T(S_AA_OFF + i); }
std::wstring pose_device_name(int i){return T(i?S_POSE_GPU:S_POSE_CPU);}
std::wstring frame_generation_name(int i){return T(i?S_FRAME_GEN_ON:S_FRAME_GEN_OFF);}
std::wstring vsync_name(int i){return T(i?S_VSYNC_ON:S_VSYNC_OFF);}
bool frame_generation_allowed(){const char* d=getenv("CV_FPS_DIAGNOSTIC"),*p=getenv("CV_FSR_DIAGNOSTIC");return !(d&&!strcmp(d,"1"))||(p&&!strcmp(p,"1"));}
int pose_device_override(){const char* d=std::getenv("CV_FPS_DIAGNOSTIC"),*p=std::getenv("CV_POSE_COMPUTE");return d&&std::strcmp(d,"1")==0&&p?(std::strcmp(p,"1")==0?1:0):-1;}
std::wstring fps_name(int i) { return T(i ? S_FPS_60_DIAG : S_FPS_ORIG); }
std::wstring sharp_name(int i) { return i ? std::to_wstring(i * 10) + L"%" : T(S_AA_OFF); }

/* Game patches (projects/code_veronica/patches.json, lifted as switchable
 * sites): id = the runtime flag name. */
struct PatchOpt { const char* id; int title, desc, credit; };
const PatchOpt k_patches[] = {
    { "cv_door_skip", S_PATCH_DOOR_T, S_PATCH_DOOR_D, S_PATCH_DOOR_C },
    { "cv_skip_logos", S_PATCH_LOGO_T, S_PATCH_LOGO_D, S_PATCH_LOGO_C },   /* boot_skip.cpp */
    { "cv_infinite_health", S_CHEAT_HP_T, S_CHEAT_HP_D, S_PATCH_LOGO_C },
    { "cv_infinite_ammo", S_CHEAT_AMMO_T, S_CHEAT_AMMO_D, S_PATCH_LOGO_C },
};
const int NPATCHES = sizeof k_patches / sizeof k_patches[0];
/* Infinite health and ammunition: shown on the Inventario page, beside the
 * item menu, rather than with the game patches. */
bool is_cheat(int i) { return !strncmp(k_patches[i].id, "cv_infinite_", 12); }

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
                       PA_L3, PA_R3, PA_UP, PA_DOWN, PA_LEFT, PA_RIGHT, PA_SELECT, PA_START,
                       PA_LS_UP, PA_LS_DOWN, PA_LS_LEFT, PA_LS_RIGHT, PA_RS_UP, PA_RS_DOWN, PA_RS_LEFT, PA_RS_RIGHT };
const int NROWS = sizeof k_rows / sizeof k_rows[0];
const wchar_t* k_action_name[PA_COUNT] = {
    L"Cross", L"Circle", L"Square", L"Triangle", L"L1", L"R1", L"L2", L"R2", L"L3", L"R3",
    L"Select", L"Start", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita" };
/* Physical buttons, by icon set (icon_layout): PlayStation, Xbox, Switch. */
const wchar_t* k_pb_name[3][PB_COUNT] = {
    { L"Cross", L"Circle", L"Square", L"Triangle", L"L1", L"R1", L"L2", L"R2", L"L3", L"R3",
      L"Select", L"Start", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita", L"PS", L"Touchpad" },
    { L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT", L"RT", L"LS", L"RS",
      L"Back", L"Start", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita", L"Guide", L"-" },
    { L"B", L"A", L"Y", L"X", L"L", L"R", L"ZL", L"ZR", L"LS", L"RS",
      L"Minus", L"Plus", L"D-pad cima", L"D-pad baixo", L"D-pad esquerda", L"D-pad direita", L"Home", L"Capture" } };
/* The d-pad's names are translated; the rest are the buttons' own names. */
std::wstring action_name(int a)
{
    if(a>=PA_LS_UP&&a<PA_COUNT)return cv_axis_name(g_lang,a-PA_LS_UP);
    return (a >= PA_UP && a <= PA_RIGHT) ? T(S_DPAD_UP + (a - PA_UP)) : k_action_name[a];
}
std::wstring pb_name(int layout, int pb)
{
    if (pb < 0) return L"-";
    if(pb>=CV_AXIS_FIRST&&pb<CV_BINDING_COUNT)return cv_axis_name(g_lang,pb-CV_AXIS_FIRST);
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
// Editable beside all regional executables; defaults are seeded only once.
const wchar_t* k_social_keys[]={L"github",L"youtube"};
const wchar_t* k_social_defaults[]={
    L"https://github.com/kastiellcontato-design/re-cvx-ps3recomp",
    L"https://www.youtube.com/@kastieloficial"};
void init_social_links()
{
    for(int i=0;i<2;i++){
        wchar_t value[2048];
        GetPrivateProfileStringW(L"social",k_social_keys[i],L"__missing__",value,2048,ini_path().c_str());
        if(std::wstring(value)==L"__missing__")ini_set(L"social",k_social_keys[i],k_social_defaults[i]);
    }
}
std::wstring social_url(int index)
{
    wchar_t value[2048];
    GetPrivateProfileStringW(L"social",k_social_keys[index],k_social_defaults[index],value,2048,ini_path().c_str());
    std::wstring url(value);
    // Only web links go to the browser; never execute a path or another scheme.
    if(url.empty()||url.find_first_of(L" \t\r\n\"")!=std::wstring::npos)return {};
    if(_wcsnicmp(url.c_str(),L"https://",8)&&_wcsnicmp(url.c_str(),L"http://",7))return {};
    const auto host=url.find(L"://")+3;
    if(host>=url.size()||url[host]==L'/'||url[host]==L'?'||url[host]==L'#')return {};
    return url;
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

// Host-only regional library. Slots follow k_versions: EU, US, JP.
std::string sfo_field(const fs::path&,const char*);
int region_index(const std::wstring& id){for(int i=0;i<3;i++)if(id==k_versions[i].code)return i;return -1;}
void library_load(Settings& s){
 const std::wstring legacy_game=s.game,legacy_rap=s.rap;
 for(int i=0;i<3;i++){const std::wstring section=L"biblioteca_"+std::wstring(k_versions[i].code);s.region_game[i]=ini_get(section.c_str(),L"pasta",L"");s.region_rap[i]=ini_get(section.c_str(),L"rap",L"");}
 for(int i=0;i<3;i++)if(s.region_rap[i].empty()&&!legacy_rap.empty()&&fs::path(legacy_rap).filename().wstring().find(k_versions[i].code)!=std::wstring::npos)s.region_rap[i]=legacy_rap;
 const int legacy=region_index(widen(sfo_field(fs::path(legacy_game)/L"PARAM.SFO","TITLE_ID").c_str()));
 if(legacy>=0&&s.region_game[legacy].empty()){s.region_game[legacy]=legacy_game;if(legacy!=2)s.region_rap[legacy]=rap_for_game(s,k_versions[legacy].code);}
 const int chosen=region_index(ini_get(L"biblioteca",L"selecionada",legacy>=0?k_versions[legacy].code:GAME_CODE));
 s.region_selected=chosen>=0?chosen:0;
 if(s.region_game[s.region_selected].empty()&&legacy>=0)s.region_selected=legacy;
 if(!s.region_game[s.region_selected].empty()){s.game=s.region_game[s.region_selected];s.rap=s.region_rap[s.region_selected];}
}
void library_save(const Settings& s){
 ini_set(L"biblioteca",L"selecionada",k_versions[s.region_selected].code);
 for(int i=0;i<3;i++){const std::wstring section=L"biblioteca_"+std::wstring(k_versions[i].code);ini_set(section.c_str(),L"pasta",s.region_game[i]);ini_set(section.c_str(),L"rap",s.region_rap[i]);}
}

Settings load_settings()
{
    Settings s;
    s.mode   = _wtoi(ini_get(L"tela", L"modo", L"0").c_str());
    s.res    = _wtoi(ini_get(L"tela", L"resolucao", L"1").c_str());
    s.aa     = _wtoi(ini_get(L"tela", L"antialias", L"0").c_str());
    s.fps    = _wtoi(ini_get(L"tela", L"fps", L"0").c_str()) == 1;
    s.quick.on=_wtoi(ini_get(L"jogo",L"atalhos_equipamento",L"0").c_str())!=0;
    s.quick.key=std::clamp(_wtoi(ini_get(L"jogo",L"atalhos_tecla",L"86").c_str()),0,255);
    s.quick.button=std::clamp(_wtoi(ini_get(L"jogo",L"atalhos_botao",L"4").c_str()),-1,PB_COUNT-1);
    s.quick.edit_key=std::clamp(_wtoi(ini_get(L"jogo",L"atalhos_editar_tecla",L"70").c_str()),0,255);s.quick.edit_button=std::clamp(_wtoi(ini_get(L"jogo",L"atalhos_editar_botao",L"3").c_str()),-1,PB_COUNT-1);
    s.quick.confirm_key=std::clamp(_wtoi(ini_get(L"jogo",L"atalhos_confirmar_tecla",L"13").c_str()),0,255);s.quick.confirm_button=std::clamp(_wtoi(ini_get(L"jogo",L"atalhos_confirmar_botao",L"0").c_str()),-1,PB_COUNT-1);
    for(int d=0;d<4;d++){auto k=L"atalhos_direcao_"+std::to_wstring(d),i=L"atalhos_item_"+std::to_wstring(d);s.quick.keys[d]=std::clamp(_wtoi(ini_get(L"jogo",k.c_str(),std::to_wstring(s.quick.keys[d]).c_str()).c_str()),0,255);s.quick.items[d]=_wtoi(ini_get(L"jogo",i.c_str(),L"0").c_str());}
    s.reload_key=std::clamp(_wtoi(ini_get(L"jogo",L"recarga_tecla",L"82").c_str()),0,255);
    s.reload_button=std::clamp(_wtoi(ini_get(L"jogo",L"recarga_botao",s.region_selected==2?L"8":L"1").c_str()),-1,PB_COUNT-1);
    s.manual_reload=_wtoi(ini_get(L"jogo",L"recarga_manual",L"0").c_str())!=0;
    s.combat_hud=_wtoi(ini_get(L"jogo",L"hud_combate",L"0").c_str())!=0;
    s.game_text_ptbr=_wtoi(ini_get(L"jogo",L"texto_ptbr_teste",L"0").c_str())==1;
    s.pose_device=0;   // CPU interpolation only; a saved interpolacao_gpu=1 is ignored
    s.frame_generation=_wtoi(ini_get(L"tela",L"fsr_frame_generation",L"0").c_str())==1;
    s.vsync=_wtoi(ini_get(L"tela",L"vsync",L"0").c_str())==1;
    if(s.vsync)s.frame_generation=0;   // the two never run together
    if(pose_device_override()>=0)s.pose_device=pose_device_override();
    if(opt30_comparison())s.fps=0;   // this process only; saved FPS choice is preserved
    // Forced anisotropy temporarily retired: never restore legacy 2x-16x preferences.
    s.brightness = _wtoi(ini_get(L"tela", L"brilho", L"0").c_str());
    s.contrast = _wtoi(ini_get(L"tela", L"contraste", L"100").c_str());
    s.saturation = _wtoi(ini_get(L"tela", L"saturacao", L"100").c_str());
    s.sharpness = std::clamp(_wtoi(ini_get(L"tela", L"nitidez", L"0").c_str()) / 10 * 10, 0, 100);
    s.layout = _wtoi(ini_get(L"controles", L"layout", L"2").c_str());
    s.rumble = std::clamp(_wtoi(ini_get(L"controles", L"vibracao", L"100").c_str()), 0, 200);
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
    if (s.layout < 0 || s.layout > 4) s.layout = 2;
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
    s.srt_free=_wtoi(ini_get(L"srt",L"mover_livremente",L"0").c_str())!=0;
    s.srt_separate=_wtoi(ini_get(L"srt",L"separar_inventario",L"0").c_str())!=0;
    s.srt_inventory_scale=std::clamp(_wtoi(ini_get(L"srt",L"tamanho_inventario",L"100").c_str()),50,200);
    s.srt_main_x=std::clamp(_wtoi(ini_get(L"srt",L"painel_x",L"0").c_str()),0,1000);
    s.srt_main_y=std::clamp(_wtoi(ini_get(L"srt",L"painel_y",L"0").c_str()),0,1000);
    s.srt_inv_x=std::clamp(_wtoi(ini_get(L"srt",L"inventario_x",L"1000").c_str()),0,1000);
    s.srt_inv_y=std::clamp(_wtoi(ini_get(L"srt",L"inventario_y",L"0").c_str()),0,1000);
    s.srt_scale=std::clamp(_wtoi(ini_get(L"srt",L"tamanho",L"100").c_str()),50,200);
    s.test_tools=_wtoi(ini_get(L"testes",L"mapas",L"0").c_str())!=0;
    s.items_menu=_wtoi(ini_get(L"inventario",L"menu_itens",L"0").c_str())!=0;
    s.music_volume=std::clamp(_wtoi(ini_get(L"audio",L"musica",L"100").c_str()),0,100);
    s.sfx_volume=std::clamp(_wtoi(ini_get(L"audio",L"efeitos",L"100").c_str()),0,100);
    s.trophy_corner=std::clamp(_wtoi(ini_get(L"trofeus",L"posicao",L"1").c_str()),0,3);
    s.trophies=_wtoi(ini_get(L"trofeus",L"registrar",L"1").c_str())!=0;
    s.trophy_notifications=_wtoi(ini_get(L"trofeus",L"notificacoes",L"1").c_str())!=0;
    s.dynamic_shadows=_wtoi(ini_get(L"tela",L"sombras_dinamicas",L"1").c_str())!=0;
    s.film_filter=_wtoi(ini_get(L"tela",L"filtro_filme",L"1").c_str())!=0;
    s.battle_unlocked=_wtoi(ini_get(L"extras",L"battle_game",L"0").c_str())!=0;
    s.focus_pause=_wtoi(ini_get(L"jogo",L"pausar_sem_foco",L"1").c_str())!=0;
    s.check_updates=_wtoi(ini_get(L"launcher",L"procurar_atualizacoes",L"1").c_str())!=0;
    s.srt_opacity=std::clamp(_wtoi(ini_get(L"srt",L"opacidade",L"70").c_str()),0,100);
    s.srt_inventory_below=std::clamp(_wtoi(ini_get(L"srt",L"inventario_embaixo",L"0").c_str()),0,1);
    s.srt_window=std::clamp(_wtoi(ini_get(L"srt",L"janela",L"0").c_str()),0,1);
    s.srt_boss_bar=std::clamp(_wtoi(ini_get(L"srt",L"barra_chefe",L"0").c_str()),0,2);
    s.srt_border=_wtoi(ini_get(L"srt",L"borda",L"1").c_str())!=0;
    s.srt_language=std::clamp(_wtoi(ini_get(L"srt",L"idioma",L"0").c_str()),0,int(LANG_COUNT));
    s.overlay = _wtoi(ini_get(L"overlay", L"mostrar", L"0").c_str()) != 0;
    for (int i = 0; i < NSHADER_OPTS; i++)
        s.shader_opt[i] = _wtoi(ini_get(L"shaders", k_shader_opts[i].key, L"1").c_str()) != 0;
    s.shader_compiler=ini_get(L"shaders",L"compilador",L"dxc")==L"fxc"?1:0;
    s.tex_dir = ini_get(L"texturas", L"pasta", L"");
    s.tex_dump_dir = ini_get(L"texturas", L"pasta_extraidas", L"");
    s.tex_replace = _wtoi(ini_get(L"texturas", L"substituir", L"0").c_str()) != 0;
    s.tex_dump = _wtoi(ini_get(L"texturas", L"extrair", L"0").c_str()) != 0;
    s.tex_dds=ini_get(L"texturas",L"formato",L"png")==L"dds";
    s.tex_dump_all = _wtoi(ini_get(L"texturas", L"extrair_todos", L"0").c_str()) != 0;
    if(s.tex_dump_all)s.tex_dump=false;
    s.overlay_items = _wtoi(ini_get(L"overlay", L"itens", L"3").c_str()) & ((1 << NOVERLAY) - 1);
    s.overlay_corner = std::clamp(_wtoi(ini_get(L"overlay", L"posicao", L"1").c_str()), 0, 3);
    s.overlay_layout = std::clamp(_wtoi(ini_get(L"overlay", L"disposicao", L"0").c_str()), 0, 1);
    s.overlay_size = std::clamp(_wtoi(ini_get(L"overlay", L"tamanho", L"1").c_str()), 0, 2);
    s.overlay_text_only = std::clamp(_wtoi(ini_get(L"overlay", L"so_texto", L"0").c_str()), 0, 1);
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
            std::wstring b = ini_get(L"controles", bid.c_str(), widen(cv_binding_id(k ? -1 : k_pad_default_btn[a])));
            std::string nb(b.begin(), b.end());
            int pb = cv_binding_from_id(nb.c_str());
            s.btn[a][k] = pb >= 0 || nb == "none" ? pb : (k ? -1 : k_pad_default_btn[a]);
        }
    }
    library_load(s);
    return s;
}

void save_settings(const Settings& s)
{
    library_save(s);
    ini_set(L"tela", L"modo", std::to_wstring(s.mode));
    ini_set(L"tela", L"resolucao", std::to_wstring(s.res));
    ini_set(L"tela", L"antialias", std::to_wstring(s.aa));
    if(!opt30_comparison())ini_set(L"tela", L"fps", std::to_wstring(s.fps));
    if(pose_device_override()<0)ini_set(L"tela",L"interpolacao_gpu",std::to_wstring(s.pose_device));
    ini_set(L"tela",L"fsr_frame_generation",std::to_wstring(s.frame_generation));
    ini_set(L"tela",L"vsync",std::to_wstring(s.vsync));
    ini_set(L"tela", L"anisotropico", L"0");
    ini_set(L"tela", L"brilho", std::to_wstring(s.brightness));
    ini_set(L"tela", L"contraste", std::to_wstring(s.contrast));
    ini_set(L"tela", L"saturacao", std::to_wstring(s.saturation));
    ini_set(L"tela", L"nitidez", std::to_wstring(s.sharpness));
    ini_set(L"jogo",L"atalhos_editar_tecla",std::to_wstring(s.quick.edit_key));ini_set(L"jogo",L"atalhos_editar_botao",std::to_wstring(s.quick.edit_button));
    ini_set(L"jogo",L"atalhos_confirmar_tecla",std::to_wstring(s.quick.confirm_key));ini_set(L"jogo",L"atalhos_confirmar_botao",std::to_wstring(s.quick.confirm_button));
    ini_set(L"jogo",L"atalhos_equipamento",std::to_wstring(s.quick.on));ini_set(L"jogo",L"atalhos_tecla",std::to_wstring(s.quick.key));ini_set(L"jogo",L"atalhos_botao",std::to_wstring(s.quick.button));
    for(int d=0;d<4;d++){auto k=L"atalhos_direcao_"+std::to_wstring(d),i=L"atalhos_item_"+std::to_wstring(d);ini_set(L"jogo",k.c_str(),std::to_wstring(s.quick.keys[d]));ini_set(L"jogo",i.c_str(),std::to_wstring(s.quick.items[d]));}
    ini_set(L"jogo",L"recarga_tecla",std::to_wstring(s.reload_key));
    ini_set(L"jogo",L"recarga_botao",std::to_wstring(s.reload_button));
    ini_set(L"jogo",L"recarga_manual",s.manual_reload?L"1":L"0");
    ini_set(L"jogo",L"hud_combate",s.combat_hud?L"1":L"0");
    ini_set(L"jogo",L"texto_ptbr_teste",std::to_wstring(s.game_text_ptbr));
    ini_set(L"controles", L"layout", std::to_wstring(s.layout));
    ini_set(L"controles", L"vibracao", std::to_wstring(s.rumble));
    ini_set(L"launcher", L"idioma", g_lang_pick < 0 ? std::wstring(L"system") : widen(k_lang_id[g_lang_pick]));
    for (int i = 0; i < NPATCHES; i++)
        ini_set(L"patches", widen(k_patches[i].id).c_str(), s.patch[i] ? L"1" : L"0");
    ini_set(L"testes",L"mapas",s.test_tools?L"1":L"0");
    ini_set(L"inventario",L"menu_itens",s.items_menu?L"1":L"0");
    ini_set(L"audio",L"musica",std::to_wstring(s.music_volume));
    ini_set(L"audio",L"efeitos",std::to_wstring(s.sfx_volume));
    ini_set(L"extras",L"battle_game",s.battle_unlocked?L"1":L"0");
    ini_set(L"tela",L"filtro_filme",s.film_filter?L"1":L"0");
    ini_set(L"tela",L"sombras_dinamicas",s.dynamic_shadows?L"1":L"0");
    ini_set(L"trofeus",L"posicao",std::to_wstring(s.trophy_corner));
    ini_set(L"trofeus",L"registrar",s.trophies?L"1":L"0");
    ini_set(L"trofeus",L"notificacoes",s.trophy_notifications?L"1":L"0");
    ini_set(L"jogo",L"pausar_sem_foco",s.focus_pause?L"1":L"0");
    ini_set(L"launcher",L"procurar_atualizacoes",s.check_updates?L"1":L"0");
    ini_set(L"srt", L"mostrar",s.srt?L"1":L"0");
    { const wchar_t* keys[]={L"nome",L"vida",L"tempo",L"contadores",L"inventario"};
      for(int i=0;i<5;i++)ini_set(L"srt",keys[i],s.srt_parts[i]?L"1":L"0"); }
    ini_set(L"srt",L"inimigos",std::to_wstring(s.srt_enemies));
    ini_set(L"srt",L"posicao",std::to_wstring(s.srt_corner));
    ini_set(L"srt",L"mover_livremente",s.srt_free?L"1":L"0");
    ini_set(L"srt",L"separar_inventario",s.srt_separate?L"1":L"0");
    ini_set(L"srt",L"tamanho_inventario",std::to_wstring(s.srt_inventory_scale));
    ini_set(L"srt",L"painel_x",std::to_wstring(s.srt_main_x));ini_set(L"srt",L"painel_y",std::to_wstring(s.srt_main_y));
    ini_set(L"srt",L"inventario_x",std::to_wstring(s.srt_inv_x));ini_set(L"srt",L"inventario_y",std::to_wstring(s.srt_inv_y));
    ini_set(L"srt",L"tamanho",std::to_wstring(s.srt_scale));
    ini_set(L"srt",L"idioma",std::to_wstring(s.srt_language));
    ini_set(L"srt",L"opacidade",std::to_wstring(s.srt_opacity));
    ini_set(L"srt",L"inventario_embaixo",std::to_wstring(s.srt_inventory_below));
    ini_set(L"srt",L"janela",std::to_wstring(s.srt_window));
    ini_set(L"srt",L"barra_chefe",std::to_wstring(s.srt_boss_bar));
    ini_set(L"srt",L"borda",s.srt_border?L"1":L"0");
    ini_set(L"overlay", L"mostrar", s.overlay ? L"1" : L"0");
    ini_set(L"overlay", L"itens", std::to_wstring(s.overlay_items));
    ini_set(L"overlay", L"posicao", std::to_wstring(s.overlay_corner));
    ini_set(L"overlay", L"disposicao", std::to_wstring(s.overlay_layout));
    ini_set(L"overlay", L"tamanho", std::to_wstring(s.overlay_size));
    ini_set(L"overlay", L"so_texto", std::to_wstring(s.overlay_text_only));
    for (int i = 0; i < NSHADER_OPTS; i++)
        ini_set(L"shaders", k_shader_opts[i].key, s.shader_opt[i] ? L"1" : L"0");
    ini_set(L"shaders",L"compilador",s.shader_compiler?L"fxc":L"dxc");
    ini_set(L"texturas", L"pasta", s.tex_dir);
    ini_set(L"texturas", L"pasta_extraidas", s.tex_dump_dir);
    ini_set(L"texturas", L"substituir", s.tex_replace ? L"1" : L"0");
    ini_set(L"texturas", L"extrair", s.tex_dump ? L"1" : L"0");
    ini_set(L"texturas",L"formato",s.tex_dds?L"dds":L"png");
    ini_set(L"texturas", L"extrair_todos", s.tex_dump_all ? L"1" : L"0");
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
                    widen(cv_binding_id(s.btn[a][k])));
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
std::wstring check_game_uncached(const std::wstring& g, bool* ok);
/* Pages call this on every repaint (several folders per frame on Jogos):
 * reuse a result for a second instead of re-reading PARAM.SFO per frame. */
std::wstring check_game(const std::wstring& g, bool* ok)
{
    struct Entry { std::wstring path, msg; bool ok; int lang; ULONGLONG at; };
    static std::vector<Entry> cache;
    const ULONGLONG now = GetTickCount64();
    for (auto& e : cache)
        if (e.path == g && e.lang == g_lang && now - e.at < 1000) { *ok = e.ok; return e.msg; }
    std::wstring msg = check_game_uncached(g, ok);
    for (auto& e : cache) if (e.path == g) { e = { g, msg, *ok, g_lang, now }; return msg; }
    cache.push_back({ g, msg, *ok, g_lang, now });
    return msg;
}
std::wstring check_game_uncached(const std::wstring& g, bool* ok)
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
enum Page { P_INICIO, P_JOGO, P_SAVES, P_CONTROLES, P_ATALHOS, P_PATCHES, P_MODS, P_SRT, P_INVENTARIO, P_DESEMPENHO, P_AVANCADO, P_TROPHIES, P_HUD, P_COUNT };
/* Each page's name in the side bar. */
const int k_nav[P_COUNT] = { S_NAV_HOME, S_NAV_GAME, S_NAV_GAME /* Saves: named by design::SAVES */, S_NAV_CONTROLS, S_NAV_SHORTCUTS, S_NAV_PATCHES, S_NAV_MODS, S_NAV_SRT, S_NAV_ITEMS, S_NAV_PERF, S_NAV_ADVANCED, S_NAV_GAME, S_NAV_GAME };
/* Side bar order: the game library first. */
const int k_nav_order[P_COUNT] = { P_JOGO, P_INICIO, P_HUD, P_SAVES, P_CONTROLES, P_ATALHOS, P_PATCHES, P_MODS, P_SRT, P_INVENTARIO, P_TROPHIES, P_DESEMPENHO, P_AVANCADO };
enum Capture { CAP_NONE, CAP_KEY, CAP_BTN, CAP_RELOAD_KEY, CAP_RELOAD_BTN, CAP_QUICK_KEY, CAP_QUICK_BTN, CAP_QUICK_DIR, CAP_QUICK_EDIT_KEY, CAP_QUICK_EDIT_BTN, CAP_QUICK_CONFIRM_KEY, CAP_QUICK_CONFIRM_BTN };

struct Hit { RectF r; int id; };
struct UI {
    HWND hwnd = nullptr;
    Settings s;
    Page page = P_INICIO;
    int hover = -1;
    int srt_layout_drag=-1;float srt_grab_x=0,srt_grab_y=0;bool srt_layout_dirty=false;
    int srt_drag = -1; bool srt_drag_dirty = false;
    int open_dd = -1;            /* open dropdown id */
    Capture cap = CAP_NONE; int cap_action = -1, cap_slot = 0; ULONGLONG cap_since = 0;
    std::vector<FilePatch> fpatches;   /* read from s.patch_dir when it changes or the page opens */
    int fpatch_files = 0; float patch_scroll = 0, patch_scroll_max = 0;
    float page_scroll[P_COUNT] = {}, page_scroll_max[P_COUNT] = {};   /* each page scrolls on its own */
    bool page_drag = false; float page_grab = 0;                      /* dragging the page's scroll bar */
    RectF page_track, page_thumb;                                      /* where the bar was drawn */
    std::wstring log_msg;   /* after Copy / Save on the Advanced page */
    uint32_t pad_prev[PAD_PHYS_MAX] = {};
    uint32_t test_combo_prev=0;
    uint32_t items_combo_prev=0;   /* the F6 item menu chord, per controller */
    uint32_t shot_combo_prev=0;    /* the F10 screenshot chord */
    std::vector<Hit> hits;
    std::wstring play_msg;
    int result = 0;              /* 1 = play */
    Bitmap* banner = nullptr;
    Bitmap* brand_logo=nullptr;
    std::wstring social_urls[2];
    Bitmap* castle=nullptr;Bitmap* cover[3]={},*cover_bg[3]={};std::wstring art_path[3];
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
    ID_DD_MODE = 200, ID_DD_RES = 201, ID_DD_AA = 202, ID_DD_LANG = 203, ID_DD_FPS = 204, ID_DD_SHARP = 206, ID_DD_POSE_DEVICE = 207, ID_DD_FRAME_GEN = 208, ID_DD_VSYNC = 209, ID_DD_FILM = 210, ID_DD_GAME_TEXT = 211, ID_DD_SHADOWS=212, ID_DD_COMBAT_HUD=213, ID_DD_MANUAL_RELOAD=214, ID_DD_ITEM = 300,  /* + item index */
    ID_DD_UP = 390, ID_DD_DOWN = 391,
    ID_PLAY = 400, ID_RESUME, ID_QUIT,
    ID_GAME_BROWSE = 500, ID_GAME_OPEN, ID_RAP_BROWSE, ID_RAP_OPEN, ID_SAVE_BROWSE, ID_SAVE_OPEN, ID_SAVE_DEFAULT,
    ID_LAYOUT_PS = 600, ID_LAYOUT_XB, ID_LAYOUT_AUTO, ID_LAYOUT_KB, ID_LAYOUT_SW, ID_RUMBLE,
    ID_KEY = 700, ID_BTN = 800, ID_RESTORE = 900, ID_PATCH = 1000,  /* + index (keys and buttons: action * slots + slot) */
    ID_TEST_ENABLE=2500, ID_TEST_OPEN,
    ID_FOCUS_PAUSE=2550, ID_SHOT_OPEN,
    ID_SRT_BORDER=2560,
    ID_SRT_FREE=2760,ID_SRT_SEPARATE,ID_SRT_INV_SCALE,ID_SRT_MAIN_MOVE,ID_SRT_INV_MOVE,ID_SRT_RESET,
    ID_ITEMS_ENABLE=2570,
    ID_CACHE_CLEAR = 1100, ID_CACHE_OPEN,
    ID_KEY_CLEAR = 1200, ID_BTN_CLEAR = 1300,                      /* + action * slots + slot */
    ID_OVERLAY = 1400, ID_OVERLAY_ITEM = 1410,                     /* + item */
    ID_PATCH_DIR_BROWSE = 1500, ID_PATCH_DIR_OPEN, ID_PATCH_DIR_CLEAR, ID_FPATCH = 1600,  /* + file patch index */
    ID_LOG_VIEW = 2100, ID_LOG_COPY, ID_LOG_SAVE, ID_LOG_FOLDER,
    ID_SHADER_COMPILER=3090, ID_SHADER_OPT = 2200,                                          /* + k_shader_opts index */
    ID_SRT=2400, ID_SRT_PART=2410, ID_DD_SRT_ENEMIES=2420, ID_DD_SRT_CORNER, ID_DD_SRT_LANGUAGE, ID_SRT_SCALE, ID_SRT_OPACITY,
    ID_DD_OV_CORNER = 2450, ID_DD_OV_LAYOUT, ID_DD_SRT_INVPOS, ID_DD_SRT_WINDOW, ID_DD_SRT_BOSSBAR, ID_DD_OV_SIZE, ID_DD_OV_BG,
    ID_TEX_BROWSE = 2300, ID_TEX_OPEN, ID_TEX_REPLACE, ID_TEX_DUMP, ID_TEX_DUMP_BROWSE, ID_TEX_DUMP_OPEN, ID_TEX_ALL, ID_TEX_ALL_RUN, ID_TEX_FORMAT_PNG, ID_TEX_FORMAT_DDS,
    ID_SOCIAL_GITHUB=2580, ID_SOCIAL_YOUTUBE,
    ID_UPD_TOGGLE=2590, ID_UPD_CHECK, ID_UPD_INSTALL, ID_UPD_LATER, ID_UPD_CANCEL, ID_UPD_CLOSE, ID_UPD_NOTES_THUMB, ID_UPD_NOTES_TRACK,
    ID_TROPHY_ENABLE=2800,ID_TROPHY_NOTIFY,ID_TROPHY_RESET,ID_TROPHY_REFRESH,ID_DD_TROPHY_FILTER,ID_DD_TROPHY_CORNER,
    ID_QUICK_ON=3000,ID_QUICK_KEY,ID_QUICK_BUTTON,ID_QUICK_KEY_CLEAR,ID_QUICK_BUTTON_CLEAR,ID_QUICK_RESET,ID_QUICK_DIR=3010,ID_QUICK_DIR_CLEAR=3020,ID_QUICK_SLOT=3030,ID_QUICK_EDIT_KEY=3040,ID_QUICK_EDIT_BUTTON,ID_QUICK_EDIT_KEY_CLEAR,ID_QUICK_EDIT_BUTTON_CLEAR,ID_QUICK_CONFIRM_KEY,ID_QUICK_CONFIRM_BUTTON,ID_QUICK_CONFIRM_KEY_CLEAR,ID_QUICK_CONFIRM_BUTTON_CLEAR,
    ID_HUD_ENABLED=2900,ID_HUD_RELOAD,ID_HUD_RELOAD_KEY,ID_HUD_RELOAD_BUTTON,ID_HUD_RELOAD_KEY_CLEAR,ID_HUD_RELOAD_BUTTON_CLEAR,ID_HUD_RELOAD_RESET,
    ID_PAGE_THUMB = 2600, ID_PAGE_TRACK,
};

/* Page scrolling. A page draws in its own coordinates, as tall as it needs;
 * paint() shifts it up by the page's scroll and clips it above the Jogar bar
 * (PAGE_BOTTOM), so a page that grows past the window scrolls (wheel or bar)
 * instead of running under the button. While a page draws, hit areas and the
 * open dropdown's anchor move with it, and the lowest thing drawn sets how far
 * the page can scroll. */
const float PAGE_BOTTOM = H - 92.0f;
bool  g_in_page = false;
float g_page_dy = 0, g_page_end = 0;

void hit(float x, float y, float w, float h, int id)
{
    if (g_in_page) {
        g_page_end = std::max(g_page_end, y + h);
        y -= g_page_dy;
        const float top = std::max(y, 0.0f), bottom = std::min(y + h, PAGE_BOTTOM);
        if (bottom <= top) return;   /* scrolled out of view */
        h = bottom - top; y = top;
    }
    g.hits.push_back({ RectF(x, y, w, h), id });
}

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
    if (g_in_page) g_page_end = std::max(g_page_end, y + (hh > 0 ? hh : f.GetHeight(&gr)));
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
    /* The fonts draw the PlayStation square (□) tiny: draw it as a box instead,
     * between the text around it. Left-aligned lines only (all the hints). */
    const size_t sq = t.find_first_of(L"□◻");
    if (sq != std::wstring::npos && h == StringAlignmentNear && w > 0 && hh > 0) {
        StringFormat tf(StringFormat::GenericTypographic()); tf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
        tf.SetLineAlignment(StringAlignmentCenter);
        const float side = f.GetHeight(&gr) * 0.55f; float cx = x + 2;
        size_t from = 0;
        for (size_t at = sq; ; at = t.find_first_of(L"□◻", from)) {
            const std::wstring part = t.substr(from, at == std::wstring::npos ? std::wstring::npos : at - from);
            if (!part.empty()) {
                RectF m; gr.MeasureString(part.c_str(), -1, &f, PointF(0, 0), &tf, &m);
                gr.DrawString(part.c_str(), -1, &f, RectF(cx, y, std::max(0.f, x + w - cx), hh), &tf, &b);
                cx += m.Width;
            }
            if (at == std::wstring::npos) break;
            Pen box(c, 1.3f); gr.DrawRectangle(&box, cx + 2, y + (hh - side) / 2, side, side);
            cx += side + 4; from = at + 1;
        }
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
 * lit. layout 0 = PlayStation, 1 = Xbox, 2 = Switch; pb is a PB_* position. */
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
    cv_keycap::draw(gr, vk > 0 ? cv_key_cap_label(vk) : L"", x, y, s, s);
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
    if(pb>=CV_AXIS_FIRST&&pb<CV_BINDING_COUNT){
        int n=pb-CV_AXIS_FIRST,d=n%4;
        const float dx=d==2?-1.f:d==3?1.f:0.f,dy=d==0?-1.f:d==1?1.f:0.f;
        Pen inner(G_LINE,1.1f*k);gr.DrawEllipse(&inner,cx-6*k,cy-6*k,12*k,12*k);
        g_label(gr,n<4?L"L":L"R",RectF(cx-5*k,cy-5*k,10*k,10*k),7*k,G_FILL);
        PointF tip(cx+dx*10*k,cy+dy*10*k),base(cx+dx*7*k,cy+dy*7*k);
        PointF tri[]={tip,PointF(base.X-dy*2*k,base.Y+dx*2*k),PointF(base.X+dy*2*k,base.Y-dx*2*k)};
        SolidBrush arrow(G_FILL);gr.FillPolygon(&arrow,tri,3);return;
    }
    SolidBrush fill(G_FILL);
    switch (pb) {
    case PB_SOUTH: case PB_EAST: case PB_WEST: case PB_NORTH: {
        static const Color ps[4] = { Color(255, 124, 178, 240), Color(255, 255, 102, 110),
                                     Color(255, 222, 140, 210), Color(255, 70, 220, 170) };
        static const Color xb[4] = { Color(255, 110, 200, 80), Color(255, 240, 80, 70),
                                     Color(255, 70, 150, 245), Color(255, 245, 200, 50) };
        const int i = pb - PB_SOUTH;
        if (layout == 2) {           /* Switch: one cream colour, B at the bottom, A on the right */
            static const wchar_t* L[4] = { L"B", L"A", L"Y", L"X" };
            g_label(gr, L[i], RectF(x, y, s, s), 12.0f * k, Color(255, 236, 228, 212));
            break;
        }
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
        g_label(gr, layout == 1 ? (pb == PB_L1 ? L"LB" : L"RB") : layout == 2 ? (pb == PB_L1 ? L"L" : L"R")
                                : (pb == PB_L1 ? L"L1" : L"R1"), r, 7.5f * k, G_INK);
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
        g_label(gr, layout == 1 ? (pb == PB_L2 ? L"LT" : L"RT") : layout == 2 ? (pb == PB_L2 ? L"ZL" : L"ZR")
                                : (pb == PB_L2 ? L"L2" : L"R2"),
                RectF(r.X, r.Y + 2 * k, r.Width, r.Height - 2 * k), (layout == 2 ? 6.0f : 7.0f) * k, G_INK);
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
        if (layout == 2) {           /* Switch Minus */
            Pen p(G_LINE, 2.0f * k); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
            gr.DrawLine(&p, cx - 5 * k, cy, cx + 5 * k, cy);
        } else if (layout == 1) {    /* Xbox View: two overlapping windows */
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
    case PB_START: {                 /* Options / Menu: three lines; Switch Plus */
        Pen p(G_LINE, layout == 2 ? 2.0f * k : 1.8f * k); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
        if (layout == 2) { gr.DrawLine(&p, cx - 5 * k, cy, cx + 5 * k, cy); gr.DrawLine(&p, cx, cy - 5 * k, cx, cy + 5 * k); break; }
        for (int i = -1; i <= 1; i++) gr.DrawLine(&p, cx - 5 * k, cy + i * 3.6f * k, cx + 5 * k, cy + i * 3.6f * k);
        break; }
    case PB_HOME: case PB_TOUCH:
        if (layout == 2) {           /* Switch Home (a house) and Capture (a ringed square) */
            if (pb == PB_HOME) {
                PointF house[7] = { PointF(cx, cy - 6 * k), PointF(cx + 6 * k, cy - 0.5f * k), PointF(cx + 4 * k, cy - 0.5f * k),
                                    PointF(cx + 4 * k, cy + 5 * k), PointF(cx - 4 * k, cy + 5 * k),
                                    PointF(cx - 4 * k, cy - 0.5f * k), PointF(cx - 6 * k, cy - 0.5f * k) };
                gr.FillPolygon(&fill, house, 7);
                SolidBrush door(Color(255, 40, 34, 34)); gr.FillRectangle(&door, cx - 1.3f * k, cy + 1.5f * k, 2.6f * k, 3.5f * k);
            } else {
                Pen p(G_LINE, 1.4f * k);
                GraphicsPath* sq = round_rect(RectF(cx - 5.5f * k, cy - 5.5f * k, 11 * k, 11 * k), 2 * k); gr.DrawPath(&p, sq); delete sq;
                gr.DrawEllipse(&p, cx - 2.8f * k, cy - 2.8f * k, 5.6f * k, 5.6f * k);
            }
            break;
        }
        [[fallthrough]];
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
    if (id >= 0 && id == g.open_dd) { g_dd_anchor = r; g_dd_anchor.Y -= g_page_dy; }
}

void section(Graphics& gr, const std::wstring& t, float x, float y, float w, const Font& f)
{
    text(gr, t, x, y, f, C_RED);
    Pen pen(C_LINE, 1);
    gr.DrawLine(&pen, x, y + 26, x + w, y + 26);
}


/* Release shown under the sidebar, beside the credit. Bump it per release. */
const wchar_t* const k_launcher_version = L"v1.1.11";

/* ---------------------------------------------------------------- updates
 * cv_updater.h does the network and file work on threads of its own, which
 * post WM_UPDATE back here; the window shows the offer, the download and
 * the result as a dialog over the launcher. k_launcher_version is the
 * version compared with the release's tag. */
namespace upd_text = cv_update_text;
const wchar_t* ux(upd_text::Id id) { return upd_text::texts[g_lang][id]; }
const UINT WM_UPDATE = WM_APP + 30;
enum { UPD_MSG_CHECKED = 1, UPD_MSG_PROGRESS, UPD_MSG_INSTALLING, UPD_MSG_FINISHED };   /* WM_UPDATE's wp */
enum UpdStep { UPD_NONE, UPD_CHECKING, UPD_OFFER, UPD_DOWNLOADING, UPD_INSTALLING, UPD_FAILED };
struct UpdState {
    UpdStep step = UPD_NONE;
    bool manual = false;            /* "Procurar agora": also report "up to date" and errors */
    cv_update::Release rel;
    std::wstring notes;             /* rel.notes made readable */
    float notes_scroll=0,notes_max=0,notes_height=0,notes_grab=0,notes_width=0;
    bool notes_drag=false;int notes_lang=-1;
    std::wstring measured_notes;
    RectF notes_box,notes_view,notes_track,notes_thumb;
    cv_update::Progress prog;
    cv_update::Error err = cv_update::OK;
    std::wstring status;            /* the line beside "Procurar agora" */
};
UpdState g_upd;
struct UpdCheck { cv_update::Error err; cv_update::Release rel; };

std::wstring upd_fill(std::wstring t, const std::wstring& a, const std::wstring& b = L"")
{
    if (size_t at = t.find(L"%1"); at != std::wstring::npos) t.replace(at, 2, a);
    if (size_t at = t.find(L"%2"); at != std::wstring::npos) t.replace(at, 2, b);
    return t;
}
const wchar_t* upd_error_text(cv_update::Error e)
{
    switch (e) {
    case cv_update::ERR_NO_RELEASE:  return ux(upd_text::ERR_NO_RELEASE);
    case cv_update::ERR_NO_PACKAGE:  return ux(upd_text::ERR_NO_PACKAGE);
    case cv_update::ERR_DOWNLOAD: case cv_update::ERR_CANCELED: return ux(upd_text::ERR_DOWNLOAD);
    case cv_update::ERR_CHECKSUM:    return ux(upd_text::ERR_CHECKSUM);
    case cv_update::ERR_EXTRACT:     return ux(upd_text::ERR_EXTRACT);
    case cv_update::ERR_BAD_PACKAGE: return ux(upd_text::ERR_BAD_PACKAGE);
    case cv_update::ERR_WRITE:       return ux(upd_text::ERR_WRITE);
    default:                         return ux(upd_text::ERR_NETWORK);
    }
}
/* Release notes are Markdown: drop the heading marks and bold, show bullets. */
std::wstring upd_plain_notes(const std::wstring& md)
{
    std::wstring out, line;
    auto flush = [&] {
        size_t i = line.find_first_not_of(L" \t");
        std::wstring l = i == std::wstring::npos ? L"" : line.substr(i);
        while (!l.empty() && l[0] == L'#') l.erase(0, 1);
        if (l.size() > 1 && (l[0] == L'-' || l[0] == L'*') && l[1] == L' ') l = L"•" + l.substr(1);
        for (size_t at; (at = l.find(L"**")) != std::wstring::npos; ) l.erase(at, 2);
        i = l.find_first_not_of(L" \t");
        out += (i == std::wstring::npos ? L"" : l.substr(i)) + L"\n";
        line.clear();
    };
    for (wchar_t c : md) { if (c == L'\r') continue; if (c == L'\n') flush(); else line += c; }
    if (!line.empty()) flush();
    while (!out.empty() && (out.back() == L'\n' || out.back() == L' ')) out.pop_back();
    return out;
}

void upd_check(bool manual)
{
    if (g_upd.step != UPD_NONE) return;
    g_upd.step = UPD_CHECKING; g_upd.manual = manual; g_upd.status.clear();
    const HWND hwnd = g.hwnd;
    std::thread([hwnd] {
        auto* r = new UpdCheck{};
        try{r->err = cv_update::fetch_latest(&r->rel);}catch(...){r->err=cv_update::ERR_NETWORK;}
        if (!PostMessageW(hwnd, WM_UPDATE, UPD_MSG_CHECKED, LPARAM(r))) delete r;
    }).detach();
}
void upd_start_install()
{
    wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL, self, MAX_PATH);
    const fs::path exe(self);
    g_upd.step = UPD_DOWNLOADING; g_upd.prog.done = 0; g_upd.prog.total = g_upd.rel.asset_size; g_upd.prog.cancel = false;
    const HWND hwnd = g.hwnd;
    const cv_update::Release rel = g_upd.rel;
    std::thread([hwnd, rel, exe] {
        std::atomic<bool> busy{true};
        std::thread tick([&] {   /* the bar moves ten times a second */
            while (busy) { PostMessageW(hwnd, WM_UPDATE, UPD_MSG_PROGRESS, 0); Sleep(100); }
        });
        fs::path zip;
        cv_update::Error e=cv_update::ERR_DOWNLOAD;
        try{e = cv_update::download(rel, cv_update::work_dir(), &g_upd.prog, &zip);}catch(...){e=cv_update::ERR_DOWNLOAD;}
        busy = false; tick.join();
        if(e==cv_update::OK&&g_upd.prog.cancel)e=cv_update::ERR_CANCELED;
        if (e == cv_update::OK) {
            PostMessageW(hwnd, WM_UPDATE, UPD_MSG_INSTALLING, 0);
            try{
                auto install_directory=exe.parent_path();
                std::wstring entry_name=exe.filename().wstring();
#ifdef CV_DISTRIBUTION
                if(install_directory.filename()==L"bin" &&
                   fs::is_regular_file(install_directory.parent_path()/L"Code Veronica.exe")) {
                    install_directory=install_directory.parent_path();
                    entry_name=L"Code Veronica.exe";
                }
#endif
                e=cv_update::install(zip,install_directory,entry_name);
            }catch(...){e=cv_update::ERR_WRITE;}
        }
        PostMessageW(hwnd, WM_UPDATE, UPD_MSG_FINISHED, LPARAM(e));
    }).detach();
}
/* The new files are in place: start the new executable, which deletes the
 * old copies once this process has gone (cv_update::cleanup), and leave. */
void upd_relaunch()
{
    wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    SetEnvironmentVariableW(L"CV_UPDATE_DONE", std::to_wstring(GetCurrentProcessId()).c_str());
    std::wstring cmd = L"\"" + std::wstring(self) + L"\"";
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi = {};
    if (CreateProcessW(self, &cmd[0], NULL, NULL, FALSE, 0, NULL, cwd, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        ExitProcess(0);
    }
    SetEnvironmentVariableW(L"CV_UPDATE_DONE",nullptr);
    g_upd.step=UPD_FAILED;g_upd.err=cv_update::ERR_WRITE;
    InvalidateRect(g.hwnd,nullptr,FALSE);
}

/* The offer and a failure ask for an answer, so they come as a dialog; the
 * download and the install run in a card at the foot of the sidebar while
 * the rest of the launcher stays usable. */
bool upd_modal() { return g_upd.step == UPD_OFFER || g_upd.step == UPD_FAILED; }
bool upd_busy() { return g_upd.step == UPD_DOWNLOADING || g_upd.step == UPD_INSTALLING; }
void draw_update_card(Graphics& gr, const Font& body, const Font& fsm)
{
    if (!upd_busy()) return;
    const bool installing = g_upd.step == UPD_INSTALLING;   /* no numbers and no Cancel then */
    const float x = 12, w = SIDE - 24.0f, h = installing ? 82.0f : 138.0f, y = H - 12 - h, px = x + 12, pw = w - 24;
    GraphicsPath* p = round_rect(RectF(x, y, w, h), 6);
    SolidBrush bg(C_PANEL); gr.FillPath(&bg, p);
    Pen bd(C_RED, 1); gr.DrawPath(&bd, p); delete p;
    {   /* "Instalando… o launcher vai reabrir sozinho." takes two lines here */
        StringFormat wrap; wrap.SetTrimming(StringTrimmingEllipsisWord);
        if (g_lang == LANG_AR) wrap.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
        SolidBrush ink(C_TEXT);
        gr.DrawString(ux(installing ? upd_text::INSTALLING : upd_text::DOWNLOADING), -1, &fsm, RectF(px, y + 10, pw, 40), &wrap, &ink);
    }
    const uint64_t done = g_upd.prog.done, total = g_upd.prog.total;
    const float frac = installing ? 1.0f : total ? std::min(1.0f, float(double(done) / double(total))) : 0.0f;
    if (!installing) {
        wchar_t b[96];
        if (total) swprintf(b, 96, L"%d%%   %.1f / %.1f MB", int(frac * 100), done / 1048576.0, total / 1048576.0);
        else swprintf(b, 96, L"%.1f MB", done / 1048576.0);
        text(gr, b, px, y + 50, fsm, C_DIM, StringAlignmentNear, pw, 20);
    }
    RectF bar(px, y + (installing ? 58.0f : 76.0f), pw, 10);
    GraphicsPath* tp = round_rect(bar, 5); SolidBrush track(C_FIELD); gr.FillPath(&track, tp);
    Pen tb(C_FIELD_BD, 1); gr.DrawPath(&tb, tp); delete tp;
    if (frac > 0.01f) {
        GraphicsPath* fp = round_rect(RectF(bar.X, bar.Y, std::max(10.0f, bar.Width * frac), bar.Height), 5);
        SolidBrush fill(installing ? C_RED : C_RED_HI); gr.FillPath(&fill, fp); delete fp;
    }
    if (!installing) button(gr, RectF(px, y + 96, pw, 30), ux(upd_text::CANCEL), ID_UPD_CANCEL, body);
}

/* Release notes scroll independently of the page behind this modal. The
 * complete wrapped text is measured, then drawn under a viewport clip. */
void upd_notes_scroll_to(float offset)
{
    const float value=std::clamp(offset,0.f,g_upd.notes_max);
    if(value!=g_upd.notes_scroll){g_upd.notes_scroll=value;InvalidateRect(g.hwnd,nullptr,FALSE);}
}
void upd_notes_drag_to(float y)
{
    const float travel=g_upd.notes_track.Height-g_upd.notes_thumb.Height;
    if(travel>0)upd_notes_scroll_to((y-g_upd.notes_grab-g_upd.notes_track.Y)/travel*g_upd.notes_max);
}
void draw_update_notes(Graphics& gr,RectF box,const Font& font)
{
    g_upd.notes_box=box;g_upd.notes_view=RectF(box.X+12,box.Y+10,box.Width-42,box.Height-20);
    const RectF view=g_upd.notes_view;
    const std::wstring value=g_upd.notes.empty()?ux(upd_text::NO_NOTES):g_upd.notes;
    StringFormat wrap;wrap.SetTrimming(StringTrimmingNone);
    if(g_lang==LANG_AR)wrap.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
    if(g_upd.measured_notes!=value||g_upd.notes_width!=view.Width||g_upd.notes_lang!=g_lang){
        RectF measured;gr.MeasureString(value.c_str(),-1,&font,RectF(0,0,view.Width,1000000.f),&wrap,&measured);
        g_upd.notes_height=std::ceil(measured.Height)+2.f;
        g_upd.measured_notes=value;g_upd.notes_width=view.Width;g_upd.notes_lang=g_lang;
    }
    g_upd.notes_max=std::max(0.f,g_upd.notes_height-view.Height);
    g_upd.notes_scroll=std::clamp(g_upd.notes_scroll,0.f,g_upd.notes_max);
    SolidBrush bg(C_FIELD);gr.FillRectangle(&bg,box);Pen border(C_FIELD_BD,1);gr.DrawRectangle(&border,box);
    GraphicsState saved=gr.Save();gr.SetClip(view,CombineModeIntersect);
    SolidBrush ink(g_upd.notes.empty()?C_DIM:C_TEXT);
    gr.DrawString(value.c_str(),-1,&font,RectF(view.X,view.Y-g_upd.notes_scroll,view.Width,std::max(view.Height,g_upd.notes_height)),&wrap,&ink);
    gr.Restore(saved);
    g_upd.notes_track=g_upd.notes_thumb=RectF();
    if(g_upd.notes_max>0){
        RectF track(box.GetRight()-17,box.Y+8,10,box.Height-16);
        const float size=std::max(24.f,track.Height*view.Height/g_upd.notes_height);
        RectF thumb(track.X,track.Y+(track.Height-size)*g_upd.notes_scroll/g_upd.notes_max,track.Width,size);
        g_upd.notes_track=track;g_upd.notes_thumb=thumb;
        SolidBrush tb(Color(255,45,37,39));gr.FillRectangle(&tb,track);
        SolidBrush sb(g_upd.notes_drag||g.hover==ID_UPD_NOTES_THUMB?C_RED_HI:C_RED);GraphicsPath* shape=round_rect(thumb,4);gr.FillPath(&sb,shape);delete shape;
        hit(track.X-3,track.Y,track.Width+6,track.Height,ID_UPD_NOTES_TRACK);
        hit(thumb.X-3,thumb.Y,thumb.Width+6,thumb.Height,ID_UPD_NOTES_THUMB);
    }
}

/* The dialog, over everything; while it is up only its controls answer. */
void draw_update_dialog(Graphics& gr, const Font& title, const Font& body, const Font& fsm)
{
    if (!upd_modal()) return;
    g.hits.clear();
    SolidBrush dim(Color(210, 8, 8, 10)); gr.FillRectangle(&dim, 0, 0, W, H);
    const float bw = 620, bh = 440, x = (W - bw) / 2, y = (H - bh) / 2, px = x + 30, pw = bw - 60;
    GraphicsPath* p = round_rect(RectF(x, y, bw, bh), 8);
    SolidBrush bg(C_PANEL); gr.FillPath(&bg, p);
    Pen bd(C_RED, 1.5f); gr.DrawPath(&bd, p); delete p;
    const bool failed = g_upd.step == UPD_FAILED;
    float cy = y + 26;
    text(gr, ux(failed ? upd_text::FAILED : upd_text::TITLE), px, cy, title, failed ? C_RED_HI : C_TEXT, StringAlignmentNear, pw, 32);
    cy += 40;
    text(gr, upd_fill(ux(upd_text::VERSIONS), k_launcher_version, g_upd.rel.tag), px, cy, body, C_DIM, StringAlignmentNear, pw, 22);
    cy += 34;
    Pen line(C_LINE, 1); gr.DrawLine(&line, px, cy, px + pw, cy);
    cy += 14;
    StringFormat wrap; wrap.SetTrimming(StringTrimmingEllipsisWord);
    if (g_lang == LANG_AR) wrap.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
    if (failed) {
        SolidBrush ink(C_TEXT);
        gr.DrawString(upd_error_text(g_upd.err), -1, &body, RectF(px, cy, pw, 120), &wrap, &ink);
    } else {
        text(gr, ux(upd_text::NOTES), px, cy, body, C_RED_HI, StringAlignmentNear, pw, 22);
        cy += 28;
        draw_update_notes(gr,RectF(px,cy,pw,178),fsm);
        cy += 190;
        text(gr, ux(upd_text::KEEP_NOTE), px, cy, fsm, C_DIM, StringAlignmentNear, pw, 20);
    }
    const float by = y + bh - 66;
    if (g_upd.step == UPD_OFFER) {
        button(gr, RectF(x + bw - 30 - 180, by, 180, 40), ux(upd_text::INSTALL), ID_UPD_INSTALL, body, true);
        button(gr, RectF(x + bw - 30 - 180 - 14 - 150, by, 150, 40), ux(upd_text::LATER), ID_UPD_LATER, body);
    } else {
        button(gr, RectF(x + bw - 30 - 150, by, 150, 40), ux(upd_text::CLOSE), ID_UPD_CLOSE, body);
    }
}

/* WM_UPDATE: a step of the check or the install has finished. */
void upd_message(WPARAM wp, LPARAM lp)
{
    if (wp == UPD_MSG_CHECKED) {
        std::unique_ptr<UpdCheck> r(reinterpret_cast<UpdCheck*>(lp));
        const bool manual = g_upd.manual;
        g_upd.step = UPD_NONE;
        if (r->err != cv_update::OK) {
            /* the check at start stays quiet when offline or nothing is published */
            g_upd.status = manual ? upd_error_text(r->err) : L"";
        } else if (cv_update::compare_versions(r->rel.tag, k_launcher_version) <= 0) {
            g_upd.status = manual ? upd_fill(ux(upd_text::LATEST), k_launcher_version) : L"";
        } else if (!g.running) {   /* never over the running game's menu */
            g_upd.rel = r->rel; g_upd.notes = upd_plain_notes(r->rel.notes);
            g_upd.notes_scroll=0;g_upd.notes_drag=false;g_upd.notes_lang=-1;
            g_upd.step = UPD_OFFER; g.open_dd = -1; g.hover = -1;
        }
    } else if (wp == UPD_MSG_INSTALLING) {
        g_upd.step = UPD_INSTALLING;
    } else if (wp == UPD_MSG_FINISHED) {
        const auto e = cv_update::Error(lp);
        if (e == cv_update::OK) upd_relaunch();   /* returns only if the new executable did not start */
        else if (e == cv_update::ERR_CANCELED) { g_upd.step = UPD_NONE; g_upd.status.clear(); }
        else { g_upd.step = UPD_FAILED; g_upd.err = e; }
    }
    InvalidateRect(g.hwnd, NULL, FALSE);
}

/* ---------------------------------------------------------------- pages */
namespace design=cv_launcher_design;
const wchar_t* lx(design::Id id){return design::texts[g_lang][id];}
const wchar_t* region_name(int i){return T(i==2?S_REGION_JP:i==1?S_REGION_US:S_REGION_EU);}
const int region_order[]={2,1,0};
/* A small flag for a k_versions slot (0 Europe, 1 America, 2 Japan), drawn as vectors. */
void flag(Graphics& gr,int region,float x,float y,float w,float h){
 GraphicsState st=gr.Save();gr.SetSmoothingMode(SmoothingModeAntiAlias);
 if(region==2){SolidBrush white(Color(255,245,245,245)),red(Color(255,188,0,45));gr.FillRectangle(&white,x,y,w,h);
  const float d=h*0.6f;gr.FillEllipse(&red,x+(w-d)/2,y+(h-d)/2,d,d);}
 else if(region==1){SolidBrush red(Color(255,178,34,52)),white(Color(255,245,245,245)),blue(Color(255,60,59,110));
  const int stripes=7;const float sh=h/stripes;
  for(int k=0;k<stripes;k++)gr.FillRectangle(k%2?&white:&red,x,y+k*sh,w,sh+0.3f);
  const float cw=w*0.42f,ch=sh*4;gr.FillRectangle(&blue,x,y,cw,ch);
  for(int r=0;r<3;r++)for(int c=0;c<3;c++)gr.FillEllipse(&white,x+cw*(0.2f+c*0.3f)-0.6f,y+ch*(0.2f+r*0.3f)-0.6f,1.2f,1.2f);}
 else{SolidBrush blue(Color(255,0,51,153)),gold(Color(255,255,204,0));gr.FillRectangle(&blue,x,y,w,h);
  const float cx=x+w/2,cy=y+h/2,r=h*0.33f;
  for(int k=0;k<12;k++){const float a=k*0.5235988f;gr.FillEllipse(&gold,cx+r*cosf(a)-0.9f,cy+r*sinf(a)-0.9f,1.8f,1.8f);}}
 Pen edge(Color(90,0,0,0),1.f);gr.DrawRectangle(&edge,x,y,w,h);
 gr.Restore(st);
}
/* Bicubic scaling of the castle and the PIC1/ICON0 art is the slow part of a
 * repaint, and a scroll repaints every wheel tick: scale each image once per
 * box size and copy it 1:1 afterwards. */
struct FitCache { Bitmap* src; int w, h; bool cover; Bitmap* out; };
std::vector<FitCache> g_fit_cache;
void image_fit_forget(){for(auto& c:g_fit_cache)delete c.out;g_fit_cache.clear();}
void image_fit(Graphics& gr,Bitmap* image,RectF box,bool cover=false){
 if(!image||!image->GetWidth()||!image->GetHeight())return;
 const int bw=int(box.Width+.5f),bh=int(box.Height+.5f);if(bw<=0||bh<=0)return;
 Bitmap* scaled=nullptr;
 for(auto& c:g_fit_cache)if(c.src==image&&c.w==bw&&c.h==bh&&c.cover==cover){scaled=c.out;break;}
 if(!scaled){
  const float iw=float(image->GetWidth()),ih=float(image->GetHeight());
  const float scale=cover?std::max(bw/iw,bh/ih):std::min(bw/iw,bh/ih);
  scaled=new Bitmap(bw,bh,PixelFormat32bppPARGB);
  {Graphics sg(scaled);sg.SetInterpolationMode(InterpolationModeHighQualityBicubic);sg.SetPixelOffsetMode(PixelOffsetModeHighQuality);
   sg.DrawImage(image,RectF((bw-iw*scale)/2,(bh-ih*scale)/2,iw*scale,ih*scale));}
  g_fit_cache.push_back({image,bw,bh,cover,scaled});
 }
 gr.DrawImage(scaled,int(box.X+.5f),int(box.Y+.5f),bw,bh);
}
void library_art(){
 for(int i=0;i<3;i++){
  if(g.art_path[i]==g.s.region_game[i])continue;g.art_path[i]=g.s.region_game[i];
  image_fit_forget();
  delete g.cover[i];delete g.cover_bg[i];g.cover[i]=g.cover_bg[i]=nullptr;
  if(g.art_path[i].empty())continue;
  auto load=[&](const wchar_t* name)->Bitmap*{const auto path=fs::path(g.art_path[i])/name;std::error_code ec;if(!fs::is_regular_file(path,ec))return nullptr;auto* img=new Bitmap(path.c_str());if(img->GetLastStatus()!=Ok||!img->GetWidth()||!img->GetHeight()){delete img;return nullptr;}return img;};
  g.cover[i]=load(L"ICON0.PNG");g.cover_bg[i]=load(L"PIC1.PNG");
 }
}
void nav_icon(Graphics& gr,int page,float x,float y,Color color){
 Pen p(color,1.5f);p.SetLineJoin(LineJoinRound);p.SetStartCap(LineCapRound);p.SetEndCap(LineCapRound);
 if(page==P_INICIO){gr.DrawRectangle(&p,x,y,23.f,15.f);gr.DrawLine(&p,x+11,y+16,x+11,y+21);gr.DrawLine(&p,x+5,y+21,x+18,y+21);}
 else if(page==P_HUD){gr.DrawArc(&p,x+1,y+1,22.f,22.f,90.f,270.f);gr.DrawArc(&p,x+5,y+5,14.f,14.f,90.f,210.f);gr.DrawLine(&p,x+15,y+19,x+24,y+19);gr.DrawLine(&p,x+19,y+15,x+19,y+23);}
 else if(page==P_TROPHIES){
  // Original compact cup silhouette: rolled rim, curved handles, fluted bowl,
  // narrow stem and stepped pedestal. Drawn as vectors at the sidebar scale.
  GraphicsState saved=gr.Save();gr.TranslateTransform(x,y);gr.ScaleTransform(1.f,1.f);
  Pen outline(color,1.15f);outline.SetLineJoin(LineJoinRound);outline.SetStartCap(LineCapRound);outline.SetEndCap(LineCapRound);
  GraphicsPath cup;cup.StartFigure();cup.AddBezier(5.f,3.f,5.5f,12.5f,7.4f,15.f,12.f,15.8f);cup.AddBezier(12.f,15.8f,16.6f,15.f,18.5f,12.5f,19.f,3.f);gr.DrawPath(&outline,&cup);
  gr.DrawEllipse(&outline,4.f,0.7f,16.f,3.f);gr.DrawLine(&outline,5.f,4.f,19.f,4.f);
  GraphicsPath left;left.AddBezier(4.6f,4.7f,-1.f,2.f,-1.f,13.f,8.f,15.f);left.StartFigure();left.AddBezier(4.7f,6.5f,1.3f,5.f,2.f,11.f,6.5f,12.7f);gr.DrawPath(&outline,&left);
  GraphicsPath right;right.AddBezier(19.4f,4.7f,25.f,2.f,25.f,13.f,16.f,15.f);right.StartFigure();right.AddBezier(19.3f,6.5f,22.7f,5.f,22.f,11.f,17.5f,12.7f);gr.DrawPath(&outline,&right);
  gr.DrawLine(&outline,8.f,5.5f,9.f,11.8f);gr.DrawLine(&outline,11.f,5.7f,11.4f,13.7f);
  gr.DrawEllipse(&outline,10.f,16.f,4.f,1.8f);gr.DrawLine(&outline,11.f,18.f,10.f,20.f);gr.DrawLine(&outline,13.f,18.f,14.f,20.f);
  gr.DrawRectangle(&outline,8.f,20.f,8.f,2.f);gr.DrawRectangle(&outline,6.f,22.f,12.f,2.f);gr.DrawLine(&outline,4.f,25.f,20.f,25.f);gr.Restore(saved);
 }
 else if(page==P_JOGO){gr.DrawEllipse(&p,x+1,y,22.f,22.f);gr.DrawEllipse(&p,x+9,y+8,6.f,6.f);gr.DrawArc(&p,x+5,y+4,14.f,14.f,200.f,70.f);}
 else if(page==P_PATCHES){GraphicsState s=gr.Save();gr.TranslateTransform(x+12,y+11);gr.RotateTransform(-45.f);GraphicsPath band;band.AddArc(-14.f,-5.f,10.f,10.f,90.f,180.f);band.AddArc(4.f,-5.f,10.f,10.f,270.f,180.f);band.CloseFigure();gr.DrawPath(&p,&band);gr.DrawLine(&p,-5.f,-5.f,-5.f,5.f);gr.DrawLine(&p,5.f,-5.f,5.f,5.f);for(int a=0;a<2;a++)for(int b=0;b<2;b++)gr.DrawLine(&p,-2.f+b*4,-2.f+a*4,-1.5f+b*4,-2.f+a*4);gr.Restore(s);}
 else if(page==P_SRT){gr.DrawEllipse(&p,x+2,y+3,20.f,20.f);gr.DrawLine(&p,x+9,y,x+15,y);gr.DrawLine(&p,x+12,y,x+12,y+3);gr.DrawLine(&p,x+20,y+4,x+22,y+6);gr.DrawLine(&p,x+12,y+13,x+12,y+8);gr.DrawLine(&p,x+12,y+13,x+16,y+15);}
 else if(page==P_CONTROLES){GraphicsPath path;PointF a[]={{x+4,y+3},{x+20,y+3},{x+25,y+18},{x+21,y+22},{x+16,y+15},{x+8,y+15},{x+2,y+22},{x-2,y+18}};path.AddPolygon(a,8);gr.DrawPath(&p,&path);gr.DrawLine(&p,x+4,y+8,x+10,y+8);gr.DrawLine(&p,x+7,y+5,x+7,y+11);gr.DrawEllipse(&p,x+17,y+7,2.f,2.f);}
 else if(page==P_ATALHOS){gr.DrawRectangle(&p,x,y+2,24.f,17.f);for(int a=0;a<3;a++)for(int b=0;b<6;b++)gr.DrawLine(&p,x+3+b*3,y+6+a*4,x+4+b*3,y+6+a*4);}
 else if(page==P_DESEMPENHO){for(int a=0;a<4;a++)gr.DrawRectangle(&p,x+a*6,y+17-a*5,4.f,7.f+a*5);}
 else if(page==P_AVANCADO){GraphicsPath gear;PointF t[32];for(int k=0;k<8;k++){const float a=k*0.785398f,d[4]={-0.26f,-0.13f,0.13f,0.26f},r[4]={8.f,11.f,11.f,8.f};for(int j=0;j<4;j++)t[k*4+j]=PointF(x+12+r[j]*cosf(a+d[j]),y+12+r[j]*sinf(a+d[j]));}gear.AddPolygon(t,32);gr.DrawPath(&p,&gear);gr.DrawEllipse(&p,x+8.5f,y+8.5f,7.f,7.f);}
 else if(page==P_INVENTARIO){gr.DrawLine(&p,x+2,y+22,x+15,y+9);gr.DrawLine(&p,x+15,y+9,x+17,y+11);gr.DrawLine(&p,x+17,y+11,x+4,y+24);
  const float sx=x+19,sy=y+5;gr.DrawLine(&p,sx,sy-4,sx,sy+4);gr.DrawLine(&p,sx-4,sy,sx+4,sy);gr.DrawLine(&p,x+9,y+3,x+9,y+7);gr.DrawLine(&p,x+7,y+5,x+11,y+5);gr.DrawLine(&p,x+23,y+14,x+23,y+16);gr.DrawLine(&p,x+22,y+15,x+24,y+15);}
 else if(page==P_SAVES){gr.DrawRectangle(&p,x+1,y+1,22.f,22.f);gr.DrawRectangle(&p,x+6,y+1,12.f,7.f);gr.DrawLine(&p,x+14,y+3,x+14,y+6);gr.DrawRectangle(&p,x+5,y+13,14.f,10.f);}
 else if(page==P_MODS){GraphicsPath piece;piece.AddLine(x+3,y+6,x+8,y+6);piece.AddLine(x+8,y+6,x+8,y+4);piece.AddArc(x+8,y+1,6.f,6.f,180.f,180.f);piece.AddLine(x+14,y+4,x+14,y+6);piece.AddLine(x+14,y+6,x+19,y+6);piece.AddLine(x+19,y+6,x+19,y+11);piece.AddLine(x+19,y+11,x+21,y+11);piece.AddArc(x+18,y+11,6.f,6.f,270.f,180.f);piece.AddLine(x+21,y+17,x+19,y+17);piece.AddLine(x+19,y+17,x+19,y+22);piece.AddLine(x+19,y+22,x+3,y+22);piece.CloseFigure();gr.DrawPath(&p,&piece);}
 else{gr.DrawRectangle(&p,x+3,y,18.f,24.f);for(int a=0;a<3;a++)gr.DrawLine(&p,x+7,y+6+a*5,x+17,y+6+a*5);}
}
void brand(Graphics& gr,float x,float y,float width){
 if(g.brand_logo){image_fit(gr,g.brand_logo,RectF(x,y,width,96.f));return;}
 FontFamily face(L"Impact");Font logo(&face,34,FontStyleRegular,UnitPixel);SolidBrush ink(C_TEXT);StringFormat sf;sf.SetFormatFlags(StringFormatFlagsNoWrap);GraphicsState saved=gr.Save();gr.TranslateTransform(x,y);gr.ScaleTransform(width/218.f,1.f);gr.DrawString(L"RESIDENT EVIL",-1,&logo,PointF(0,0),&sf,&ink);gr.Restore(saved);
 FontFamily serif(L"Georgia");Font hint_font(&serif,14,FontStyleBold,UnitPixel);text(gr,L"CODE: VERONICA X",x,y+44,hint_font,C_RED_HI,StringAlignmentNear,width,25);
}
// Flat vector glyphs: no button outline, tile, or enclosing decoration.
void social_icon(Graphics& gr,int index,float x,float y,Color color)
{
    GraphicsState saved=gr.Save();gr.TranslateTransform(x,y);gr.ScaleTransform(28.f/24.f,28.f/24.f);
    SolidBrush ink(color);
    GraphicsPath shape;
    if(index==1){
        shape.AddArc(0.f,3.f,8.f,8.f,180.f,90.f);shape.AddArc(16.f,3.f,8.f,8.f,270.f,90.f);
        shape.AddArc(16.f,13.f,8.f,8.f,0.f,90.f);shape.AddArc(0.f,13.f,8.f,8.f,90.f,90.f);shape.CloseFigure();
        PointF play[]={{9.5f,7.5f},{16.5f,12.f},{9.5f,16.5f}};shape.AddPolygon(play,3);
        gr.FillPath(&ink,&shape); // alternate fill leaves the play triangle transparent
    }else{
        // Primer Octicons mark-github-16, MIT; see launcher_art/github-octicons-LICENSE.txt.
        gr.ScaleTransform(1.5f,1.5f);
        shape.AddBezier(6.76600f,11.32800f,4.70300f,11.07800f,3.25000f,9.59400f,3.25000f,7.67200f);
        shape.AddBezier(3.25000f,7.67200f,3.25000f,6.89100f,3.53100f,6.04700f,4.00000f,5.48400f);
        shape.AddBezier(4.00000f,5.48400f,3.79700f,4.96900f,3.82800f,3.87500f,4.06300f,3.42200f);
        shape.AddBezier(4.06300f,3.42200f,4.68800f,3.34400f,5.53100f,3.67200f,6.03100f,4.12500f);
        shape.AddBezier(6.03100f,4.12500f,6.62500f,3.93800f,7.25000f,3.84400f,8.01600f,3.84400f);
        shape.AddBezier(8.01600f,3.84400f,8.78100f,3.84400f,9.40600f,3.93800f,9.96900f,4.10900f);
        shape.AddBezier(9.96900f,4.10900f,10.45300f,3.67200f,11.31300f,3.34400f,11.93800f,3.42200f);
        shape.AddBezier(11.93800f,3.42200f,12.15600f,3.84400f,12.18800f,4.93700f,11.98400f,5.46900f);
        shape.AddBezier(11.98400f,5.46900f,12.48400f,6.06200f,12.75000f,6.85900f,12.75000f,7.67200f);
        shape.AddBezier(12.75000f,7.67200f,12.75000f,9.59400f,11.29700f,11.04700f,9.20300f,11.31200f);
        shape.AddBezier(9.20300f,11.31200f,9.73400f,11.65600f,10.09300f,12.40600f,10.09300f,13.26600f);
        shape.AddLine(10.09300f,13.26600f,10.09300f,14.89100f);
        shape.AddBezier(10.09300f,14.89100f,10.09300f,15.35900f,10.48400f,15.62500f,10.95300f,15.43800f);
        shape.AddBezier(10.95300f,15.43800f,13.78100f,14.35900f,16.00000f,11.53000f,16.00000f,8.03000f);
        shape.AddBezier(16.00000f,8.03000f,16.00000f,3.61000f,12.40600f,0.00000f,7.98400f,0.00000f);
        shape.AddBezier(7.98400f,0.00000f,3.56300f,0.00000f,0.00000f,3.61000f,0.00000f,8.03100f);
        shape.AddBezier(0.00000f,8.03100f,0.00000f,11.36000f,2.10000f,14.32000f,5.17200f,15.45300f);
        shape.AddBezier(5.17200f,15.45300f,5.59400f,15.60900f,6.00000f,15.32800f,6.00000f,14.90600f);
        shape.AddLine(6.00000f,14.90600f,6.00000f,13.65600f);
        shape.AddBezier(6.00000f,13.65600f,5.78100f,13.75000f,5.50000f,13.81200f,5.25000f,13.81200f);
        shape.AddBezier(5.25000f,13.81200f,4.21900f,13.81200f,3.61000f,13.25000f,3.17200f,12.20300f);
        shape.AddBezier(3.17200f,12.20300f,3.00000f,11.78100f,2.81200f,11.53100f,2.45300f,11.48400f);
        shape.AddBezier(2.45300f,11.48400f,2.26600f,11.46900f,2.20300f,11.39100f,2.20300f,11.29700f);
        shape.AddBezier(2.20300f,11.29700f,2.20300f,11.10900f,2.51600f,10.96900f,2.82800f,10.96900f);
        shape.AddBezier(2.82800f,10.96900f,3.28100f,10.96900f,3.67200f,11.25000f,4.07800f,11.82900f);
        shape.AddBezier(4.07800f,11.82900f,4.39100f,12.28100f,4.71800f,12.48400f,5.10900f,12.48400f);
        shape.AddBezier(5.10900f,12.48400f,5.50000f,12.48400f,5.75000f,12.34400f,6.10900f,11.98400f);
        shape.AddBezier(6.10900f,11.98400f,6.37500f,11.71900f,6.57900f,11.48400f,6.76600f,11.32800f);
        shape.CloseFigure();gr.FillPath(&ink,&shape);
    }
    gr.Restore(saved);
}
void draw_social_links(Graphics& gr,const Font& font)
{
    Pen divider(C_FIELD_BD);gr.DrawLine(&divider,22.f,H-140.f,SIDE-22.f,H-140.f);
    for(int i=0;i<2;i++){
        const int id=ID_SOCIAL_GITHUB+i;const float x=22.f+i*60.f,y=H-118.f;
        const bool available=!g.social_urls[i].empty();
        social_icon(gr,i,x,y,available?C_TEXT:C_DIM);
        if(available)hit(x-8,y-8,44,44,id);
        if(g.hover==id)text(gr,i?L"YouTube":L"GitHub",126,y+2,font,C_TEXT,StringAlignmentNear,SIDE-134.f,24);
    }
}
void draw_sidebar(Graphics& gr,const Font&,const Font&,const Font& nav,const Font& footer_font){
 SolidBrush side(C_SIDE);gr.FillRectangle(&side,0,0,SIDE,H);Pen line(C_FIELD_BD);gr.DrawLine(&line,float(SIDE-1),0.f,float(SIDE-1),float(H));brand(gr,20,22,SIDE-38.f);
 for(int slot=0;slot<P_COUNT;slot++){const int i=k_nav_order[slot];float y=128.f+slot*45;bool active=g.page==i,hot=g.hover==ID_NAV+i;if(active||hot){LinearGradientBrush b(PointF(0,y),PointF(float(SIDE),y),active?Color(255,68,20,28):C_HOVER,C_SIDE);gr.FillRectangle(&b,0.f,y,float(SIDE),41.f);}if(active){SolidBrush b(C_RED);gr.FillRectangle(&b,0.f,y,3.f,41.f);}nav_icon(gr,i,22,y+10,active?C_TEXT:C_DIM);text(gr,i==P_HUD?std::wstring(L"HUD"):i==P_INICIO?lx(design::GRAPHICS):i==P_JOGO?lx(design::GAMES):i==P_SAVES?lx(design::SAVES):i==P_TROPHIES?cv_trophy_text::get(g_lang,cv_trophy_text::Title):T(k_nav[i]),62,y,nav,active?C_TEXT:C_DIM,StringAlignmentNear,SIDE-72.f,45);hit(0,y,SIDE,41,ID_NAV+i);}
 if(upd_busy())return;   /* the update card takes the foot of the sidebar */
 draw_social_links(gr,footer_font);
 text(gr,T(S_CREDIT_KASTIEL),22,H-62.f,footer_font,C_DIM,StringAlignmentNear,SIDE-38.f,22);text(gr,k_launcher_version,22,H-38.f,footer_font,C_DIM);
}
void draw_play(Graphics& gr,const Font& big,const Font& hint_font){
 SolidBrush bg(Color(250,16,15,17));gr.FillRectangle(&bg,float(SIDE),PAGE_BOTTOM,float(W-SIDE),float(H)-PAGE_BOTTOM);Pen line(Color(255,104,80,53));gr.DrawLine(&line,float(SIDE),PAGE_BOTTOM,float(W),PAGE_BOTTOM);
 if(g.running){button(gr,RectF(W-310.f,H-72.f,280,50),T(S_RESUME),ID_RESUME,big,true);button(gr,RectF(W-460.f,H-64.f,135,36),T(S_QUIT),ID_QUIT,hint_font);return;}
 int installed=0;for(const auto& path:g.s.region_game)installed+=!path.empty();
 text(gr,std::to_wstring(installed)+L" / 3  "+lx(design::CONFIGURED),SIDE+30.f,H-72.f,hint_font,C_TEXT,StringAlignmentNear,430,25);
 text(gr,g.play_msg.empty()?lx(design::SELECT_HINT):g.play_msg,SIDE+30.f,H-44.f,hint_font,g.play_msg.empty()?C_DIM:C_RED_HI,StringAlignmentNear,W-SIDE-400.f,25);
 button(gr,RectF(W-310.f,H-72.f,280,50),L"▶   "+std::wstring(T(S_PLAY)),ID_PLAY,big,true,!upd_busy());
}
void draw_inicio(Graphics& gr,const Font& head,const Font& body,const Font&,const Font& hint_font){
 const float x=SIDE+30.f,w=W-SIDE-60.f,bh=308;
 if(g.banner){const float iw=float(g.banner->GetWidth()),ih=float(g.banner->GetHeight()),sc=std::max(float(W-SIDE)/iw,bh/ih);GraphicsState save=gr.Save();gr.SetClip(RectF(float(SIDE),0,float(W-SIDE),bh),CombineModeIntersect);gr.DrawImage(g.banner,RectF(float(SIDE)+(W-SIDE-iw*sc)/2,0,iw*sc,ih*sc));gr.Restore(save);}
 LinearGradientBrush fade(PointF(float(SIDE),0),PointF(float(W),0),Color(240,12,13,16),Color(20,12,13,16));gr.FillRectangle(&fade,float(SIDE),0.f,float(W-SIDE),bh);
 LinearGradientBrush bottom(PointF(0,bh-130),PointF(0,bh),Color(0,16,16,18),C_BG);gr.FillRectangle(&bottom,float(SIDE),bh-130,float(W-SIDE),131.f);
 FontFamily serif(family(true,g_lang));Font title(&serif,37,FontStyleBold,UnitPixel);text(gr,L"RESIDENT EVIL",x,64,head,C_TEXT);text(gr,L"CODE: VERONICA X",x,100,title,C_TEXT,StringAlignmentNear,w-20,55);
 text(gr,lx(design::GRAPHICS),x,170,body,C_DIM);bool ok=false;check_game(g.s.game,&ok);text(gr,(ok?L"●  ":L"○  ")+std::wstring(ok?lx(design::READY):T(S_SET_FOLDER_TAB)),x,234,body,ok?C_OK:C_DIM,StringAlignmentNear,w-40,30);
 text(gr,lx(design::GRAPHICS),x,320,head,C_TEXT);
 const float gap=16,cw=(w-2*gap)/3,cy=362,ch=311;
 const wchar_t* titles[]={lx(design::DISPLAY),lx(design::QUALITY),lx(design::PERFORMANCE)};
 for(int i=0;i<3;i++){float xx=x+i*(cw+gap);SolidBrush b(Color(245,22,21,23));gr.FillRectangle(&b,xx,cy,cw,ch);Pen p(Color(255,96,78,54));gr.DrawRectangle(&p,xx,cy,cw,ch);text(gr,titles[i],xx+17,cy+12,head,C_TEXT,StringAlignmentNear,cw-34,34);}
 auto setting=[&](int card,int row,const wchar_t* name,const std::wstring& value,int id,bool enabled=true){float xx=x+card*(cw+gap)+16,yy=cy+60+row*61; text(gr,name,xx,yy,hint_font,C_DIM,StringAlignmentNear,cw-32,21);dropdown(gr,RectF(xx,yy+22,cw-32,29),value,id,body,enabled);};
 setting(0,0,T(S_DISPLAY_MODE),mode_name(g.s.mode),ID_DD_MODE);setting(0,1,T(S_RENDER_RES),res_name(g.s.res),ID_DD_RES);
 setting(0,2,cv_enhanced_text::get(g_lang,cv_enhanced_text::FilmFilter),cv_enhanced_text::get(g_lang,g.s.film_filter?cv_enhanced_text::On:cv_enhanced_text::Off),ID_DD_FILM);
 setting(0,3,cv_enhanced_text::get(g_lang,cv_enhanced_text::DynamicShadows),cv_enhanced_text::get(g_lang,g.s.dynamic_shadows?cv_enhanced_text::On:cv_enhanced_text::Off),ID_DD_SHADOWS);
 setting(1,0,T(S_AA),aa_name(g.s.aa),ID_DD_AA);setting(1,1,T(S_SHARPNESS),sharp_name(g.s.sharpness/10),ID_DD_SHARP);
 setting(2,0,T(S_FRAME_LIMIT),fps_name(g.s.fps),g.running||opt30_comparison()?-1:ID_DD_FPS,!g.running&&!opt30_comparison());
 if((g.s.fps==1||pose_device_override()>=0)&&!opt30_comparison())setting(2,1,T(S_FRAME_GEN),frame_generation_name(frame_generation_allowed()?g.s.frame_generation:0),g.running||!frame_generation_allowed()?-1:ID_DD_FRAME_GEN,!g.running&&frame_generation_allowed());
 /* VSync switches live, except in a session running frame generation (its swapchain paces itself). */
 if((g.s.fps==1||pose_device_override()>=0)&&!opt30_comparison()){const bool fg_running=g.running&&g.s.frame_generation&&frame_generation_allowed();
  setting(2,2,T(S_VSYNC),vsync_name(fg_running?0:g.s.vsync),fg_running?-1:ID_DD_VSYNC,!fg_running);}
 text(gr,cv_enhanced_text::get(g_lang,cv_enhanced_text::RestartApply),x,688,hint_font,C_DIM,StringAlignmentNear,w,24);
 text(gr,cv_enhanced_text::get(g_lang,cv_enhanced_text::ShadowHelp),x,718,hint_font,C_DIM,StringAlignmentNear,w,24);
 std::wstring note=T(g.s.fps?S_FPS_NOTE_60:S_FPS_NOTE_30);if(g.s.fps&&g.s.frame_generation&&frame_generation_allowed())note+=L"\n"+std::wstring(T(S_FPS_NOTE_FG));if(g.s.fps&&g.s.vsync)note+=L"\n"+std::wstring(T(S_FPS_NOTE_VSYNC));
 StringFormat sf;sf.SetLineAlignment(StringAlignmentNear);if(g_lang==LANG_AR)sf.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);SolidBrush ink(C_DIM);gr.DrawString(note.c_str(),-1,&hint_font,RectF(x,748,w,60),&sf,&ink);g_page_end=820;
}

void path_row(Graphics& gr, float x, float y, const std::wstring& label, const std::wstring& value,
              const std::wstring& note, bool ok, int id_browse, int id_open, const Font& body, const Font& fsm, float label_width = 170)
{
    text(gr, label, x, y, body, C_TEXT, StringAlignmentNear, label_width, 32);
    RectF fr(x + label_width + 5, y, 640 - label_width, 32);
    field(gr, fr, false, false);
    text(gr, value.empty() ? std::wstring(T(S_NOT_SET)) : value, fr.X + 8, fr.Y, body, value.empty() ? C_DIM : C_TEXT,
         StringAlignmentNear, fr.Width - 14, fr.Height);
    button(gr, RectF(x + 655, y, 110, 32), T(S_BROWSE), id_browse, body, false, id_browse >= 0);
    button(gr, RectF(x + 773, y, 80, 32), T(S_OPEN), id_open, body, false, !value.empty());
    text(gr, (ok ? L"✔  " : L"✖  ") + note, x + label_width + 5, y + 36, fsm, ok ? C_OK : C_RED_HI,
         StringAlignmentNear, 850 - label_width, 20);
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

void option_card(Graphics& gr, float x, float y, float w, float h, bool on, int id,
                 const std::wstring& title, const std::wstring& desc, const Font& body, const Font& fsm);

/* A version is ready when its folder holds the game and, outside Japan, its RAP is set. */
bool region_ready(int i)
{
    if (g.s.region_game[i].empty()) return false;
    bool valid = false; check_game(g.s.region_game[i], &valid);
    std::error_code ec;
    return valid && (i == 2 || (!g.s.region_rap[i].empty() && fs::is_regular_file(g.s.region_rap[i], ec)));
}
int ready_count() { int n = 0; for (int i = 0; i < 3; i++) n += region_ready(i); return n; }
bool g_version_picked = false;   /* a card was clicked this session (needed with 2+ ready versions) */

/* A shortcut on all four: keyboard, PlayStation, Xbox and Switch controllers. */
std::wstring shortcut_text(const wchar_t* key, const wchar_t* ps, const wchar_t* xbox, const wchar_t* sw)
{
    return std::wstring(T(S_SC_COL_KEY)) + L": " + key + L"   ·   PlayStation: " + ps + L"   ·   Xbox: " + xbox +
           L"   ·   Switch: " + sw;
}

constexpr int ID_AUDIO_MUSIC=2780,ID_AUDIO_SFX=2781,ID_BATTLE=2782;
RectF g_audio_slider[2];
void srt_slider(Graphics&,int,RectF,int,int,int,const Font&,bool);
void draw_jogo(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    library_art();
    const float x=SIDE+30.f,w=W-SIDE-60.f,gap=18,card_w=(w-2*gap)/3;
    FontFamily face(family(true,g_lang));Font title(&face,29,FontStyleBold,UnitPixel);
    const Color gold(255,208,177,119),amber(255,226,160,80);
    text(gr,lx(design::LIBRARY),x,24,title,C_TEXT,StringAlignmentNear,w-440,46);
    text(gr,lx(design::DESCRIPTION),x,72,fsm,C_DIM,StringAlignmentNear,w-440,22);
    if(!g.pad_status.empty()){
        std::wstring pad=lx(design::PAD_CONNECTED);const size_t at=pad.find(L"%s");
        if(at!=std::wstring::npos)pad.replace(at,2,g.pad_kind==PAD_KIND_XINPUT?std::wstring(T(S_PAD_XBOX)):g.pad_status);
        text(gr,L"●  "+pad,x,96,fsm,C_OK,StringAlignmentNear,w-440,20);
    }
    if(ready_count()>1)
        text(gr,lx(design::PICK_VERSION),x,116,body,g_version_picked?C_DIM:gold,StringAlignmentNear,w,22);

    const float yy=146,hh=388;
    /* Only added versions get a card; the next slot offers to add another. */
    int shown[3],n=0;for(int r:region_order)if(!g.s.region_game[r].empty())shown[n++]=r;
    /* Several-line hint, centered in a box (text() is one line). */
    auto wrap=[&](const wchar_t* t,RectF r){const std::wstring s=g_lang==LANG_AR?L"‫"+std::wstring(t)+L"‬":std::wstring(t);
        StringFormat sf;sf.SetAlignment(StringAlignmentCenter);sf.SetLineAlignment(StringAlignmentNear);SolidBrush b(C_DIM);gr.DrawString(s.c_str(),-1,&fsm,r,&sf,&b);};
    if(!n){   /* nothing added yet: one big Add version in the middle */
        SolidBrush bg(Color(200,18,18,20));gr.FillRectangle(&bg,x,yy,w,hh);Pen edge(C_FIELD_BD);gr.DrawRectangle(&edge,x,yy,w,hh);
        FontFamily face2(family(true,g_lang));Font big(&face2,22,FontStyleBold,UnitPixel);
        button(gr,RectF(x+w/2-200,yy+120,400,64),L"+   "+std::wstring(lx(design::ADD)),2710,big,true,!g.running);
        wrap(lx(design::ADD_HINT),RectF(x+60,yy+204,w-120,22));
    }
    for(int slot=0;slot<n;slot++){
        const int i=shown[slot];const float xx=x+slot*(card_w+gap);
        /* With several ready versions nothing looks chosen until a card is clicked. */
        const bool added=!g.s.region_game[i].empty(),active=added&&g.s.region_selected==i&&(g_version_picked||ready_count()<=1);
        SolidBrush bg(Color(240,18,18,20));gr.FillRectangle(&bg,xx,yy,card_w,hh);
        if(!g.running)hit(xx,yy,card_w,hh,added?2700+i:2710);   /* inner buttons are added later and win */
        /* art */
        const RectF art(xx+8,yy+8,card_w-16,150);
        SolidBrush art_bg(Color(255,12,12,14));gr.FillRectangle(&art_bg,art);
        image_fit(gr,g.cover_bg[i],art,true);
        SolidBrush dim(Color(120,9,10,12));gr.FillRectangle(&dim,art);
        if(g.cover[i])image_fit(gr,g.cover[i],RectF(art.X+10,art.Y+10,art.Width-20,art.Height-20));
        else text(gr,lx(design::NO_ART),art.X+10,art.Y+50,fsm,C_DIM,StringAlignmentCenter,art.Width-20,40);
        /* name, code, state */
        const float tx=xx+14,tw=card_w-28;
        text(gr,region_name(i),tx,yy+166,head,C_TEXT,StringAlignmentNear,tw,26);
        {   /* the region's flag beside its name */
            RectF m;StringFormat sf(StringFormat::GenericTypographic());const std::wstring nm=region_name(i);
            gr.MeasureString(nm.c_str(),-1,&head,PointF(0,0),&sf,&m);
            flag(gr,i,tx+m.Width+12,yy+172,22,15);
        }
        text(gr,k_versions[i].code,tx,yy+192,fsm,C_DIM,StringAlignmentNear,tw,18);
        bool valid=false;if(added)check_game(g.s.region_game[i],&valid);
        std::error_code ec;const bool rap_ok=i==2||(!g.s.region_rap[i].empty()&&fs::is_regular_file(g.s.region_rap[i],ec));
        const wchar_t* status=!added?lx(design::MISSING):!valid?T(S_CHK_GAME_BAD):!rap_ok?lx(design::RAP_PENDING):lx(design::READY);
        const Color sc=!added?C_DIM:!valid?C_RED_HI:!rap_ok?amber:C_OK;
        {SolidBrush dot(sc);gr.FillEllipse(&dot,tx,yy+219,9.f,9.f);}
        text(gr,status,tx+16,yy+212,body,sc,StringAlignmentNear,tw-16,22);
        {
            /* game folder (set by Add version), then the license */
            text(gr,T(S_GAME_FOLDER),tx,yy+244,fsm,C_DIM,StringAlignmentNear,tw,18);
            field(gr,RectF(tx,yy+264,tw,30),false,false);
            text(gr,g.s.region_game[i],tx+8,yy+264,fsm,C_TEXT,StringAlignmentNear,tw-16,30);
            text(gr,T(S_LICENSE),tx,yy+300,fsm,C_DIM,StringAlignmentNear,tw,18);
            if(i==2){
                field(gr,RectF(tx,yy+320,tw,30),false,false);
                text(gr,lx(design::RAP_NOT_NEEDED),tx+8,yy+320,fsm,C_DIM,StringAlignmentNear,tw-16,30);
            } else {
                const RectF rf(tx,yy+320,tw-40,30);field(gr,rf,false,false);
                if(!rap_ok&&g.play_msg==lx(design::RAP_REQUIRED)&&g.s.region_selected==i){Pen need(amber,2.f);gr.DrawRectangle(&need,rf);}   /* where to fix it */
                const bool has=!g.s.region_rap[i].empty();
                text(gr,has?fs::path(g.s.region_rap[i]).filename().wstring():std::wstring(lx(design::RAP_PICK)),tx+8,yy+320,fsm,has&&rap_ok?C_TEXT:C_DIM,StringAlignmentNear,rf.Width-16,30);
                button(gr,RectF(tx+tw-34,yy+320,34,30),L"…",2720+i,body,false,!g.running);
            }
            /* actions */
            const float ay=yy+hh-34,half=(tw-10)/2;
            button(gr,RectF(tx,ay,half,26),lx(design::OPEN_FOLDER),2730+i,fsm);
            button(gr,RectF(tx+half+10,ay,half,26),lx(design::REMOVE),2740+i,fsm,false,!g.running);
        }
        Pen edge(active?gold:g.hover==2700+i?C_TEXT:C_FIELD_BD,active?2.f:1.f);gr.DrawRectangle(&edge,xx,yy,card_w,hh);
        if(active){Pen tick(gold,2.f);tick.SetStartCap(LineCapRound);tick.SetEndCap(LineCapRound);tick.SetLineJoin(LineJoinRound);
            PointF v[3]={PointF(xx+card_w-32,yy+178),PointF(xx+card_w-26,yy+184),PointF(xx+card_w-16,yy+172)};gr.DrawLines(&tick,v,3);}
    }
    if(n&&n<3){   /* beside the last card: add another version */
        const float xx=x+n*(card_w+gap);const bool hot=g.hover==2710;
        SolidBrush bg(Color(hot?230:160,22,20,22));gr.FillRectangle(&bg,xx,yy,card_w,hh);
        Pen edge(hot?C_TEXT:C_FIELD_BD,1.f);edge.SetDashStyle(DashStyleDash);gr.DrawRectangle(&edge,xx,yy,card_w,hh);
        Pen plus(hot?C_TEXT:C_DIM,2.f);const float cx=xx+card_w/2,cy=yy+150;
        gr.DrawEllipse(&plus,cx-28,cy-28,56.f,56.f);gr.DrawLine(&plus,cx-12,cy,cx+12,cy);gr.DrawLine(&plus,cx,cy-12,cx,cy+12);
        text(gr,lx(design::ADD_ANOTHER),xx+14,cy+44,head,hot?C_TEXT:C_DIM,StringAlignmentCenter,card_w-28,28);
        wrap(lx(design::ADD_HINT),RectF(xx+20,cy+84,card_w-40,40));
        if(!g.running)hit(xx,yy,card_w,hh,2710);
    }
    /* Always visible: which versions need a license. */
    text(gr,L"ⓘ  "+std::wstring(lx(design::RAP_HINT)),x,yy+hh+10,fsm,C_DIM,StringAlignmentNear,w,20);
    if(n)text(gr,L"ⓘ  "+std::wstring(lx(design::REMOVE_NOTE)),x,yy+hh+30,fsm,C_DIM,StringAlignmentNear,w,20);
    option_card(gr,x,yy+hh+60,w,84,g.s.focus_pause,ID_FOCUS_PAUSE,
                T(S_FOCUS_PAUSE_T),T(S_FOCUS_PAUSE_D),body,fsm);
    using namespace cv_enhanced_text;
    const float ty=yy+hh+170;
    text(gr,ptbr_ui(0),x,ty,body,C_TEXT,StringAlignmentNear,240,32);
    dropdown(gr,RectF(x+250,ty,w-250,32),ptbr_ui(g.s.game_text_ptbr?2:1),ID_DD_GAME_TEXT,body);
    text(gr,ptbr_ui(3),x,ty+39,fsm,C_DIM,StringAlignmentNear,w,28);
    const float ay=yy+hh+260,col=(w-28)/2;
    text(gr,get(g_lang,Audio),x,ay,head,C_TEXT);
    for(int i=0;i<2;i++){
        const float xx=x+i*(col+28);
        text(gr,get(g_lang,i?Effects:Music),xx,ay+42,body,C_TEXT);
        srt_slider(gr,i?ID_AUDIO_SFX:ID_AUDIO_MUSIC,RectF(xx,ay+72,col,32),i?g.s.sfx_volume:g.s.music_volume,0,100,body,true);
    }
    g_page_end=std::max(g_page_end,ay+149);
    text(gr,get(g_lang,MusicHelp),x,ay+119,fsm,C_DIM,StringAlignmentNear,w,30);


}

void draw_saves(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    const float x=SIDE+30.f,w=W-SIDE-60.f;
    FontFamily face(family(true,g_lang));Font title(&face,29,FontStyleBold,UnitPixel);
    text(gr,lx(design::SAVES_TITLE),x,24,title,C_TEXT,StringAlignmentNear,w,46);
    text(gr,lx(design::SAVES_DESC),x,76,fsm,C_DIM,StringAlignmentNear,w,25);
    float y=124;
    section(gr,T(S_SAVE_FOLDER),x,y,w,head);
    y+=44;
    path_row(gr, x, y, T(S_SAVE_FOLDER), save_folder().wstring(),
             T(g.s.saves.empty() ? S_SAVE_USING_DEFAULT : S_SAVE_SELECTED), true,
             ID_SAVE_BROWSE, ID_SAVE_OPEN, body, fsm);
    save_note(gr, T(S_SAVE_HINT), x + 175, y + 62, fsm);
    save_note(gr, T(S_SAVE_NO_MOVE), x + 175, y + 114, fsm);
    button(gr, RectF(x + 175, y + 168, 330, 32), T(S_SAVE_DEFAULT), ID_SAVE_DEFAULT,
           body, false, !g.s.saves.empty());
    if (g.running)
        text(gr, T(S_PATHS_RESTART), x + 520, y + 172, body, C_RED_HI,
             StringAlignmentNear, w - 520, 24);
    y+=230;
    section(gr,lx(design::SHOT_TITLE),x,y,w,head);
    y+=44;
    text(gr,lx(design::SHOT_DESC),x,y,fsm,C_DIM,StringAlignmentNear,w,20);
    text(gr,shortcut_text(L"F10",L"R1+R2+L1+L2+R3",L"RB+RT+LB+LT+RS",L"R+ZR+L+ZL+RS"),
         x,y+24,body,C_TEXT,StringAlignmentNear,w,24);
    button(gr,RectF(x,y+60,330,32),T(S_SHOT_OPEN),ID_SHOT_OPEN,body);
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
    const Color foot_c = foot_warn ? C_RED_HI : Color(255, 120, 110, 104);
    text(gr, title, x + 18, y + 8, body, usable ? C_TEXT : C_DIM, StringAlignmentNear, w - 120, 24);
    text(gr, desc, x + 18, y + 32, fsm, C_DIM, StringAlignmentNear, w - 120, 20);
    if (h < 70)   /* compact: the foot on the title's line, at the right */
        text(gr, foot, x + 18, y + 10, fsm, foot_c, StringAlignmentFar, w - 100, 20);
    else
        text(gr, foot, x + 18, y + 52, fsm, foot_c, StringAlignmentNear, w - 120, 20);
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
    /* the port's own patches: two lines each, the credit beside the title */
    for (int i = 0; i < NPATCHES; i++) {
        if (is_cheat(i)) continue;   /* on the Cheats page */
        patch_card(gr, x, y, w, 58, g.s.patch[i], true, ID_PATCH + i, T(k_patches[i].title), T(k_patches[i].desc),
                   T(k_patches[i].credit), false, body, fsm);
        y += 66;
    }
    /* patches from files: made by the player or shared by others */
    y += 10;
    section(gr, T(S_PATCH_CUSTOM_T), x, y, w, head);
    { StringFormat sf; sf.SetLineAlignment(StringAlignmentNear);   /* two lines, wrapped */
      if (g_lang == LANG_AR) sf.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
      SolidBrush brush(C_DIM);
      gr.DrawString(T(S_PATCH_CUSTOM_D), -1, &fsm, RectF(x, y + 34, w, 40), &sf, &brush); }
    y += 80;
    std::wstring note;
    if (!g.s.patch_dir.empty()) {
        wchar_t b[200];
        if (g.fpatches.empty()) note = T(S_PATCH_NONE);
        else { swprintf(b, 200, T(S_PATCH_FOUND_FMT), (int)g.fpatches.size(), g.fpatch_files); note = b; }
    }
    path_row(gr, x, y, T(S_PATCH_FOLDER), g.s.patch_dir, g.s.patch_dir.empty() ? std::wstring(T(S_PATCH_FOLDER_HINT)) : note,
             !g.fpatches.empty() || g.s.patch_dir.empty(), ID_PATCH_DIR_BROWSE, ID_PATCH_DIR_OPEN, body, fsm);
    button(gr, RectF(x + 175, y + 62, 230, 32), T(S_PATCH_CLEAR_PATH), ID_PATCH_DIR_CLEAR,
           body, false, !g.s.patch_dir.empty());
    text(gr, T(S_PATCH_CLEAR_PATH_HINT), x + 419, y + 62, fsm, C_DIM,
         StringAlignmentNear, w - 419, 36);
    y += 108;
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
 * tone, dark touchpad), Xbox (rounder, offset sticks, logo button) or Switch
 * (the Xbox outline as Joy-Con in a grip: neon blue and red halves). */
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
    if (kind == PAD_KIND_XINPUT || kind == PAD_KIND_SWITCH) {
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
        if (kind == PAD_KIND_SWITCH) {
            Region clip(&body); gr.SetClip(&clip, CombineModeIntersect);
            SolidBrush blue(Color(255, 0, 165, 225)), red(Color(255, 240, 72, 64)), grip(Color(255, 70, 68, 72));
            gr.FillRectangle(&blue, 0.0f, 0.0f, 44.0f, 68.0f);
            gr.FillRectangle(&red, 56.0f, 0.0f, 44.0f, 68.0f);
            gr.FillRectangle(&grip, 44.0f, 0.0f, 12.0f, 68.0f);
            gr.ResetClip();
            ring(pDark, 24, 26, 6.5f); disc(bDark, 24, 26, 3.6f);        /* left stick, high */
            ring(pDark, 34, 39, 2.3f); ring(pDark, 34, 49, 2.3f);        /* the four arrow buttons */
            ring(pDark, 29, 44, 2.3f); ring(pDark, 39, 44, 2.3f);
            ring(pDark, 76, 19, 3.0f); ring(pDark, 83, 26, 3.0f);        /* X A B Y */
            ring(pDark, 76, 33, 3.0f); ring(pDark, 69, 26, 3.0f);
            ring(pDark, 66, 46, 6.5f); disc(bDark, 66, 46, 3.6f);        /* right stick, low */
            Pen pm(dark, 1.4f);
            gr.DrawLine(&pm, 38.0f, 15.0f, 42.0f, 15.0f);                /* minus */
            gr.DrawLine(&pm, 58.0f, 15.0f, 62.0f, 15.0f); gr.DrawLine(&pm, 60.0f, 13.0f, 60.0f, 17.0f);   /* plus */
            gr.DrawRectangle(&pm, 39.0f, 51.0f, 3.2f, 3.2f);              /* capture */
            ring(pm, 61, 39, 1.8f);                                      /* home */
            gr.Restore(st);
            return;
        }
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
int icon_layout()
{
    if (g.s.layout == 4) return 2;
    if (g.s.layout < 2) return g.s.layout;
    return g.pad_kind == PAD_KIND_XINPUT ? 1 : g.pad_kind == PAD_KIND_SWITCH ? 2 : 0;
}

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

RectF srt_slider_rect(int id);
void srt_slider(Graphics& gr, int id, RectF r, int value, int minimum, int maximum, const Font& body, bool enabled = true);
float g_rumble_y = 0;   /* the vibration slider's top on the Controles page, in page coordinates */

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
            { L"Xbox", 1, ID_LAYOUT_XB }, { L"Nintendo Switch", 4, ID_LAYOUT_SW }, { T(S_ICONS_KB), 3, ID_LAYOUT_KB } };
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
    const bool kb_on = g.s.layout == 2 || g.s.layout == 3, pad_on = g.s.layout != 3;
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
    /* Vibration strength: applied at once (PAD_RUMBLE_SCALE); letting go of
     * the slider gives a short test pulse. */
    by += 48;
    text(gr, std::wstring(lx(design::RUMBLE)) + L"  (0–200%)", x, by, body, C_TEXT, StringAlignmentNear, 300, 32);
    g_rumble_y = by;
    srt_slider(gr, ID_RUMBLE, srt_slider_rect(ID_RUMBLE), g.s.rumble, 0, 200, body);
    text(gr, lx(design::RUMBLE_HINT), x, by + 34, fsm, C_DIM, StringAlignmentNear, W - SIDE - 64.0f, 20);
    text(gr, cv_axis_hints[g_lang], x, by + 64, fsm, C_DIM, StringAlignmentNear, W - SIDE - 64.0f, 52);
}

/* Resolve the port independently of how a shortcut/editor launched it.
 * The same root is used for the runtime's existing relative shader_cache. */
fs::path port_directory()
{
    static const fs::path dir=[] {
        wchar_t executable[32768];
        const DWORD n=GetModuleFileNameW(nullptr,executable,32768);
        return cv_launcher_cache::port_directory(fs::path(std::wstring(executable,n)));
    }();
    return dir;
}
fs::path cache_dir(){return port_directory()/L"shader_cache";}
// Cache files are enumerated only when the user explicitly clears them.
// Merely opening/painting Advanced must never scan the shader directory.
int cache_clear(){return cv_launcher_cache::clear(cache_dir());}

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
    y += 62;
    /* Updates from the GitHub releases (cv_updater.h) */
    option_card(gr, x, y, w, 84, g.s.check_updates, ID_UPD_TOGGLE, ux(upd_text::OPTION), ux(upd_text::OPTION_DESC), body, fsm);
    y += 98;
    {
        const bool busy = g_upd.step != UPD_NONE;
        button(gr, RectF(x, y, 220, 34), ux(upd_text::CHECK_NOW), ID_UPD_CHECK, body, false, !g.running && !busy);
        const std::wstring st = g_upd.step == UPD_CHECKING && g_upd.manual ? ux(upd_text::CHECKING) : g_upd.status;
        text(gr, st, x + 236, y, body, C_DIM, StringAlignmentNear, w - 236, 34);
    }
    y += 60;
    section(gr, T(S_ADV_FILES), x, y, w, head);
    y += 46;
    button(gr, RectF(x, y, 280, 34), T(S_CLEAR_CACHE), ID_CACHE_CLEAR, body, false, !g.running);
    button(gr, RectF(x + 292, y, 150, 34), T(S_OPEN_FOLDER), ID_CACHE_OPEN, body);
    y += 46;
    std::wstring st = g.cache_msg;
    if (g.running) st = T(S_CACHE_RUNNING);
    else if (st.empty()) st = L"shader_cache"; // stable folder name, no automatic count
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
    {   /* then its size, and the box behind it or the numbers alone */
        const float col = (w - 24) / 2;
        const int layouts[] = { S_OV_ROW, S_OV_COLUMN };
        const design::Id sizes[] = { design::OV_SMALL, design::OV_MEDIUM, design::OV_LARGE };
        const std::wstring names[] = { T(S_OV_POSITION), T(S_OV_LAYOUT), lx(design::OV_SIZE), lx(design::OV_BG) };
        const std::wstring values[] = { T(S_SRT_TL + g.s.overlay_corner), T(layouts[g.s.overlay_layout]),
                                        lx(sizes[g.s.overlay_size]), lx(g.s.overlay_text_only ? design::OV_BG_TEXT : design::OV_BG_BOX) };
        const int ids[] = { ID_DD_OV_CORNER, ID_DD_OV_LAYOUT, ID_DD_OV_SIZE, ID_DD_OV_BG };
        // Share each column's label width across both rows, in every language.
        float label_width[2]={};
        for(int i=0;i<4;i++){
            RectF measured;gr.MeasureString(names[i].c_str(),-1,&body,PointF(0,0),&sf,&measured);
            label_width[i%2]=std::max(label_width[i%2],std::min(measured.Width+12,col*0.55f));
        }
        for (int i = 0; i < 4; i++) {
            if (i == 2) y += 44;
            const float cx0 = x + (i % 2) * (col + 24);
            const float lw=label_width[i%2];
            text(gr, names[i], cx0, y, body, C_TEXT, StringAlignmentNear, lw, 32);
            dropdown(gr, RectF(cx0 + lw, y, col - lw, 32), values[i], ids[i], body);
        }
    }
    y += 50;
    /* The game's log: view it live, copy it, save a copy */
    section(gr, T(S_LOG_TITLE), x, y, w, head);
    y += 46;
    static const std::wstring lp = log_file_path(); // stdout destination is stable for this process
    button(gr, RectF(x, y, 150, 34), T(S_LOG_VIEW), ID_LOG_VIEW, body);
    button(gr, RectF(x + 160, y, 150, 34), T(S_LOG_COPY), ID_LOG_COPY, body, false, !lp.empty());
    button(gr, RectF(x + 320, y, 170, 34), T(S_LOG_SAVE), ID_LOG_SAVE, body, false, !lp.empty());
    button(gr, RectF(x + 500, y, 150, 34), T(S_OPEN_FOLDER), ID_LOG_FOLDER, body, false, !lp.empty());
    y += 44;
    std::wstring log_note = g.log_msg;
    if (log_note.empty()) {
        if (lp.empty()) log_note = T(log_to_console() ? S_LOG_CONSOLE : S_LOG_NONE);
        else log_note = lp; // do not flush the game log or query its size while painting
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

#include "launcher_trophies.inc"

std::wstring srt_enemy_value(int n) {return n==0?T(S_AA_OFF):n==99?T(S_SRT_ALL):std::to_wstring(n);}
std::wstring srt_language_value() {return g.s.srt_language?k_lang_name[cv_srt_i18n::selection(g.s.srt_language,g_lang)]:T(S_SRT_FOLLOW);}
/* Where draw_srt last put its sliders and the panel preview, in page coordinates. */
RectF g_srt_slider[3];   /* ID_SRT_SCALE, ID_SRT_OPACITY, ID_SRT_INV_SCALE */
float g_srt_preview_y = 0;
RectF srt_slider_rect(int id) {
    if(id==ID_AUDIO_MUSIC||id==ID_AUDIO_SFX)return g_audio_slider[id-ID_AUDIO_MUSIC];
    if(id==ID_RUMBLE)return RectF(SIDE+32.f+310,g_rumble_y,360,32);
    return g_srt_slider[id==ID_SRT_SCALE?0:id==ID_SRT_OPACITY?1:2];
}
/* Draws the slider in r (the value goes in the last 72 pixels); a disabled
 * one is dimmed and takes no clicks. */
void srt_slider(Graphics& gr,int id,RectF r,int value,int minimum,int maximum,const Font& body,bool enabled) {
    if(id==ID_AUDIO_MUSIC||id==ID_AUDIO_SFX){r.Width-=72;g_audio_slider[id-ID_AUDIO_MUSIC]=r;}
    else if(id!=ID_RUMBLE){r.Width-=72;g_srt_slider[id==ID_SRT_SCALE?0:id==ID_SRT_OPACITY?1:2]=r;}
    const float left=r.X+8,span=r.Width-16,cy=r.Y+16;
    const float thumb=left+span*cv_launcher_controls::slider_fraction(value,minimum,maximum);
    Pen track(C_FIELD_BD,5);track.SetStartCap(LineCapRound);track.SetEndCap(LineCapRound);
    gr.DrawLine(&track,left,cy,left+span,cy);
    Pen progress(enabled?C_RED:C_FIELD_BD,5);progress.SetStartCap(LineCapRound);progress.SetEndCap(LineCapRound);
    if(thumb>left)gr.DrawLine(&progress,left,cy,thumb,cy);
    if(enabled&&(g.srt_drag==id||g.hover==id)) {SolidBrush glow(Color(45,230,50,50));gr.FillEllipse(&glow,thumb-11,cy-11,22.f,22.f);}
    SolidBrush fill(enabled?C_TEXT:C_DIM);Pen border(enabled?C_RED:C_FIELD_BD,2);
    gr.FillEllipse(&fill,thumb-7,cy-7,14.f,14.f);gr.DrawEllipse(&border,thumb-7,cy-7,14.f,14.f);
    text(gr,std::to_wstring(value)+L"%",r.X+r.Width+8,r.Y,body,enabled?C_TEXT:C_DIM,StringAlignmentFar,64,32);
    if(enabled)hit(r.X,r.Y,r.Width,r.Height,id);
}
const wchar_t* slt(cv_srt_layout_text::Id id){return cv_srt_layout_text::texts[g_lang][id];}
RectF srt_layout_preview(){return RectF(SIDE+32.f,g_srt_preview_y,640.f,360.f);}
RectF srt_layout_panel(int which){
    const auto area=srt_layout_preview();const bool split=g.s.srt_separate;
    const bool side=g.s.srt_parts[4]&&!split&&!g.s.srt_inventory_below;
    const float scale=(which?g.s.srt_inventory_scale:g.s.srt_scale)/100.f;
    const float pw=std::min(area.Width,(which||!side?304.f:620.f)*area.Width/1600.f*scale);
    const float ph=std::min(area.Height,(which?380.f:420.f+(g.s.srt_parts[4]&&!split&&g.s.srt_inventory_below?380.f:0.f))*area.Height/900.f*scale);
    const int nx=which?g.s.srt_inv_x:g.s.srt_main_x,ny=which?g.s.srt_inv_y:g.s.srt_main_y;
    return RectF(area.X+cv_srt_layout::position(area.Width,pw,nx),area.Y+cv_srt_layout::position(area.Height,ph,ny),pw,ph);
}
void move_srt_preview(int mouse_x,int mouse_y){
    const int which=g.srt_layout_drag;const auto area=srt_layout_preview(),panel=srt_layout_panel(which);
    int& x=which?g.s.srt_inv_x:g.s.srt_main_x;int& y=which?g.s.srt_inv_y:g.s.srt_main_y;
    x=cv_srt_layout::normalized(mouse_x-g.srt_grab_x-area.X,area.Width,panel.Width);
    y=cv_srt_layout::normalized(mouse_y+g.page_scroll[P_SRT]-g.srt_grab_y-area.Y,area.Height,panel.Height);
    g.srt_layout_dirty=true;InvalidateRect(g.hwnd,NULL,FALSE);
}
std::vector<int> quick_item_choices(){return {0,-1,55,8,9,131,10,5,33,4,32,6,7,3,2,34,142,1,11};}
std::wstring quick_item_label(int id){if(id==0)return quick_text::get(g_lang,quick_text::Auto);if(id<0)return quick_text::get(g_lang,quick_text::Empty);return cv_srt_i18n::item(id,cv_srt_i18n::Language(g_lang));}
void draw_hud_page(Graphics& gr,const Font& head,const Font& body,const Font& fsm){
 using namespace cv_enhanced_text;const float x=SIDE+32.f,w=W-SIDE-64.f,gap=20,cw=(w-gap)/2;
 section(gr,L"HUD",x,24,w,head);text(gr,get(g_lang,HUDPageHint),x,58,fsm,C_DIM,StringAlignmentNear,w,42);
 option_card(gr,x,116,w,94,g.s.combat_hud,ID_HUD_ENABLED,get(g_lang,CombatHUD),get(g_lang,HUDHelp),body,fsm);
 option_card(gr,x,228,w,94,g.s.manual_reload,ID_HUD_RELOAD,get(g_lang,ManualReload),get(g_lang,ReloadHelp),body,fsm);
 option_card(gr,x,340,w,94,g.s.quick.on,ID_QUICK_ON,quick_text::get(g_lang,quick_text::Title),quick_text::get(g_lang,quick_text::Help),body,fsm);
 float y=466;
 auto capture=[&](float bx,float by,float width,int id,int clear,bool active,int value,bool pad){
  RectF r(bx,by,width-38,38);field(gr,r,g.hover==id,active);const bool has=pad?value>=0:value>0;
  if(has&&!active){if(pad)btn_glyph(gr,icon_layout(),shown_button(value),r.X+8,r.Y+7,24);else key_glyph(gr,value,r.X+8,r.Y+7,24);}
  const auto label=active?T(pad?S_PRESS_BTN:S_PRESS_KEY):has?(pad?pb_name(icon_layout(),shown_button(value)):key_name(value)):std::wstring(L"—");
  text(gr,label,r.X+(has&&!active?39:9),r.Y,body,active?C_RED_HI:C_TEXT,StringAlignmentNear,r.Width-47,r.Height);hit(r.X,r.Y,r.Width,r.Height,id);button(gr,RectF(bx+width-32,by,32,38),L"×",clear,body);
 };
 if(g.s.manual_reload){
  section(gr,get(g_lang,ReloadBindings),x,y,w,body);y+=34;text(gr,get(g_lang,ReloadBindingsHint),x,y,fsm,C_DIM,StringAlignmentNear,w,58);y+=68;
  text(gr,T(S_COL_KB),x,y,body,C_TEXT,StringAlignmentNear,cw,26);text(gr,T(S_COL_PAD),x+cw+gap,y,body,C_TEXT,StringAlignmentNear,cw,26);y+=32;
  capture(x,y,cw,ID_HUD_RELOAD_KEY,ID_HUD_RELOAD_KEY_CLEAR,g.cap==CAP_RELOAD_KEY,g.s.reload_key,false);capture(x+cw+gap,y,cw,ID_HUD_RELOAD_BUTTON,ID_HUD_RELOAD_BUTTON_CLEAR,g.cap==CAP_RELOAD_BTN,g.s.reload_button,true);y+=54;
  button(gr,RectF(x,y,180,34),T(S_RESTORE),ID_HUD_RELOAD_RESET,body);y+=66;
 }
 if(g.s.quick.on){
  section(gr,quick_text::get(g_lang,quick_text::Open),x,y,w,body);y+=37;
  text(gr,T(S_COL_KB),x,y,fsm,C_DIM,StringAlignmentNear,cw,26);text(gr,T(S_COL_PAD),x+cw+gap,y,fsm,C_DIM,StringAlignmentNear,cw,26);y+=30;
  capture(x,y,cw,ID_QUICK_KEY,ID_QUICK_KEY_CLEAR,g.cap==CAP_QUICK_KEY,g.s.quick.key,false);capture(x+cw+gap,y,cw,ID_QUICK_BUTTON,ID_QUICK_BUTTON_CLEAR,g.cap==CAP_QUICK_BTN,g.s.quick.button,true);y+=64;
  section(gr,quick_edit_text::get(g_lang,quick_edit_text::Edit),x,y,w,body);y+=34;
  text(gr,quick_edit_text::get(g_lang,quick_edit_text::ChooseHelp),x,y,fsm,C_DIM,StringAlignmentNear,w,48);y+=56;
  capture(x,y,cw,ID_QUICK_EDIT_KEY,ID_QUICK_EDIT_KEY_CLEAR,g.cap==CAP_QUICK_EDIT_KEY,g.s.quick.edit_key,false);capture(x+cw+gap,y,cw,ID_QUICK_EDIT_BUTTON,ID_QUICK_EDIT_BUTTON_CLEAR,g.cap==CAP_QUICK_EDIT_BTN,g.s.quick.edit_button,true);y+=59;
  section(gr,quick_edit_text::get(g_lang,quick_edit_text::Confirm),x,y,w,body);y+=37;
  capture(x,y,cw,ID_QUICK_CONFIRM_KEY,ID_QUICK_CONFIRM_KEY_CLEAR,g.cap==CAP_QUICK_CONFIRM_KEY,g.s.quick.confirm_key,false);capture(x+cw+gap,y,cw,ID_QUICK_CONFIRM_BUTTON,ID_QUICK_CONFIRM_BUTTON_CLEAR,g.cap==CAP_QUICK_CONFIRM_BTN,g.s.quick.confirm_button,true);y+=60;
  text(gr,quick_edit_text::get(g_lang,quick_edit_text::SwapHelp),x,y,fsm,C_DIM,StringAlignmentNear,w,48);y+=61;
  section(gr,quick_text::get(g_lang,quick_text::Select),x,y,w,body);y+=35;
  text(gr,quick_text::get(g_lang,quick_text::SelectHelp),x,y,fsm,C_DIM,StringAlignmentNear,w,55);y+=65;
  const float qw=(w-3*16)/4;
  for(int d=0;d<4;d++){float bx=x+d*(qw+16);text(gr,quick_text::get(g_lang,quick_text::Up+d),bx,y,body,C_TEXT,StringAlignmentNear,qw,28);capture(bx,y+32,qw,ID_QUICK_DIR+d,ID_QUICK_DIR_CLEAR+d,g.cap==CAP_QUICK_DIR&&g.cap_slot==d,g.s.quick.keys[d],false);}y+=101;
  section(gr,quick_text::get(g_lang,quick_text::Slots),x,y,w,body);y+=35;text(gr,quick_text::get(g_lang,quick_text::SlotHelp),x,y,fsm,C_DIM,StringAlignmentNear,w,48);y+=58;
  for(int d=0;d<4;d++){text(gr,quick_text::get(g_lang,quick_text::Up+d),x,y,body,C_TEXT,StringAlignmentNear,100,36);dropdown(gr,RectF(x+115,y,w-115,36),quick_item_label(g.s.quick.items[d]),ID_QUICK_SLOT+d,body);y+=48;}
  button(gr,RectF(x,y+8,180,34),T(S_RESTORE),ID_QUICK_RESET,body);y+=62;
 }
 g_page_end=y+24;
}


void draw_srt(Graphics& gr,const Font& head,const Font& body,const Font& fsm)
{
    namespace st=cv_srt_layout_text;
    const float x=SIDE+32.f,w=W-SIDE-64.f,col=(w-24)/2,right=x+col+24;
    /* Upper case like the other pages' section titles. */
    auto heading=[&](const wchar_t* t,float top){std::wstring s=t;CharUpperBuffW(s.data(),(DWORD)s.size());section(gr,s,x,top,w,head);};
    auto label=[&](const std::wstring& t,float left,float top){text(gr,t,left,top,body,C_TEXT,StringAlignmentNear,col,24);};
    StringFormat sf;sf.SetLineAlignment(StringAlignmentNear);SolidBrush brush(C_DIM);
    auto note=[&](const wchar_t* t,RectF r){const std::wstring s=g_lang==LANG_AR?L"‫"+std::wstring(t)+L"‬":std::wstring(t);gr.DrawString(s.c_str(),-1,&fsm,r,&sf,&brush);};
    section(gr,T(S_SRT_TITLE),x,34,w,head);
    text(gr,T(S_SRT_INTRO),x,70,fsm,C_DIM,StringAlignmentNear,w,24);
    option_card(gr,x,104,w,84,g.s.srt,ID_SRT,T(S_SRT_ENABLE),T(S_SRT_HOTKEY),body,fsm);
    /* What it shows: five switches and the enemy count, two by two. */
    float y=214;
    heading(slt(st::SEC_SHOW),y);y+=46;
    const int names[]={S_SRT_NAME,S_SRT_HEALTH,S_SRT_TIME,S_SRT_STATS,S_SRT_INVENTORY};
    for(int i=0;i<5;i++)option_card(gr,i%2?right:x,y+(i/2)*70.f,col,58,g.s.srt_parts[i],ID_SRT_PART+i,T(names[i]),L"",body,fsm);
    {
        const float top=y+140,dw=std::min(200.f,col*0.45f);
        RectF card(right,top,col,58);GraphicsPath* cp=round_rect(card,6);
        SolidBrush cb(C_PANEL);gr.FillPath(&cb,cp);Pen cbd(C_FIELD_BD,1);gr.DrawPath(&cbd,cp);delete cp;
        text(gr,T(S_SRT_ENEMIES),right+18,top+8,body,C_TEXT,StringAlignmentNear,col-dw-48,24);
        dropdown(gr,RectF(right+col-18-dw,top+13,dw,32),srt_enemy_value(g.s.srt_enemies),ID_DD_SRT_ENEMIES,body);
    }
    y+=224;
    /* How it looks: language, boss bar, size, background, border. */
    heading(slt(st::SEC_LOOK),y);y+=46;
    label(T(S_SRT_LANGUAGE),x,y);
    dropdown(gr,RectF(x,y+29,col,32),srt_language_value(),ID_DD_SRT_LANGUAGE,body);
    label(T(S_SRT_BOSS_BAR),right,y);
    dropdown(gr,RectF(right,y+29,col,32),T(S_SRT_BOSS_PANEL+g.s.srt_boss_bar),ID_DD_SRT_BOSSBAR,body);
    y+=85;
    label((g.s.srt_free?slt(st::MAIN_SIZE):T(S_SRT_SCALE))+std::wstring(L"  (50–200%)"),x,y);
    srt_slider(gr,ID_SRT_SCALE,RectF(x,y+29,col,32),g.s.srt_scale,50,200,body);
    label(T(S_SRT_OPACITY)+std::wstring(L"  (0–100%)"),right,y);
    srt_slider(gr,ID_SRT_OPACITY,RectF(right,y+29,col,32),g.s.srt_opacity,0,100,body);
    y+=85;
    option_card(gr,x,y,w,64,g.s.srt_border,ID_SRT_BORDER,T(S_SRT_BORDER),slt(st::BORDER_DESC),body,fsm);
    y+=90;
    /* Where it goes: game or window, a corner or anywhere, the inventory. */
    heading(slt(st::SEC_PLACE),y);y+=46;
    label(T(S_SRT_SHOW_IN),x,y);
    dropdown(gr,RectF(x,y+29,col,32),T(g.s.srt_window?S_SRT_IN_WINDOW:S_SRT_IN_GAME),ID_DD_SRT_WINDOW,body);
    label(T(S_SRT_CORNER),right,y);
    dropdown(gr,RectF(right,y+29,col,32),(g.s.srt_free?slt(st::FREE):T(S_SRT_TL+g.s.srt_corner)),ID_DD_SRT_CORNER,body,!g.s.srt_free);
    y+=61;
    if(g.s.srt_window){
        RectF used;gr.MeasureString(T(S_SRT_WINDOW_HINT),-1,&fsm,RectF(0,0,col,60),&sf,&used);
        note(T(S_SRT_WINDOW_HINT),RectF(x,y+6,col,60));y+=6+used.Height;
    }
    y+=24;
    option_card(gr,x,y,col,84,g.s.srt_free,ID_SRT_FREE,slt(st::FREE),slt(st::FREE_DESC),body,fsm);
    option_card(gr,right,y,col,84,g.s.srt_separate,ID_SRT_SEPARATE,slt(st::SEPARATE),slt(st::SEPARATE_DESC),body,fsm);
    y+=108;
    /* Only one of the two applies: the inventory beside the panel, or apart with a size of its own. */
    label(T(S_SRT_INV_POS),x,y);
    dropdown(gr,RectF(x,y+29,col,32),(g.s.srt_free&&g.s.srt_separate?slt(st::SEPARATE):T(g.s.srt_inventory_below?S_SRT_INV_BELOW:S_SRT_INV_SIDE)),ID_DD_SRT_INVPOS,body,!(g.s.srt_free&&g.s.srt_separate));
    label(slt(st::INV_SIZE)+std::wstring(L"  (50–200%)"),right,y);
    srt_slider(gr,ID_SRT_INV_SCALE,RectF(right,y+29,col,32),g.s.srt_inventory_scale,50,200,body,g.s.srt_separate);
    y+=61;
    if(g.s.srt_free){
        /* The preview, with its reset button and how to use it beside it. */
        y+=24;
        label(slt(st::PREVIEW),x,y);y+=29;
        g_srt_preview_y=y;
        const auto area=srt_layout_preview();SolidBrush bg(C_FIELD);Pen edge(C_FIELD_BD);gr.FillRectangle(&bg,area);gr.DrawRectangle(&edge,area);
        for(int i=0;i<(g.s.srt_separate?2:1);i++){
            const auto panel=srt_layout_panel(i);SolidBrush fill(i?Color(235,58,47,24):Color(235,41,36,38));Pen border(i?C_DIM:C_RED_HI,1.5f);
            gr.FillRectangle(&fill,panel);gr.DrawRectangle(&border,panel);
            const std::wstring name=i?T(S_SRT_INVENTORY):T(S_SRT_HEALTH);
            text(gr,name,panel.X+5,panel.Y+5,fsm,C_TEXT,StringAlignmentNear,panel.Width-10,24);
            hit(panel.X,panel.Y,panel.Width,panel.Height,i?ID_SRT_INV_MOVE:ID_SRT_MAIN_MOVE);
        }
        const float side=area.X+area.Width+24,sw=x+w-side;
        button(gr,RectF(side,y,sw,32),slt(st::RESET),ID_SRT_RESET,body);
        note(slt(st::HINT),RectF(side,y+46,sw,120));
        y+=area.Height;
    }
    g_page_end=std::max(g_page_end,y);
}

/* Keep map selection at the top of Cheats, visible before any scrolling.
 * Toggle, destination action and keyboard/controller hints have separate roles. */
float draw_map_selection(Graphics& gr,const Font& head,const Font& body,const Font& fsm)
{
    namespace mt=cv_launcher_maps;
    auto tr=[](mt::Id id){return mt::get(g_lang,id);};
    const float x=SIDE+32.f,w=W-SIDE-64.f,y=104,h=252;
    std::wstring title=tr(mt::Title);CharUpperBuffW(title.data(),DWORD(title.size()));
    section(gr,title,x,34,w,head);
    text(gr,tr(mt::Intro),x,70,fsm,C_DIM,StringAlignmentNear,w,24);
    RectF card(x,y,w,h);GraphicsPath* cp=round_rect(card,6);
    SolidBrush fill(C_PANEL);gr.FillPath(&fill,cp);Pen border(C_FIELD_BD,1);gr.DrawPath(&border,cp);delete cp;
    const float rowY=y+16,rowW=w-36;
    RectF row(x+18,rowY,rowW,58);GraphicsPath* rp=round_rect(row,4);
    SolidBrush rb(g.hover==ID_TEST_ENABLE?C_HOVER:C_FIELD);gr.FillPath(&rb,rp);gr.DrawPath(&border,rp);delete rp;
    text(gr,tr(mt::Enable),row.X+16,row.Y,body,C_TEXT,StringAlignmentNear,rowW-230,58);
    text(gr,tr(g.s.test_tools?mt::On:mt::Off),row.X+rowW-196,row.Y,fsm,C_DIM,StringAlignmentFar,110,58);
    RectF sw(row.X+rowW-66,row.Y+16,48,26);GraphicsPath* sp=round_rect(sw,13);
    SolidBrush sb(g.s.test_tools?C_RED:Color(255,60,52,52));gr.FillPath(&sb,sp);delete sp;
    SolidBrush knob(C_TEXT);gr.FillEllipse(&knob,g.s.test_tools?sw.X+25:sw.X+3,sw.Y+3,20.f,20.f);
    hit(row.X,row.Y,row.Width,row.Height,ID_TEST_ENABLE);
    text(gr,tr(g.running?mt::RunningHint:mt::StartHint),x+18,y+90,fsm,C_DIM,StringAlignmentNear,w-36,38);
    gr.DrawLine(&border,x+18,y+148,x+w-18,y+148);
    text(gr,tr(mt::Shortcuts),x+18,y+158,fsm,C_DIM,StringAlignmentNear,w-36,22);
    const float col=(w-36)/4;
    const wchar_t* labels[]={T(S_SC_COL_KEY),L"PlayStation",L"Xbox",L"Switch"};
    const wchar_t* keys[]={L"F7",L"R1 + R2 + L1 + L2 + ○",L"RB + RT + LB + LT + B",L"R + ZR + L + ZL + A"};
    for(int i=0;i<4;i++){
        const float cx=x+18+i*col;
        text(gr,labels[i],cx,y+186,fsm,C_DIM,StringAlignmentNear,col-16,20);
        if(!i)key_glyph(gr,VK_F7,cx,y+208,28);
        else text(gr,keys[i],cx,y+208,fsm,C_TEXT,StringAlignmentNear,col-16,28);
        if(i)gr.DrawLine(&border,cx-10,y+190,cx-10,y+236);
    }
    return y+h;
}

/* The F6 item menu: the switch and how to use it (cv_item_menu.cpp). */
void draw_inventario(Graphics& gr,const Font& head,const Font& body,const Font& fsm)
{
    const float x=SIDE+32.f,w=W-SIDE-64.f;
    const float top=draw_map_selection(gr,head,body,fsm)+28;
    section(gr,T(S_ITEMS_TITLE),x,top,w,head);
    text(gr,T(S_ITEMS_INTRO),x,top+36,fsm,C_DIM,StringAlignmentNear,w,24);
    option_card(gr,x,top+70,w,84,g.s.items_menu,ID_ITEMS_ENABLE,T(S_ITEMS_ENABLE_T),T(S_ITEMS_ENABLE_D),body,fsm);
    /* How to use it: one row per action, keyboard / PlayStation / Xbox / Switch side by side. */
    const float y=top+176;
    RectF card(x,y,w,236);GraphicsPath* cp=round_rect(card,6);
    SolidBrush cb(C_PANEL);gr.FillPath(&cb,cp);Pen cbd(C_FIELD_BD,1);gr.DrawPath(&cbd,cp);delete cp;
    text(gr,T(S_ITEMS_HOW_T),x+20,y+12,head,C_RED_HI,StringAlignmentNear,w-40,26);
    {
        const float ax=x+20,kx=x+290,px=x+430,xx=x+600,sx=x+770,hy=y+46;
        text(gr,T(S_SC_COL_ACTION),ax,hy,fsm,C_DIM,StringAlignmentNear,260,20);
        text(gr,T(S_SC_COL_KEY),kx,hy,fsm,C_DIM,StringAlignmentNear,130,20);
        text(gr,L"PlayStation",px,hy,fsm,C_DIM,StringAlignmentNear,160,20);
        text(gr,L"Xbox",xx,hy,fsm,C_DIM,StringAlignmentNear,160,20);
        text(gr,L"Nintendo Switch",sx,hy,fsm,C_DIM,StringAlignmentNear,w-(sx-x)-20,20);
        struct Row{const wchar_t* what;const wchar_t* key;const wchar_t* ps;const wchar_t* xb;const wchar_t* sw;};
        const Row rows[]={
            {lx(design::ITEM_OPEN),L"F6  /  Esc",L"R1+R2+L1+L2+\u25B3",L"RB+RT+LB+LT+Y",L"R+ZR+L+ZL+X"},
            {lx(design::ITEM_CATEGORY),L"Q  /  E",L"L1  /  R1",L"LB  /  RB",L"L  /  R"},
            {lx(design::ITEM_CHOOSE),L"\u2191 \u2193 \u2190 \u2192",lx(design::DPAD),lx(design::DPAD),lx(design::DPAD)},
            {lx(design::ITEM_PUT),L"Enter",L"\u2715",L"A",L"B"},
            {lx(design::ITEM_TAKE),L"R",L"\u25A1",L"X",L"Y"},
        };
        SolidBrush line(Color(255,46,38,38));float ry=hy+26;
        for(const Row& r:rows){
            gr.FillRectangle(&line,x+20,ry-3,w-40,1.f);
            text(gr,r.what,ax,ry,body,C_TEXT,StringAlignmentNear,260,30);
            text(gr,r.key,kx,ry,body,C_TEXT,StringAlignmentNear,130,30);
            text(gr,r.ps,px,ry,body,C_TEXT,StringAlignmentNear,160,30);
            text(gr,r.xb,xx,ry,body,C_TEXT,StringAlignmentNear,160,30);
            text(gr,r.sw,sx,ry,body,C_TEXT,StringAlignmentNear,w-(sx-x)-20,30);
            ry+=30;
        }
    }
    /* The warning, short, in the SRT's amber. */
    RectF warn(x,y+252,w,52);GraphicsPath* wp=round_rect(warn,6);
    SolidBrush wb(Color(255,38,30,22));gr.FillPath(&wb,wp);Pen wbd(Color(255,120,90,40),1);gr.DrawPath(&wbd,wp);delete wp;
    SolidBrush bar(Color(255,226,172,82));gr.FillRectangle(&bar,warn.X,warn.Y,3.f,warn.Height);
    {
        const std::wstring t=g_lang==LANG_AR?L"\u202B"+std::wstring(lx(design::ITEM_WARN))+L"\u202C":lx(design::ITEM_WARN);
        StringFormat sf;sf.SetAlignment(StringAlignmentNear);sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush tb(Color(255,226,190,130));gr.DrawString(t.c_str(),-1,&fsm,RectF(warn.X+18,warn.Y+6,warn.Width-32,warn.Height-12),&sf,&tb);
    }
    /* Infinite health and ammunition (cv_cheats.cpp), moved here from Patches. */
    float cy=warn.Y+warn.Height;
    bool any=false;for(int i=0;i<NPATCHES;i++)any=any||is_cheat(i);
    if(any){cy+=26;section(gr,T(S_ITEMS_CHEATS),x,cy,w,head);cy+=40;}
    for(int i=0;i<NPATCHES;i++) {
        if(!is_cheat(i))continue;
        patch_card(gr,x,cy,w,58,g.s.patch[i],true,ID_PATCH+i,T(k_patches[i].title),T(k_patches[i].desc),
                   T(k_patches[i].credit),false,body,fsm);
        cy+=66;
    }
    cy+=16;
    option_card(gr,x,cy,w,90,g.s.battle_unlocked,ID_BATTLE,
                cv_enhanced_text::get(g_lang,cv_enhanced_text::Battle),
                cv_enhanced_text::get(g_lang,cv_enhanced_text::BattleHelp),body,fsm);
    cy+=106;
    g_page_end=std::max(g_page_end,cy);
}

void draw_desempenho(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    const float w = W - SIDE - 64.0f;
    section(gr, T(S_SH_TITLE), x, y, w, head);
    text(gr, T(S_SH_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, w, 20);
    y += 74;
    text(gr,cv_shader_text::get(g_lang,cv_shader_text::Title),x,y,body,C_TEXT,StringAlignmentNear,w,24);
    dropdown(gr,RectF(x,y+28,w,34),cv_shader_text::get(g_lang,g.s.shader_compiler?cv_shader_text::FXC:cv_shader_text::DXC),ID_SHADER_COMPILER,body);
    text(gr,cv_shader_text::get(g_lang,cv_shader_text::Help),x,y+71,fsm,C_DIM,StringAlignmentNear,w,50);y+=138;
    for (int i = 0; i < NSHADER_OPTS; i++) {
        /* Six cards end above the Jogar button (its top is H - 78). */
        option_card(gr, x, y, w, 86, g.s.shader_opt[i], ID_SHADER_OPT + i,
                    T(k_shader_opts[i].title), T(k_shader_opts[i].desc), body, fsm);
        y += 94;
    }
    g_page_end=std::max(g_page_end,y+16);
}

/* Mod input and extraction output are independent, using the selected region. */
fs::path texture_executable_directory() {
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);
    return fs::path(exe).parent_path();
}
std::wstring texture_game_code() {
    if(g.running)return GAME_CODE;
    const auto selected=folder_code();return version_of(selected)?selected:GAME_CODE;
}
fs::path tex_folder() {
    return cv_texture_paths::mod_folder(texture_executable_directory(),g.s.tex_dir);
}
fs::path tex_dump_folder() {
    return cv_texture_paths::dump_folder(texture_executable_directory(),g.s.tex_dump_dir,texture_game_code());
}
fs::path tex_replacement_folder() {
    return cv_texture_paths::replacement_folder(tex_folder(),texture_game_code());
}
bool texture_folders_distinct(const fs::path& input,const fs::path& output) {
    if(!cv_texture_paths::same_folder(input,output))return true;
    MessageBoxW(g.hwnd,T(S_TEX_PATH_CONFLICT),L"Code: Veronica",MB_OK|MB_ICONINFORMATION);return false;
}
int count_png(const fs::path& dir, bool recurse, const fs::path& excluded = {})
{
    int n = 0; std::error_code ec;
    if (!recurse) {
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && (_wcsicmp(e.path().extension().c_str(), L".png") == 0||_wcsicmp(e.path().extension().c_str(), L".dds") == 0)) n++;
        return n;
    }
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec)) { if (_wcsicmp(it->path().filename().c_str(), L"dump") == 0 || (!excluded.empty() && cv_texture_paths::same_folder(it->path(),excluded))) it.disable_recursion_pending(); continue; }
        if ((_wcsicmp(it->path().extension().c_str(), L".png") == 0||_wcsicmp(it->path().extension().c_str(), L".dds") == 0)) n++;
    }
    return n;
}
void scan_tex_dir()
{
    std::error_code ec;
    const auto root = tex_replacement_folder();
    g.tex_root_name = texture_game_code();
    g.tex_rep = count_png(root, true, tex_dump_folder());
    g.tex_dumped = count_png(tex_dump_folder(), false);
}

/* Player shortcuts in the game (not the developer captures F9/F12): the keys
 * from key_hook / the renderer, the controller chords from cv_launcher_controls. */
void draw_atalhos(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    const float w = W - SIDE - 64.0f;
    section(gr, T(S_SC_TITLE), x, y, w, head);
    text(gr, T(S_SC_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, w, 20);
    y += 74;
    /* keyboard, PlayStation, Xbox and Switch side by side, whatever is connected */
    const float kx = x, px = x + 110, xx = x + 260, sx = x + 410, ax = x + 560, aw = w - 560;
    text(gr, T(S_SC_COL_KEY), kx, y, fsm, C_DIM, StringAlignmentNear, 100, 20);
    text(gr, L"PlayStation", px, y, fsm, C_DIM, StringAlignmentNear, 145, 20);
    text(gr, L"Xbox", xx, y, fsm, C_DIM, StringAlignmentNear, 145, 20);
    text(gr, L"Nintendo Switch", sx, y, fsm, C_DIM, StringAlignmentNear, 145, 20);
    text(gr, T(S_SC_COL_ACTION), ax, y, fsm, C_DIM, StringAlignmentNear, aw, 20);
    y += 26;
    wchar_t items[300], maps[300];
    swprintf(items, 300, T(S_SC_ITEMS), T(S_NAV_ITEMS));
    swprintf(maps, 300, T(S_SC_MAPS), T(S_NAV_ITEMS));
    struct Row { const wchar_t* key; const wchar_t* ps; const wchar_t* xb; const wchar_t* sw; const wchar_t* what; };
    const Row rows[] = {
        { L"Insert / F5", L"L3", L"LS", L"LS", T(S_SC_MENU) },
        { L"F6", L"△", L"Y", L"X", items },
        { L"F7", L"○", L"B", L"A", maps },
        { L"F8", L"✕", L"A", L"B", T(S_SC_SRT) },
        { L"F9", L"□", L"X", L"Y", T(S_SC_TEX) },
        { L"F10", L"R3", L"RS", L"RS", T(S_SC_SHOT) },
    };
    SolidBrush line(Color(255, 46, 38, 38));
    for (const Row& r : rows) {
        gr.FillRectangle(&line, x, y - 4, w, 1.0f);
        text(gr, r.key, kx, y, body, C_TEXT, StringAlignmentNear, 100, 40);
        text(gr, std::wstring(L"R1+R2+L1+L2+") + r.ps, px, y, body, C_TEXT, StringAlignmentNear, 145, 40);
        text(gr, std::wstring(L"RB+RT+LB+LT+") + r.xb, xx, y, body, C_TEXT, StringAlignmentNear, 145, 40);
        text(gr, std::wstring(L"R+ZR+L+ZL+") + r.sw, sx, y, body, C_TEXT, StringAlignmentNear, 145, 40);
        {   /* the action may take two lines in the narrower column */
            const std::wstring what = g_lang == LANG_AR ? L"\u202B" + std::wstring(r.what) + L"\u202C" : std::wstring(r.what);
            StringFormat sf; sf.SetLineAlignment(StringAlignmentCenter); sf.SetTrimming(StringTrimmingEllipsisWord);
            if (g_lang == LANG_AR) sf.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
            SolidBrush tb(C_TEXT); gr.DrawString(what.c_str(), -1, &fsm, RectF(ax, y - 2, aw, 44), &sf, &tb);
        }
        y += 48;
    }
    gr.FillRectangle(&line, x, y - 4, w, 1.0f);
    text(gr, T(S_SC_PAD_NOTE), x, y + 6, fsm, C_DIM, StringAlignmentNear, w, 40);
    g_page_end = std::max(g_page_end, y + 50);
}

void draw_mods(Graphics& gr, const Font& head, const Font& body, const Font& fsm)
{
    float x = SIDE + 32.0f, y = 34;
    const float w = W - SIDE - 64.0f;
    section(gr, T(S_MODS_TITLE), x, y, w, head);
    text(gr, T(S_MODS_HINT), x, y + 32, fsm, C_DIM, StringAlignmentNear, w, 20);
    y += 74;
    const auto extraction=cv_texture_export_status();
    if (g.tex_rep < 0 || g.tex_root_name != texture_game_code()) scan_tex_dir();
    wchar_t note[512];
    swprintf(note, 512, T(S_TEX_COUNT_FMT), g.tex_rep, g.tex_root_name.c_str());
    path_row(gr, x, y, T(S_TEX_FOLDER), tex_folder().wstring(), note, true, ID_TEX_BROWSE, ID_TEX_OPEN, body, fsm, 220);
    if(g.running)text(gr,T(S_TEX_MOD_RESTART),x+225,y+57,fsm,C_DIM,StringAlignmentNear,w-225,20);
    y += g.running?88:66;
    swprintf(note, 512, T(S_TEX_DUMP_COUNT_FMT), g.tex_dumped);
    path_row(gr, x, y, T(S_TEX_DUMP_FOLDER), tex_dump_folder().wstring(), note, true, extraction.running?-1:ID_TEX_DUMP_BROWSE, ID_TEX_DUMP_OPEN, body, fsm, 220);
    y += 70;
    const wchar_t* format_labels[LANG_COUNT]={L"Formato de exportação",L"Export format",L"Formato de exportación",L"書き出し形式",L"Format d’exportation",L"Exportformat",L"Formato di esportazione",L"Format eksportu",L"Dışa aktarma biçimi",L"صيغة التصدير",L"导出格式",L"내보내기 형식",L"Формат экспорта"};
    text(gr,format_labels[g_lang],x,y+7,body,C_TEXT,StringAlignmentNear,230,28);
    button(gr,RectF(x+250,y,100,32),L"PNG",ID_TEX_FORMAT_PNG,body,!g.s.tex_dds,!extraction.running);
    button(gr,RectF(x+362,y,100,32),L"DDS",ID_TEX_FORMAT_DDS,body,g.s.tex_dds,!extraction.running);
    y+=50;
    option_card(gr, x, y, w, 92, g.s.tex_replace, ID_TEX_REPLACE, T(S_TEX_REPLACE_T), T(S_TEX_REPLACE_D), body, fsm);
    y += 104;
    option_card(gr, x, y, w, 92, g.s.tex_dump, ID_TEX_DUMP, T(S_TEX_DUMP_T), T(S_TEX_DUMP_D), body, fsm);
    y += 104;
    option_card(gr, x, y, w, 108, g.s.tex_dump_all, ID_TEX_ALL, T(S_TEX_ALL_T), T(S_TEX_ALL_D), body, fsm);
    y += 120;
    button(gr, RectF(x, y, 260, 34), T(extraction.running?S_TEX_ALL_CANCEL:S_TEX_ALL_START), ID_TEX_ALL_RUN,
           body, false, extraction.running||g.s.tex_dump_all);
    if(extraction.running||extraction.finished){
        wchar_t progress[300];swprintf(progress,300,T(S_TEX_ALL_PROGRESS),extraction.files,extraction.total,
            extraction.written,extraction.existing,extraction.unsupported,extraction.errors);
        text(gr,progress,x+276,y,fsm,C_DIM,StringAlignmentNear,w-276,36);
        y+=42;
        if(extraction.finished)text(gr,T(extraction.failed?S_TEX_ALL_FAILED:extraction.canceled?S_TEX_ALL_CANCELED:S_TEX_ALL_DONE),x,y,fsm,
            extraction.failed||extraction.errors?C_RED_HI:C_OK,StringAlignmentNear,w,40);
    }
    y += 52;
    text(gr, T(S_TEX_HOTKEY), x, y, fsm, C_TEXT, StringAlignmentNear, w, 20);
    y += 26;
    const std::wstring howto = g_lang == LANG_AR ? L"\u202B" + std::wstring(T(S_TEX_HOWTO)) + L"\u202C" : T(S_TEX_HOWTO);
    StringFormat sf; sf.SetAlignment(StringAlignmentNear); sf.SetLineAlignment(StringAlignmentNear);
    SolidBrush hb(C_DIM);
    gr.DrawString(howto.c_str(), -1, &fsm, RectF(x, y, w, 170), &sf, &hb);   /* help + PS2 texture pack tip */
    g_page_end=std::max(g_page_end,y+170);
}

/* The page's scroll bar, at the window's right edge, and a fade above the
 * Jogar bar while more of the page is below. The Patches page keeps its own
 * list scrolling and always fits. */
void draw_page_scroll(Graphics& gr, int pi)
{
    const float max = pi == P_PATCHES ? 0.0f : std::max(0.0f, g_page_end + 24 - PAGE_BOTTOM);
    g.page_scroll_max[pi] = max;
    if (g.page_scroll[pi] > max) { g.page_scroll[pi] = std::floor(max); InvalidateRect(g.hwnd, NULL, FALSE); }
    g.page_track = g.page_thumb = RectF();
    if (max <= 0) return;
    const float sc = g.page_scroll[pi];
    if (sc < max) {
        LinearGradientBrush fade(PointF(0, PAGE_BOTTOM - 28), PointF(0, PAGE_BOTTOM + 1), Color(0, 21, 17, 17), C_BG);
        gr.FillRectangle(&fade, (float)SIDE, PAGE_BOTTOM - 28, (float)(W - SIDE) - 14, 28.0f);
    }
    const float top = 8, track = PAGE_BOTTOM - 16, view = PAGE_BOTTOM;
    const float bar = std::max(40.0f, track * view / (view + max));
    g.page_track = RectF(W - 12.0f, top, 10.0f, track);
    g.page_thumb = RectF(W - 12.0f, top + (track - bar) * sc / max, 10.0f, bar);
    SolidBrush tb(Color(255, 30, 25, 25)); gr.FillRectangle(&tb, W - 9.0f, top, 4.0f, track);
    const bool hot = g.page_drag || g.hover == ID_PAGE_THUMB;
    SolidBrush th(hot ? C_RED : Color(255, 90, 80, 78));
    gr.FillRectangle(&th, W - 10.0f, g.page_thumb.Y, 6.0f, bar);
    hit(g.page_track.X, g.page_track.Y, g.page_track.Width, g.page_track.Height, ID_PAGE_TRACK);
    hit(g.page_thumb.X, g.page_thumb.Y, g.page_thumb.Width, g.page_thumb.Height, ID_PAGE_THUMB);
}

void page_scroll_to(float v)
{
    const int pi = g.page;
    v = std::round(std::clamp(v, 0.0f, g.page_scroll_max[pi]));   /* whole pixels: cached art copies 1:1 */
    if (v != g.page_scroll[pi]) { g.page_scroll[pi] = v; g.open_dd = -1; InvalidateRect(g.hwnd, NULL, FALSE); }
}
void page_drag_to(int mouse_y)
{
    const float room = g.page_track.Height - g.page_thumb.Height;
    if (room > 0) page_scroll_to((mouse_y - g.page_grab - g.page_track.Y) / room * g.page_scroll_max[g.page]);
}

void paint(HDC hdc)
{
    Bitmap back(W, H, PixelFormat32bppPARGB);
    Graphics gr(&back);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gr.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    SolidBrush bg(C_BG); gr.FillRectangle(&bg, 0, 0, W, H);
    if(g.castle){image_fit(gr,g.castle,RectF(float(SIDE),0,float(W-SIDE),PAGE_BOTTOM),true);SolidBrush shade(Color(195,12,13,16));gr.FillRectangle(&shade,float(SIDE),0.f,float(W-SIDE),PAGE_BOTTOM);}
    g.hits.clear();

    FontFamily serif(family(true, g_lang)), sans(family(false, g_lang));
    Font fTitle(&serif, 22, FontStyleBold, UnitPixel), fTitle2(&serif, 17, FontStyleBold, UnitPixel);
    Font fNav(&sans, 15, FontStyleRegular, UnitPixel), fHead(&serif, 18, FontStyleRegular, UnitPixel);
    Font fBody(&sans, 14, FontStyleRegular, UnitPixel), fSmall(&sans, 12, FontStyleRegular, UnitPixel);
    Font fCode(&serif, 18, FontStyleBold, UnitPixel), fBig(&serif, 22, FontStyleBold, UnitPixel);
    Font fCol(&serif, 15, FontStyleBold, UnitPixel);

    const int pi = g.page;
    g_in_page = true; g_page_dy = g.page_scroll[pi]; g_page_end = 0;
    gr.SetClip(RectF((float)SIDE, 0.0f, (float)(W - SIDE), PAGE_BOTTOM));
    gr.TranslateTransform(0, -g_page_dy);
    if (g.page == P_INICIO) draw_inicio(gr, fHead, fBody, fCode, fSmall);
    else if (g.page == P_HUD) draw_hud_page(gr,fHead,fBody,fSmall);
    else if (g.page == P_JOGO) draw_jogo(gr, fHead, fBody, fSmall);
    else if (g.page == P_SAVES) draw_saves(gr, fHead, fBody, fSmall);
    else if (g.page == P_CONTROLES) draw_controles(gr, fHead, fBody, fSmall, fCol);
    else if (g.page == P_ATALHOS) draw_atalhos(gr, fHead, fBody, fSmall);
    else if (g.page == P_PATCHES) draw_patches(gr, fHead, fBody, fSmall);
    else if (g.page == P_DESEMPENHO) draw_desempenho(gr, fHead, fBody, fSmall);
    else if (g.page == P_MODS) draw_mods(gr, fHead, fBody, fSmall);
    else if (g.page == P_TROPHIES) draw_trophies(gr,fHead,fBody,fSmall);
    else if (g.page == P_SRT) draw_srt(gr, fHead, fBody, fSmall);
    else if (g.page == P_INVENTARIO) draw_inventario(gr, fHead, fBody, fSmall);
    else draw_avancado(gr, fHead, fBody, fSmall);
    gr.ResetTransform(); gr.ResetClip();
    g_in_page = false; g_page_dy = 0;
    draw_page_scroll(gr, pi);
    draw_sidebar(gr, fTitle, fTitle2, fNav, fSmall);
    draw_play(gr, fBig, fSmall);
    draw_update_card(gr, fBody, fSmall);
    if (upd_modal()) { g.open_dd = -1; draw_update_dialog(gr, fTitle, fBody, fSmall); }

    /* open dropdown list on top */
    if (g.open_dd >= 0) {
        std::vector<std::wstring> items; std::vector<int> item_lang; int sel = 0;
        const float x=g_dd_anchor.X,bw=g_dd_anchor.Width;float y=g_dd_anchor.Y+g_dd_anchor.Height;
        if (g.open_dd == ID_DD_MODE) { for (int i = 0; i < 3; i++) items.push_back(mode_name(i)); sel = g.s.mode; }
        else if(g.open_dd==ID_DD_TROPHY_CORNER){for(int i=0;i<4;i++)items.push_back(T(S_SRT_TL+i));sel=g.s.trophy_corner;}
        else if(g.open_dd==ID_DD_TROPHY_FILTER){items={trophy_text(tt::All),trophy_text(tt::Unlocked),trophy_text(tt::Locked)};sel=trophy_filter;}
        else if (g.open_dd == ID_DD_RES) { for (int i = 0; i < 5; i++) items.push_back(res_name(i)); sel = g.s.res; }
        else if (g.open_dd == ID_DD_AA) { for (int i = 0; i < 3; i++) items.push_back(aa_name(i)); sel = g.s.aa; }
        else if(g.open_dd==ID_DD_POSE_DEVICE){for(int i=0;i<2;i++)items.push_back(pose_device_name(i));sel=g.s.pose_device;}
        else if(g.open_dd==ID_DD_FRAME_GEN){for(int i=0;i<2;i++)items.push_back(frame_generation_name(i));sel=g.s.frame_generation;}
        else if(g.open_dd==ID_DD_VSYNC){for(int i=0;i<2;i++)items.push_back(vsync_name(i));sel=g.s.vsync;}
        else if (g.open_dd == ID_DD_FPS) { for (int i = 0; i < 2; i++) items.push_back(fps_name(i)); sel = g.s.fps; }
        else if (g.open_dd == ID_DD_GAME_TEXT) {items={ptbr_ui(1),ptbr_ui(2)};sel=g.s.game_text_ptbr;}
        else if(g.open_dd==ID_DD_MANUAL_RELOAD){items={cv_enhanced_text::get(g_lang,cv_enhanced_text::Off),cv_enhanced_text::get(g_lang,cv_enhanced_text::On)};sel=g.s.manual_reload?1:0;}
        else if(g.open_dd>=ID_QUICK_SLOT&&g.open_dd<ID_QUICK_SLOT+4){const auto choices=quick_item_choices();sel=0;for(int i=0;i<int(choices.size());i++){items.push_back(quick_item_label(choices[i]));if(choices[i]==g.s.quick.items[g.open_dd-ID_QUICK_SLOT])sel=i;}}
        else if(g.open_dd==ID_SHADER_COMPILER){items={cv_shader_text::get(g_lang,cv_shader_text::DXC),cv_shader_text::get(g_lang,cv_shader_text::FXC)};sel=g.s.shader_compiler;}
        else if(g.open_dd==ID_DD_COMBAT_HUD){items={cv_enhanced_text::get(g_lang,cv_enhanced_text::Off),cv_enhanced_text::get(g_lang,cv_enhanced_text::On)};sel=g.s.combat_hud?1:0;}
        else if(g.open_dd==ID_DD_SHADOWS){items={cv_enhanced_text::get(g_lang,cv_enhanced_text::Off),cv_enhanced_text::get(g_lang,cv_enhanced_text::On)};sel=g.s.dynamic_shadows?1:0;}
        else if (g.open_dd == ID_DD_FILM) {items.push_back(cv_enhanced_text::get(g_lang,cv_enhanced_text::Off));items.push_back(cv_enhanced_text::get(g_lang,cv_enhanced_text::On));sel=g.s.film_filter?1:0;}
        else if (g.open_dd == ID_DD_SHARP) { for (int i = 0; i <= 10; i++) items.push_back(sharp_name(i)); sel = g.s.sharpness / 10; }
        else if (g.open_dd == ID_DD_SRT_ENEMIES) { for(int i=0;i<=5;i++)items.push_back(srt_enemy_value(i));items.push_back(T(S_SRT_ALL));sel=g.s.srt_enemies==99?6:g.s.srt_enemies; }
        else if (g.open_dd == ID_DD_SRT_CORNER) {for(int i=0;i<4;i++)items.push_back(T(S_SRT_TL+i));sel=g.s.srt_corner;}
        else if (g.open_dd == ID_DD_SRT_INVPOS) { items = { T(S_SRT_INV_SIDE), T(S_SRT_INV_BELOW) }; sel = g.s.srt_inventory_below; }
        else if (g.open_dd == ID_DD_SRT_WINDOW) { items = { T(S_SRT_IN_GAME), T(S_SRT_IN_WINDOW) }; sel = g.s.srt_window; }
        else if (g.open_dd == ID_DD_SRT_BOSSBAR) { items = { T(S_SRT_BOSS_PANEL), T(S_SRT_BOSS_TOP), T(S_SRT_BOSS_BOTTOM) }; sel = g.s.srt_boss_bar; }
        else if (g.open_dd == ID_DD_OV_CORNER) { for (int i = 0; i < 4; i++) items.push_back(T(S_SRT_TL + i)); sel = g.s.overlay_corner; }
        else if (g.open_dd == ID_DD_OV_LAYOUT) { items = { T(S_OV_ROW), T(S_OV_COLUMN) }; sel = g.s.overlay_layout; }
        else if (g.open_dd == ID_DD_OV_SIZE) { items = { lx(design::OV_SMALL), lx(design::OV_MEDIUM), lx(design::OV_LARGE) }; sel = g.s.overlay_size; }
        else if (g.open_dd == ID_DD_OV_BG) { items = { lx(design::OV_BG_BOX), lx(design::OV_BG_TEXT) }; sel = g.s.overlay_text_only; }
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

    Graphics screen(hdc);RECT client{};GetClientRect(g.hwnd,&client);if(client.right==W&&client.bottom==H){screen.DrawImage(&back,0,0,W,H);return;}   /* 1:1, no resampling */
    screen.SetInterpolationMode(InterpolationModeHighQualityBicubic);screen.DrawImage(&back,RectF(0,0,float(client.right),float(client.bottom)));
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
void sync_srt_positions();

/* The vibration strength, read by pad_phys_rumble on every call. */
void set_rumble_env() { _putenv_s("PAD_RUMBLE_SCALE", std::to_string(g.s.rumble).c_str()); }
/* A short pulse at the chosen strength on every controller (timer 7 stops it). */
void rumble_test()
{
    for (int i = 0; i < PAD_PHYS_MAX; i++) pad_phys_rumble(i, 128, 64);
    SetTimer(g.hwnd, 7, 400, NULL);
}

void apply_live(bool restore_menu = true)
{
    set_rumble_env();
    cv_trophies::options(g.s.trophies,g.s.trophy_notifications,g_lang);
    cv_trophy_popup::corner(g.s.trophy_corner);
    cv_test_tools_enable(g.s.test_tools);
    cv_item_menu_enable(g.s.items_menu);
    cv_cheats_set(g.s.patch[2],g.s.patch[3]);
    cv_enhanced_set(8,g.s.music_volume);cv_enhanced_set(9,g.s.sfx_volume);cv_enhanced_set(10,g.s.battle_unlocked);cv_enhanced_set(11,g.s.film_filter);cv_enhanced_set(12,g.s.dynamic_shadows);cv_enhanced_set(13,g.s.combat_hud);cv_enhanced_set(14,g.s.manual_reload);cv_manual_reload_bindings_set(g.s.reload_key,g.s.reload_button);cv_quick::configure(g.s.quick);
    cv_focus_pause_enable(g.s.focus_pause);
    cv_focus_pause_label(T(S_PAUSED));
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
    if(g.s.fps==1&&!opt30_comparison()&&!(g.s.frame_generation&&frame_generation_allowed()))rsx_d3d12_set_vsync(g.s.vsync);
    g_pad_shortcut_chords=1;
    cellpad_set_map(&g.s.btn[0][0], &g.s.key[0][0]);
    set_map_env();
    for (int i = 0; i < NPATCHES; i++) ps3_patch_set(k_patches[i].id, g.s.patch[i]);
}

void show_menu()
{
    sync_srt_positions();
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
const UINT WM_PC_SETTINGS_READ_EFFECTS = WM_APP + 7;
void menu_hook() { PostMessageW(g.hwnd, WM_TOGGLE_MENU, 0, 0); }   /* game window thread */
const UINT WM_TOGGLE_TEXTURES = WM_APP + 5;
const UINT WM_TOGGLE_SRT = WM_APP + 6;
const UINT WM_SRT_POSITIONS = WM_APP + 8;
void sync_srt_positions() {
    if(!cv_srt_take_positions(&g.s.srt_main_x,&g.s.srt_main_y,&g.s.srt_inv_x,&g.s.srt_inv_y))return;
    ini_set(L"srt",L"painel_x",std::to_wstring(g.s.srt_main_x));ini_set(L"srt",L"painel_y",std::to_wstring(g.s.srt_main_y));
    ini_set(L"srt",L"inventario_x",std::to_wstring(g.s.srt_inv_x));ini_set(L"srt",L"inventario_y",std::to_wstring(g.s.srt_inv_y));
    InvalidateRect(g.hwnd,NULL,FALSE);
}
const UINT WM_TEST_MAPS = WM_APP + 20;
const UINT WM_ITEM_MENU = WM_APP + 21;
static_assert(WM_PC_SETTINGS_READ_EFFECTS!=WM_TOGGLE_TEXTURES && WM_PC_SETTINGS_READ_EFFECTS!=WM_TOGGLE_SRT &&
              WM_PC_SETTINGS_READ_EFFECTS!=WM_TEST_MAPS && WM_PC_SETTINGS_READ_EFFECTS!=WM_ITEM_MENU,
              "Native settings message must not consume launcher shortcuts");
/* F5..F10 in the game window: the options menu (Insert as well), the item
 * menu, the maps, the SRT and the texture mods; F10 (a screenshot) is the
 * game window's own. */
void key_hook(unsigned vk) { if (vk == VK_F9) PostMessageW(g.hwnd, WM_TOGGLE_TEXTURES, 0, 0);
    else if(vk==VK_F5)menu_hook();
    else if(vk==VK_F6)PostMessageW(g.hwnd,WM_ITEM_MENU,0,0);
    else if(vk==VK_F8)PostMessageW(g.hwnd,WM_TOGGLE_SRT,0,0);
    else if(vk==VK_F7)PostMessageW(g.hwnd,WM_TEST_MAPS,PAD_INPUT_KEYBOARD,0); }   /* game window thread */
/* F9 in the game or R1+R2+L1+L2+square on a controller: texture mods on or off at once,
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

/* The SRT's size and opacity, and the vibration strength (Controles). */
void update_srt_slider(int id,int x) {
    const RectF r=srt_slider_rect(id);
    const bool audio=id==ID_AUDIO_MUSIC||id==ID_AUDIO_SFX;
    int& value=id==ID_AUDIO_MUSIC?g.s.music_volume:id==ID_AUDIO_SFX?g.s.sfx_volume:id==ID_RUMBLE?g.s.rumble:id==ID_SRT_SCALE?g.s.srt_scale:id==ID_SRT_INV_SCALE?g.s.srt_inventory_scale:g.s.srt_opacity;
    const int next=cv_launcher_controls::slider_value(float(x),r.X+8,r.Width-16,(id==ID_SRT_SCALE||id==ID_SRT_INV_SCALE)?50:0,(audio||id==ID_SRT_OPACITY)?100:200);
    if(next==value)return;
    value=next;g.srt_drag_dirty=true;
    if(audio)cv_enhanced_set(id==ID_AUDIO_MUSIC?8:9,value);else if(id==ID_RUMBLE)set_rumble_env();else set_srt_env();
    InvalidateRect(g.hwnd,NULL,FALSE);
}
void finish_srt_drag() {
    if(g.srt_layout_drag>=0){
        g.srt_layout_drag=-1;const bool dirty=g.srt_layout_dirty;g.srt_layout_dirty=false;
        if(GetCapture()==g.hwnd)ReleaseCapture();
        if(dirty){set_env("SRT_RESET_WINDOWS","1");set_srt_env();set_env("SRT_RESET_WINDOWS","");changed();}
    }
    if(g.srt_drag<0)return;
    const bool dirty=g.srt_drag_dirty,rumble=g.srt_drag==ID_RUMBLE;g.srt_drag=-1;g.srt_drag_dirty=false;
    if(GetCapture()==g.hwnd)ReleaseCapture();
    if(rumble)rumble_test();
    if(dirty)changed();else InvalidateRect(g.hwnd,NULL,FALSE);
}

void click(int id)
{
    if (upd_modal()) {   /* the update dialog is modal */
        if (id == ID_UPD_INSTALL && g_upd.step == UPD_OFFER) upd_start_install();
        else if (id == ID_UPD_LATER && g_upd.step == UPD_OFFER) g_upd.step = UPD_NONE;
        else if (id == ID_UPD_CLOSE && g_upd.step == UPD_FAILED) g_upd.step = UPD_NONE;
        InvalidateRect(g.hwnd, NULL, FALSE); return;
    }
    if (id == ID_UPD_CANCEL && g_upd.step == UPD_DOWNLOADING) { g_upd.prog.cancel = true; return; }
    /* the game must not start while its files are being downloaded or replaced */
    if (id == ID_PLAY && upd_busy()) return;
    if(id==ID_SOCIAL_GITHUB||id==ID_SOCIAL_YOUTUBE){
        const auto url=social_url(id-ID_SOCIAL_GITHUB);
        if(!url.empty())ShellExecuteW(g.hwnd,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        return;
    }
    if((id>=2700&&id<2703)&&!g.running){
        g_version_picked=true;
        g.s.region_selected=id-2700;g.s.game=g.s.region_game[id-2700];g.s.rap=g.s.region_rap[id-2700];changed();return;
    }
    if(g.running&&(id==2710||id==ID_GAME_BROWSE||id==ID_RAP_BROWSE||(id>=2720&&id<2723)||(id>=2740&&id<2743)))return;
    if(id>=2720&&id<2723){   /* a version's RAP license, from its card */
        const int i=id-2720;auto path=pick(false,g.s.region_rap[i],T(S_RAP_FILTER),L"*.rap");
        if(!path.empty()){g.s.region_rap[i]=path;if(g.s.region_selected==i)g.s.rap=path;changed();}
        return;
    }
    if(id>=2730&&id<2733){open_in_explorer(g.s.region_game[id-2730]);return;}
    if(id>=2740&&id<2743){   /* forget the version; its files stay on disk */
        const int i=id-2740;g.s.region_game[i].clear();g.s.region_rap[i].clear();
        if(g.s.region_selected==i){
            g.s.game.clear();g.s.rap.clear();
            for(int r:region_order)if(!g.s.region_game[r].empty()){g.s.region_selected=r;g.s.game=g.s.region_game[r];g.s.rap=g.s.region_rap[r];break;}
        }
        changed();return;
    }

    if(id==ID_TROPHY_ENABLE){g.s.trophies=!g.s.trophies;changed();return;}
    if(id==ID_TROPHY_NOTIFY){g.s.trophy_notifications=!g.s.trophy_notifications;changed();return;}
    if(id==ID_TROPHY_REFRESH){trophy_view(true);InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(id==ID_TROPHY_RESET){
        auto snapshot=trophy_view(true);if(g.running||!snapshot.catalog||!snapshot.error.empty())return;
        if(!cv_trophies::has_unlocked())MessageBoxW(g.hwnd,trophy_text(tt::ResetNone),trophy_text(tt::Title),MB_OK|MB_ICONINFORMATION);
        else if(MessageBoxW(g.hwnd,trophy_text(tt::ResetConfirm),trophy_text(tt::Title),MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)==IDYES){
            if(!cv_trophies::reset())MessageBoxW(g.hwnd,trophy_text(tt::Error),trophy_text(tt::Title),MB_OK|MB_ICONERROR);
        }
        InvalidateRect(g.hwnd,nullptr,FALSE);return;
    }
    if (g.open_dd >= 0) {
        if (id == ID_DD_UP || id == ID_DD_DOWN) { g.dd_scroll += id == ID_DD_UP ? -1 : 1; InvalidateRect(g.hwnd, NULL, FALSE); return; }
        if (id == ID_DD_LIST) return;
        if (id >= ID_DD_ITEM && id < ID_DD_ITEM + 64) {
            int i = id - ID_DD_ITEM;
            if(g.open_dd>=ID_QUICK_SLOT&&g.open_dd<ID_QUICK_SLOT+4){const auto choices=quick_item_choices();if(i>=0&&i<int(choices.size()))g.s.quick.items[g.open_dd-ID_QUICK_SLOT]=choices[i];}
            else if (g.open_dd == ID_DD_MODE) g.s.mode = i;
            else if(g.open_dd==ID_DD_TROPHY_CORNER)g.s.trophy_corner=i;
            else if(g.open_dd==ID_DD_TROPHY_FILTER)trophy_filter=i;
            else if(g.open_dd==ID_SHADER_COMPILER)g.s.shader_compiler=i!=0;
            else if (g.open_dd == ID_DD_RES) g.s.res = i;
            else if (g.open_dd == ID_DD_AA) g.s.aa = i;
            else if (g.open_dd == ID_DD_FPS && !opt30_comparison()) g.s.fps = i;
            else if(g.open_dd==ID_DD_POSE_DEVICE)g.s.pose_device=i;
            else if(g.open_dd==ID_DD_FRAME_GEN){g.s.frame_generation=i;if(i)g.s.vsync=0;}
            else if(g.open_dd==ID_DD_VSYNC){g.s.vsync=i;if(i&&!g.running)g.s.frame_generation=0;}
            else if (g.open_dd == ID_DD_GAME_TEXT) g.s.game_text_ptbr=i;
            else if(g.open_dd==ID_DD_MANUAL_RELOAD)g.s.manual_reload=i!=0;
            else if(g.open_dd==ID_DD_COMBAT_HUD)g.s.combat_hud=i!=0;
            else if(g.open_dd==ID_DD_SHADOWS)g.s.dynamic_shadows=i!=0;
            else if (g.open_dd == ID_DD_FILM) g.s.film_filter = i!=0;
            else if (g.open_dd == ID_DD_SHARP) g.s.sharpness = i * 10;
            else if(g.open_dd==ID_DD_SRT_ENEMIES)g.s.srt_enemies=i==6?99:i;
            else if(g.open_dd==ID_DD_SRT_CORNER)g.s.srt_corner=i;
            else if(g.open_dd==ID_DD_SRT_LANGUAGE)g.s.srt_language=i;
            else if (g.open_dd == ID_DD_SRT_INVPOS) g.s.srt_inventory_below = i;
            else if (g.open_dd == ID_DD_SRT_WINDOW) g.s.srt_window = i;
            else if (g.open_dd == ID_DD_SRT_BOSSBAR) g.s.srt_boss_bar = i;
            else if (g.open_dd == ID_DD_OV_CORNER) { g.s.overlay_corner = i; set_overlay_env(); }
            else if (g.open_dd == ID_DD_OV_LAYOUT) { g.s.overlay_layout = i; set_overlay_env(); }
            else if (g.open_dd == ID_DD_OV_SIZE) { g.s.overlay_size = i; set_overlay_env(); }
            else if (g.open_dd == ID_DD_OV_BG) { g.s.overlay_text_only = i; set_overlay_env(); }
            else { set_lang_pick(i == 0 ? -1 : k_lang_order[i - 1]); set_note_text_env(); }
            set_srt_env();
            g.open_dd = -1; changed(); return;
        }
        g.open_dd = -1; InvalidateRect(g.hwnd, NULL, FALSE); return;
    }
    if (g.cap != CAP_NONE) { g.cap = CAP_NONE; g.cap_action = -1; }
    if(id==ID_QUICK_EDIT_KEY||id==ID_QUICK_EDIT_BUTTON||id==ID_QUICK_CONFIRM_KEY||id==ID_QUICK_CONFIRM_BUTTON){g.cap=id==ID_QUICK_EDIT_KEY?CAP_QUICK_EDIT_KEY:id==ID_QUICK_EDIT_BUTTON?CAP_QUICK_EDIT_BTN:id==ID_QUICK_CONFIRM_KEY?CAP_QUICK_CONFIRM_KEY:CAP_QUICK_CONFIRM_BTN;g.cap_since=GetTickCount64();SetFocus(g.hwnd);for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);g.pad_prev[i]=p.buttons;}InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(id==ID_QUICK_EDIT_KEY_CLEAR){g.s.quick.edit_key=0;changed();return;}if(id==ID_QUICK_EDIT_BUTTON_CLEAR){g.s.quick.edit_button=-1;changed();return;}
    if(id==ID_QUICK_CONFIRM_KEY_CLEAR){g.s.quick.confirm_key=0;changed();return;}if(id==ID_QUICK_CONFIRM_BUTTON_CLEAR){g.s.quick.confirm_button=-1;changed();return;}
    if(id==ID_SHADER_COMPILER){g.open_dd=id;g.dd_scroll=0;InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(id==ID_QUICK_ON){g.s.quick.on=!g.s.quick.on;changed();return;}
    if(id>=ID_QUICK_SLOT&&id<ID_QUICK_SLOT+4){g.open_dd=id;g.dd_scroll=0;InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(id==ID_QUICK_KEY||id==ID_QUICK_BUTTON||(id>=ID_QUICK_DIR&&id<ID_QUICK_DIR+4)){g.cap=id==ID_QUICK_KEY?CAP_QUICK_KEY:id==ID_QUICK_BUTTON?CAP_QUICK_BTN:CAP_QUICK_DIR;g.cap_slot=id-ID_QUICK_DIR;g.cap_since=GetTickCount64();SetFocus(g.hwnd);for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);g.pad_prev[i]=p.buttons;}InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(id==ID_QUICK_KEY_CLEAR){g.s.quick.key=0;changed();return;}if(id==ID_QUICK_BUTTON_CLEAR){g.s.quick.button=-1;changed();return;}
    if(id>=ID_QUICK_DIR_CLEAR&&id<ID_QUICK_DIR_CLEAR+4){g.s.quick.keys[id-ID_QUICK_DIR_CLEAR]=0;changed();return;}
    if(id==ID_QUICK_RESET){const auto on=g.s.quick.on;g.s.quick={};g.s.quick.on=on;changed();return;}
    if(id==ID_HUD_ENABLED){g.s.combat_hud=!g.s.combat_hud;changed();return;}
    if(id==ID_HUD_RELOAD){g.s.manual_reload=!g.s.manual_reload;changed();return;}
    if(id==ID_HUD_RELOAD_KEY){g.cap=CAP_RELOAD_KEY;g.cap_since=GetTickCount64();SetFocus(g.hwnd);InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(id==ID_HUD_RELOAD_BUTTON){g.cap=CAP_RELOAD_BTN;g.cap_since=GetTickCount64();for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);g.pad_prev[i]=p.buttons;}InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(id==ID_HUD_RELOAD_KEY_CLEAR){g.s.reload_key=0;changed();return;}
    if(id==ID_HUD_RELOAD_BUTTON_CLEAR){g.s.reload_button=-1;changed();return;}
    if(id==ID_HUD_RELOAD_RESET){g.s.reload_key='R';g.s.reload_button=g.s.region_selected==2?PB_L3:PB_EAST;changed();return;}
    if (id >= ID_NAV && id < ID_NAV + P_COUNT) {
        g.page = (Page)(id - ID_NAV); g.cache_msg.clear(); g.log_msg.clear();
        if (g.page == P_PATCHES) scan_patch_dir();   /* the folder may have new files */
        if (g.page == P_MODS) g.tex_rep = -1;        /* count the textures again */
    }
    else if (id >= ID_PATCH && id < ID_PATCH + NPATCHES) { g.s.patch[id - ID_PATCH] ^= true; changed(); }
    else if (id == ID_DD_MANUAL_RELOAD || id == ID_DD_COMBAT_HUD || id == ID_DD_SHADOWS || id == ID_DD_GAME_TEXT || id == ID_DD_TROPHY_CORNER || id == ID_DD_TROPHY_FILTER || id == ID_DD_FILM || id == ID_DD_MODE || id == ID_DD_RES || id == ID_DD_AA || id == ID_DD_FPS || id == ID_DD_POSE_DEVICE || id == ID_DD_FRAME_GEN || id == ID_DD_VSYNC || id == ID_DD_SHARP || id == ID_DD_LANG || id==ID_DD_SRT_ENEMIES || id==ID_DD_SRT_CORNER || id==ID_DD_SRT_LANGUAGE || id == ID_DD_OV_CORNER || id == ID_DD_OV_LAYOUT || id == ID_DD_OV_SIZE || id == ID_DD_OV_BG || id == ID_DD_SRT_INVPOS || id == ID_DD_SRT_WINDOW || id == ID_DD_SRT_BOSSBAR) {
        g.open_dd = id; g.dd_scroll = 0;
        if (id == ID_DD_LANG && g_lang_pick >= 0)   /* open with the chosen language in view */
            for (int i = 0; i < LANG_COUNT; i++)
                if (k_lang_order[i] == g_lang_pick) g.dd_scroll = std::max(0, i + 1 - DD_VISIBLE / 2);
        if(id==ID_DD_SRT_LANGUAGE)g.dd_scroll=std::max(0,g.s.srt_language-DD_VISIBLE/2);
    }
    else if (id == ID_CACHE_CLEAR && !g.running) g.cache_msg = Tf(S_CACHE_CLEARED_FMT, cache_clear());
    else if (id == ID_CACHE_OPEN) { std::error_code ec; fs::create_directories(cache_dir(), ec); open_in_explorer(cache_dir().wstring()); }
    else if(id==ID_TEST_ENABLE){g.s.test_tools=!g.s.test_tools;cv_test_tools_enable(g.s.test_tools);changed();}
    else if(id==ID_TEST_OPEN&&g.running&&g.s.test_tools)cv_test_tools_show(g.hwnd,g.s.game,g_lang,PAD_INPUT_KEYBOARD);
    else if(id==ID_BATTLE){g.s.battle_unlocked=!g.s.battle_unlocked;changed();}
    else if(id==ID_FOCUS_PAUSE){g.s.focus_pause=!g.s.focus_pause;changed();}
    else if(id==ID_UPD_TOGGLE){g.s.check_updates=!g.s.check_updates;changed();}
    else if(id==ID_UPD_CHECK&&!g.running)upd_check(true);
    else if(id==ID_ITEMS_ENABLE){g.s.items_menu=!g.s.items_menu;cv_item_menu_enable(g.s.items_menu);changed();}
    else if(id==ID_SHOT_OPEN){const fs::path d=fs::path(ini_path()).parent_path()/L"screenshots";std::error_code ec;fs::create_directories(d,ec);open_in_explorer(d.wstring());}
    else if (id == ID_GAME_BROWSE||id==2710) {
        auto path=pick(true,g.s.game,nullptr,nullptr);if(!path.empty()){
            const int region=region_index(widen(sfo_field(fs::path(path)/L"PARAM.SFO","TITLE_ID").c_str()));
            bool valid=false;const auto note=check_game(path,&valid);
            if(region<0||!valid){g.play_msg=note;InvalidateRect(g.hwnd,nullptr,FALSE);return;}
            // Detect the chosen folder by TITLE_ID, not by its directory name.
            // The player picks the license themselves; no searching for one.
            std::wstring license=region==2?std::wstring():g.s.region_rap[region];
            g.s.region_selected=region;g.s.region_game[region]=path;g.s.region_rap[region]=license;g.s.game=path;g.s.rap=license;g_version_picked=true;
            changed();
        }
    }
    else if (id == ID_RAP_BROWSE) { auto path=pick(false,g.s.rap,T(S_RAP_FILTER),L"*.rap");if(!path.empty()){g.s.rap=path;g.s.region_rap[g.s.region_selected]=path;changed();} }
    else if (id == ID_SAVE_BROWSE) { auto p = pick(true, save_folder().wstring(), nullptr, nullptr); if (!p.empty()) { g.s.saves = p; changed(); } }
    else if (id == ID_SAVE_DEFAULT) { g.s.saves.clear(); changed(); }
    else if (id == ID_SAVE_OPEN) { std::error_code ec; fs::create_directories(save_folder(), ec); open_in_explorer(save_folder().wstring()); }
    else if (id == ID_GAME_OPEN) open_in_explorer(g.s.game);
    else if (id == ID_RAP_OPEN) open_in_explorer(g.s.rap);
    else if (id == ID_LAYOUT_PS || id == ID_LAYOUT_XB || id == ID_LAYOUT_AUTO || id == ID_LAYOUT_KB || id == ID_LAYOUT_SW) {
        g.s.layout = id == ID_LAYOUT_AUTO ? 2 : id == ID_LAYOUT_KB ? 3 : id == ID_LAYOUT_SW ? 4 : id - ID_LAYOUT_PS;
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
        for (int i = 0; i < PAD_PHYS_MAX; i++) { PadPhys p; pad_phys_poll(i, &p); g.pad_prev[i] = cv_binding_mask(&p); }
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
    else if (id == ID_PATCH_DIR_CLEAR && !g.s.patch_dir.empty()) {
        // Unlink custom patches from settings only. Never delete their files.
        g.s.patch_dir.clear();
        g.s.patch_files_on.clear();
        scan_patch_dir();
        changed();
    }
    else if (id == ID_TEX_BROWSE) {
        auto p = pick(true, tex_folder().wstring(), nullptr, nullptr);
        if (!p.empty() && texture_folders_distinct(cv_texture_paths::replacement_folder(p,texture_game_code()),tex_dump_folder())) { g.s.tex_dir = p; g.tex_rep = -1; set_texture_env(); changed(); }
    }
    else if (id == ID_TEX_OPEN) {
        std::error_code ec;
        const auto root=tex_replacement_folder();
        fs::create_directories(root, ec);
        open_in_explorer(root.wstring());
        g.tex_rep = -1;
    }
    else if (id == ID_TEX_DUMP_BROWSE && !cv_texture_export_status().running) {
        auto p = pick(true, tex_dump_folder().wstring(), nullptr, nullptr);
        if (!p.empty() && texture_folders_distinct(tex_replacement_folder(),p)) { g.s.tex_dump_dir = p; g.tex_rep = -1; set_texture_env(); changed(); }
    }
    else if (id == ID_TEX_DUMP_OPEN) {
        std::error_code ec;
        fs::create_directories(tex_dump_folder(), ec);
        open_in_explorer(tex_dump_folder().wstring());
        g.tex_rep = -1;
    }
    else if (id == ID_TEX_REPLACE) toggle_textures("switch");
    else if (id == ID_TEX_DUMP && !cv_texture_export_status().running) {
        g.s.tex_dump = !g.s.tex_dump;if(g.s.tex_dump)g.s.tex_dump_all=false;set_texture_env();changed();
    }
    else if (id == ID_TEX_ALL && !cv_texture_export_status().running) {
        g.s.tex_dump_all=!g.s.tex_dump_all;if(g.s.tex_dump_all)g.s.tex_dump=false;set_texture_env();changed();
    }
    else if((id==ID_TEX_FORMAT_PNG||id==ID_TEX_FORMAT_DDS)&&!cv_texture_export_status().running){
        g.s.tex_dds=id==ID_TEX_FORMAT_DDS;set_texture_env();g.tex_rep=-1;changed();
    }
    else if (id == ID_TEX_ALL_RUN) {
        if(cv_texture_export_status().running)cv_texture_export_cancel();
        else if(g.s.tex_dump_all){
            g.s.tex_dump=false;set_texture_env();changed();
            const fs::path source=fs::path(g.s.game)/L"USRDIR"/L"BHCV"/L"nativePS3"/L"biocv_disc";
            if(!cv_texture_export_start(source,tex_dump_folder(),g.s.tex_dds))g.play_msg=T(S_TEX_ALL_FAILED);
            else g.play_msg.clear();
        }
    }
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
    else if(id==ID_SRT_FREE){g.s.srt_free=!g.s.srt_free;if(!g.s.srt_free)g.s.srt_separate=false;set_srt_env();changed();}
    else if(id==ID_SRT_SEPARATE){g.s.srt_separate=!g.s.srt_separate;if(g.s.srt_separate){g.s.srt_free=true;g.s.srt_parts[4]=true;}set_srt_env();changed();}
    else if(id==ID_SRT_RESET){g.s.srt_main_x=0;g.s.srt_main_y=0;g.s.srt_inv_x=1000;g.s.srt_inv_y=0;set_env("SRT_RESET_WINDOWS","1");set_srt_env();set_env("SRT_RESET_WINDOWS","");changed();}
    else if(id==ID_SRT_BORDER) {g.s.srt_border=!g.s.srt_border;set_srt_env();changed();}
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
        /* One ready version plays at once; with more, the player picks one first. */
        const int ready=ready_count();
        if(ready==1&&!region_ready(g.s.region_selected))
            for(int i=0;i<3;i++)if(region_ready(i)){g.s.region_selected=i;g.s.game=g.s.region_game[i];g.s.rap=g.s.region_rap[i];break;}
        if(ready>1&&!g_version_picked){g.page=P_JOGO;g.play_msg=lx(design::PICK_VERSION);InvalidateRect(g.hwnd,NULL,FALSE);return;}
        {   /* US/EU need the license the player chose, even if an old decrypted EBOOT is still there */
            const int r=g.s.region_selected;std::error_code ec;
            if(r!=2&&!g.s.region_game[r].empty()&&(g.s.region_rap[r].empty()||!fs::is_regular_file(g.s.region_rap[r],ec))){
                g.page=P_JOGO;g.play_msg=lx(design::RAP_REQUIRED);InvalidateRect(g.hwnd,NULL,FALSE);return;}
        }
        bool ok; check_game(g.s.game, &ok);
        if (!ok) { g.page = P_JOGO; g.play_msg = T(S_SET_FOLDER_FIRST); }
        else if (folder_code() != GAME_CODE) { save_settings(g.s); start_other_version(); }
        else if (!prepare_save_folder()) { g.page = P_SAVES; g.play_msg = T(S_SAVE_ERROR); }
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

int logical_x(int x){RECT c{};GetClientRect(g.hwnd,&c);return int(double(x)*W/std::max(1L,c.right));}
int logical_y(int y){RECT c{};GetClientRect(g.hwnd,&c);return int(double(y)*H/std::max(1L,c.bottom));}
LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_APP+450:{int pins[4];for(int d=0;d<4;d++){pins[d]=int((uint32_t(wp)>>(d*8))&255)-1;if(pins[d]!=-1&&!cv_quick::equipable(pins[d]))return 0;}for(int d=0;d<4;d++)g.s.quick.items[d]=pins[d];save_settings(g.s);InvalidateRect(h,nullptr,FALSE);return 0;}
    case WM_PC_SETTINGS_READ: return g.s.mode | (g.s.res << 4) | (g.s.aa << 8) | ((g.s.brightness + 80) << 10) | ((g.s.contrast - 50) << 18);
    case WM_PC_SETTINGS_READ_SATURATION: return g.s.saturation;
    case WM_PC_SETTINGS_READ_EFFECTS: return g.s.sharpness;
    case WM_PC_SETTINGS_CHANGE:
        if (wp == 0 && lp >= 0 && lp <= 4) g.s.res = int(lp);
        else if (wp == 1 && lp >= 0 && lp <= 2) g.s.aa = int(lp);
        else if (wp == 2 && lp >= 0 && lp <= 2) g.s.mode = int(lp);
        else if (wp == 3 && lp >= 0 && lp <= 160) g.s.brightness = -80 + int(lp);
        else if (wp == 4 && lp >= 0 && lp <= 130) g.s.contrast = 50 + int(lp);
        else if (wp == 5 && lp >= 0 && lp <= 200) g.s.saturation = int(lp);
        else if (wp == 6 && lp >= 0 && lp <= 100 && lp % 10 == 0) g.s.sharpness = int(lp);
        else if (wp == 7) return 0; // Retired anisotropy bridge ID.
        else if(wp==8&&lp>=0&&lp<=100)g.s.music_volume=int(lp);
        else if(wp==9&&lp>=0&&lp<=100)g.s.sfx_volume=int(lp);
        else if(wp==10&&lp>=0&&lp<=1)g.s.battle_unlocked=lp!=0;
        else if(wp==11&&lp>=0&&lp<=1)g.s.film_filter=lp!=0;
        else if(wp==14&&lp>=0&&lp<=1)g.s.manual_reload=lp!=0;
        else if(wp==13&&lp>=0&&lp<=1)g.s.combat_hud=lp!=0;
        else if(wp==12&&lp>=0&&lp<=1)g.s.dynamic_shadows=lp!=0;
        else return 0;
        save_settings(g.s); KillTimer(h, 2); apply_live(false);
        InvalidateRect(h, NULL, FALSE); return 1;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
    case WM_MOUSEMOVE: {
        if(g_upd.notes_drag){upd_notes_drag_to(float(logical_y(GET_Y_LPARAM(lp))));return 0;}
        if(g.srt_layout_drag>=0){move_srt_preview(logical_x(GET_X_LPARAM(lp)),logical_y(GET_Y_LPARAM(lp)));return 0;}
        if(g.srt_drag>=0) {update_srt_slider(g.srt_drag,logical_x(GET_X_LPARAM(lp)));SetCursor(LoadCursor(NULL,IDC_HAND));return 0;}
        if (g.page_drag) { page_drag_to(logical_y(GET_Y_LPARAM(lp))); return 0; }
        int id = hit_at(logical_x(GET_X_LPARAM(lp)), logical_y(GET_Y_LPARAM(lp)));
        if (id != g.hover) { g.hover = id; InvalidateRect(h, NULL, FALSE); }
        SetCursor(LoadCursor(NULL, id >= 0 ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSEWHEEL:
        if (upd_modal()) {
            POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&point);
            if(g_upd.step==UPD_OFFER&&g_upd.notes_box.Contains(float(logical_x(point.x)),float(logical_y(point.y))))
                upd_notes_scroll_to(g_upd.notes_scroll-float(GET_WHEEL_DELTA_WPARAM(wp))*48.f/WHEEL_DELTA);
            return 0;
        }
        if(g.page==P_CONTROLES && g.open_dd<0) {
            POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);
            if(hit_at(logical_x(p.x),logical_y(p.y))==ID_RUMBLE) {
                g.s.rumble=std::clamp(g.s.rumble+(GET_WHEEL_DELTA_WPARAM(wp)>0?5:-5),0,200);set_rumble_env();changed();return 0;
            }
        }
        if(g.page==P_SRT && g.open_dd<0) {
            POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);p.x=logical_x(p.x);p.y=logical_y(p.y);const int id=hit_at(p.x,p.y);
            if(id==ID_SRT_OPACITY) {
                const int step=GET_WHEEL_DELTA_WPARAM(wp)>0?1:-1;
                g.s.srt_opacity=std::clamp(g.s.srt_opacity+step,0,100);set_srt_env();changed();return 0;
            }
            if(id==ID_SRT_SCALE||id==ID_SRT_INV_SCALE) {
                const int step=GET_WHEEL_DELTA_WPARAM(wp)>0?1:-1;
                int& size=id==ID_SRT_SCALE?g.s.srt_scale:g.s.srt_inventory_scale;size=std::clamp(size+step,50,200);set_srt_env();changed();return 0;
            }
        }
        if (g.open_dd >= 0) {
            g.dd_scroll += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
            InvalidateRect(h, NULL, FALSE);
        } else if (g.page == P_PATCHES && g.patch_scroll_max > 0) {
            g.patch_scroll += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -60.0f : 60.0f;
            InvalidateRect(h, NULL, FALSE);
        } else if (g.page_scroll_max[g.page] > 0) {
            page_scroll_to(g.page_scroll[g.page] + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -60.0f : 60.0f));
        }
        return 0;
    case WM_MOUSELEAVE: g.hover = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) return TRUE; break;
    case WM_LBUTTONDOWN: {
        const int id=hit_at(logical_x(GET_X_LPARAM(lp)),logical_y(GET_Y_LPARAM(lp)));
        if(upd_modal()){
            if(id==ID_UPD_NOTES_THUMB){g_upd.notes_drag=true;g_upd.notes_grab=logical_y(GET_Y_LPARAM(lp))-g_upd.notes_thumb.Y;SetCapture(h);return 0;}
            if(id==ID_UPD_NOTES_TRACK){upd_notes_scroll_to(g_upd.notes_scroll+(logical_y(GET_Y_LPARAM(lp))<g_upd.notes_thumb.Y?-1.f:1.f)*g_upd.notes_view.Height);return 0;}
            click(id);return 0;
        }
        if(g.page==P_SRT&&g.open_dd<0&&(id==ID_SRT_MAIN_MOVE||id==ID_SRT_INV_MOVE)){
            g.srt_layout_drag=id==ID_SRT_INV_MOVE?1:0;const auto panel=srt_layout_panel(g.srt_layout_drag);
            g.srt_grab_x=logical_x(GET_X_LPARAM(lp))-panel.X;g.srt_grab_y=logical_y(GET_Y_LPARAM(lp))+g.page_scroll[P_SRT]-panel.Y;
            g.srt_layout_dirty=false;SetCapture(h);return 0;
        }
        if(g.open_dd<0&&((g.page==P_SRT&&(id==ID_SRT_SCALE||id==ID_SRT_OPACITY||id==ID_SRT_INV_SCALE))||(g.page==P_CONTROLES&&id==ID_RUMBLE)||(g.page==P_JOGO&&(id==ID_AUDIO_MUSIC||id==ID_AUDIO_SFX)))) {
            g.srt_drag=id;g.srt_drag_dirty=false;SetCapture(h);
            update_srt_slider(id,logical_x(GET_X_LPARAM(lp)));InvalidateRect(h,NULL,FALSE);return 0;
        }
        if (id == ID_PAGE_THUMB) {
            g.page_drag = true; g.page_grab = logical_y(GET_Y_LPARAM(lp)) - g.page_thumb.Y; SetCapture(h);
            InvalidateRect(h, NULL, FALSE); return 0;
        }
        if (id == ID_PAGE_TRACK) {   /* a page up or down, towards the click */
            const float step = PAGE_BOTTOM - 80;
            page_scroll_to(g.page_scroll[g.page] + (logical_y(GET_Y_LPARAM(lp)) < g.page_thumb.Y ? -step : step));
            return 0;
        }
        click(id);return 0;
    }
    case WM_LBUTTONUP:
        if(g_upd.notes_drag){g_upd.notes_drag=false;ReleaseCapture();InvalidateRect(h,nullptr,FALSE);return 0;}
        if(g.srt_layout_drag>=0){finish_srt_drag();return 0;}
        if (g.page_drag) { g.page_drag = false; ReleaseCapture(); InvalidateRect(h, NULL, FALSE); return 0; }
        if(g.srt_drag>=0) {update_srt_slider(g.srt_drag,logical_x(GET_X_LPARAM(lp)));finish_srt_drag();return 0;}break;
    case WM_CAPTURECHANGED: case WM_CANCELMODE: g_upd.notes_drag=false;g.page_drag = false; finish_srt_drag();return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (upd_modal() && m == WM_KEYDOWN) {   /* Esc: Agora nao / Fechar; Enter: Instalar */
            if(g_upd.step==UPD_OFFER){
                if(wp==VK_HOME){upd_notes_scroll_to(0);return 0;}
                if(wp==VK_END){upd_notes_scroll_to(g_upd.notes_max);return 0;}
                if(wp==VK_NEXT||wp==VK_PRIOR){upd_notes_scroll_to(g_upd.notes_scroll+(wp==VK_NEXT?1.f:-1.f)*(g_upd.notes_view.Height-20));return 0;}
                if(wp==VK_DOWN||wp==VK_UP){upd_notes_scroll_to(g_upd.notes_scroll+(wp==VK_DOWN?24.f:-24.f));return 0;}
            }
            if (wp == VK_ESCAPE) click(g_upd.step == UPD_OFFER ? ID_UPD_LATER : ID_UPD_CLOSE);
            else if (wp == VK_RETURN && g_upd.step == UPD_OFFER) click(ID_UPD_INSTALL);
            return 0;
        }
        if(g.cap==CAP_QUICK_EDIT_KEY||g.cap==CAP_QUICK_CONFIRM_KEY){if(wp==VK_ESCAPE){g.cap=CAP_NONE;InvalidateRect(h,nullptr,FALSE);return 0;}const int vk=vk_from_msg(wp,lp);if(g.cap==CAP_QUICK_EDIT_KEY)g.s.quick.edit_key=vk;else g.s.quick.confirm_key=vk;g.cap=CAP_NONE;changed();return 0;}
        if((g.cap==CAP_QUICK_EDIT_BTN||g.cap==CAP_QUICK_CONFIRM_BTN)&&wp==VK_ESCAPE){g.cap=CAP_NONE;InvalidateRect(h,nullptr,FALSE);return 0;}
        if(g.cap==CAP_QUICK_KEY||g.cap==CAP_QUICK_DIR){if(wp==VK_ESCAPE){g.cap=CAP_NONE;InvalidateRect(h,nullptr,FALSE);return 0;}const int vk=vk_from_msg(wp,lp);if(g.cap==CAP_QUICK_KEY)g.s.quick.key=vk;else if(g.cap_slot>=0&&g.cap_slot<4)g.s.quick.keys[g.cap_slot]=vk;g.cap=CAP_NONE;changed();return 0;}
        if(g.cap==CAP_QUICK_BTN&&wp==VK_ESCAPE){g.cap=CAP_NONE;InvalidateRect(h,nullptr,FALSE);return 0;}
        if(g.cap==CAP_RELOAD_KEY){if(wp==VK_ESCAPE){g.cap=CAP_NONE;InvalidateRect(h,nullptr,FALSE);return 0;}g.s.reload_key=vk_from_msg(wp,lp);g.cap=CAP_NONE;changed();return 0;}
        if(g.cap==CAP_RELOAD_BTN&&wp==VK_ESCAPE){g.cap=CAP_NONE;InvalidateRect(h,nullptr,FALSE);return 0;}
        if (g.cap == CAP_KEY) {
            if (wp == VK_ESCAPE) { g.cap = CAP_NONE; InvalidateRect(h, NULL, FALSE); return 0; }
            int vk = vk_from_msg(wp, lp);
            int* keys = g.s.key[g.cap_action];
            for (int k = 0; k < PAD_KEY_SLOTS; k++) if (keys[k] == vk) keys[k] = 0;   /* once per action */
            keys[g.cap_slot] = vk; g.cap = CAP_NONE; changed(); return 0;
        }
        if (g.cap == CAP_BTN && wp == VK_ESCAPE) { g.cap = CAP_NONE; InvalidateRect(h, NULL, FALSE); return 0; }
        if (wp == VK_ESCAPE && g.open_dd >= 0) { g.open_dd = -1; InvalidateRect(h, NULL, FALSE); return 0; }
        if (g.running && (wp == VK_INSERT || wp == VK_F5 || wp == VK_ESCAPE)) { hide_menu(); return 0; }
        if(!g.running&&wp==VK_RETURN&&g.open_dd<0&&(g.page==P_JOGO||g.page==P_INICIO)){click(ID_PLAY);return 0;}
        if (m == WM_SYSKEYDOWN) break;
        return 0;
    case WM_SRT_POSITIONS:sync_srt_positions();return 0;
    case WM_ACTIVATE:
        if(LOWORD(wp)!=WA_INACTIVE)sync_srt_positions();
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
        if(g_upd.step==UPD_DOWNLOADING){g_upd.prog.cancel=true;return 0;}
        if (g_upd.step == UPD_INSTALLING) return 0;   /* half-copied files would be left behind */
        if (g.running) { ShowWindow(h, SW_HIDE); quit_game(); return 0; }
        break;
    case WM_TIMER: {
        if (wp == 7) { KillTimer(h, 7); pad_phys_rumble_off(); return 0; }   /* rumble_test's pulse ends */
        if (wp == 2) {
            KillTimer(h, 2);
            if (GetForegroundWindow() == h || IsWindowVisible(h)) show_menu();
            return 0;
        }
        if (g.running) {   /* L2+R3+R1, all on the same controller, toggles the menu. */
            uint32_t test_combo=0;int test_input=PAD_KIND_NONE;
            for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};if(pad_phys_poll(i,&p)&&cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::test_chord)){test_combo|=1u<<i;if(!(g.test_combo_prev&(1u<<i)))test_input=p.kind;}}
            if(test_combo&~g.test_combo_prev)PostMessageW(g.hwnd,WM_TEST_MAPS,WPARAM(test_input),0);
            g.test_combo_prev=test_combo;
            uint32_t items_combo=0;   /* R1+R2+L1+L2+triangle: the F6 item menu, as the key (cv_launcher_controls.h) */
            for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};if(pad_phys_poll(i,&p)&&cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::items_chord))items_combo|=1u<<i;}
            if(items_combo&~g.items_combo_prev)PostMessageW(g.hwnd,WM_ITEM_MENU,0,0);
            g.items_combo_prev=items_combo;
            uint32_t combo = 0;
            for (int i = 0; i < PAD_PHYS_MAX; i++) {
                PadPhys p; if (!pad_phys_poll(i, &p)) continue;
                if (cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::menu_chord)) combo |= 1u << i;
            }
            if (combo & ~g.combo_prev) toggle_menu();
            g.combo_prev = combo;
            uint32_t tcombo = 0;   /* R1+R2+L1+L2+square: texture mods on or off */
            for (int i = 0; i < PAD_PHYS_MAX; i++) {
                PadPhys p; if (!pad_phys_poll(i, &p)) continue;
                if (cv_launcher_controls::chord_active(p.buttons, cv_launcher_controls::tex_chord)) tcombo |= 1u << i;
            }
            if (tcombo & ~g.tex_combo_prev) toggle_textures("R1+R2+L1+L2+square");
            g.tex_combo_prev = tcombo;
            uint32_t scombo=0;
            for(int i=0;i<PAD_PHYS_MAX;i++) {PadPhys p;if(!pad_phys_poll(i,&p))continue;
                if(cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::srt_chord))scombo|=1u<<i;}
            if(scombo&~g.srt_combo_prev)toggle_srt();g.srt_combo_prev=scombo;
            uint32_t shot=0;   /* R1+R2+L1+L2+R3: a screenshot, as F10 */
            for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};if(pad_phys_poll(i,&p)&&cv_launcher_controls::chord_active(p.buttons,cv_launcher_controls::shot_chord))shot|=1u<<i;}
            if(shot&~g.shot_combo_prev)rsx_d3d12_request_screenshot();g.shot_combo_prev=shot;
        }
        /* controller status + button capture */
        std::wstring st; int cnt = 0;
        for (int i = 0; i < PAD_PHYS_MAX; i++) {
            PadPhys p; if (!pad_phys_poll(i, &p)) continue;
            if (cnt++ == 0)
                st = (p.kind == PAD_KIND_XINPUT) ? L"Xbox" : (p.kind == PAD_KIND_SWITCH) ? L"Nintendo Switch"
                   : (p.kind == PAD_KIND_DS4) ? L"DualShock 4" : L"DualSense";
        }
        int kind = 0;
        for (int i = 0; i < PAD_PHYS_MAX && !kind; i++) { PadPhys p; if (pad_phys_poll(i, &p)) kind = p.kind; }
        if (const char* pv = getenv("CV_PAD_PREVIEW")) {   /* show a model without that controller (1..4) */
            kind = atoi(pv); cnt = kind ? 1 : 0;
            st = kind == PAD_KIND_XINPUT ? L"Xbox" : kind == PAD_KIND_DS4 ? L"DualShock 4" : kind == PAD_KIND_SWITCH ? L"Nintendo Switch"
               : kind ? L"DualSense" : L"";
        }
        bool redraw = st != g.pad_status || cnt != g.pad_count || kind != g.pad_kind;
        if(g.page==P_MODS && IsWindowVisible(h) && g.s.tex_dump) {
            static ULONGLONG last_dump_scan=0;const auto now=GetTickCount64();
            if(now-last_dump_scan>=1000) {
                last_dump_scan=now;const int count=count_png(tex_dump_folder(),false);
                if(count!=g.tex_dumped){g.tex_dumped=count;redraw=true;}
            }
        }
        {
            static uint64_t previous=~uint64_t(0);static bool was_exporting=false;
            const auto e=cv_texture_export_status();
            const uint64_t key=uint64_t(e.files)<<32|uint64_t(e.written)<<4|uint64_t(e.running)<<3|uint64_t(e.finished)<<2|uint64_t(e.failed)<<1|e.canceled;
            if(key!=previous){previous=key;if(g.page==P_MODS)redraw=true;}
            if(was_exporting&&!e.running){g.tex_rep=-1;if(g.page==P_MODS)redraw=true;}
            was_exporting=e.running;
        }
        s_shortcut_xbox=kind==PAD_KIND_XINPUT;
        g.pad_status = st; g.pad_count = cnt; g.pad_kind = kind;
        if((g.cap==CAP_QUICK_EDIT_KEY||g.cap==CAP_QUICK_CONFIRM_KEY)&&GetTickCount64()-g.cap_since>10000){g.cap=CAP_NONE;redraw=true;}
        if(g.cap==CAP_QUICK_EDIT_BTN||g.cap==CAP_QUICK_CONFIRM_BTN){for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);uint32_t pressed=p.buttons&~g.pad_prev[i];g.pad_prev[i]=p.buttons;for(int b=0;b<PB_COUNT;b++)if(pressed&(1u<<b)){if(g.cap==CAP_QUICK_EDIT_BTN)g.s.quick.edit_button=b;else g.s.quick.confirm_button=b;g.cap=CAP_NONE;changed();return 0;}}if(GetTickCount64()-g.cap_since>10000){g.cap=CAP_NONE;redraw=true;}}
        if((g.cap==CAP_QUICK_KEY||g.cap==CAP_QUICK_DIR)&&GetTickCount64()-g.cap_since>10000){g.cap=CAP_NONE;redraw=true;}
        if(g.cap==CAP_QUICK_BTN){for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);uint32_t pressed=p.buttons&~g.pad_prev[i];g.pad_prev[i]=p.buttons;for(int b=0;b<PB_COUNT;b++)if(pressed&(1u<<b)){g.s.quick.button=b;g.cap=CAP_NONE;changed();return 0;}}if(GetTickCount64()-g.cap_since>10000){g.cap=CAP_NONE;redraw=true;}}
        if(g.cap==CAP_RELOAD_KEY&&GetTickCount64()-g.cap_since>10000){g.cap=CAP_NONE;redraw=true;}
        if(g.cap==CAP_RELOAD_BTN){for(int i=0;i<PAD_PHYS_MAX;i++){PadPhys p{};pad_phys_poll(i,&p);uint32_t pressed=p.buttons&~g.pad_prev[i];g.pad_prev[i]=p.buttons;for(int b=0;b<PB_COUNT;b++)if(pressed&(1u<<b)){g.s.reload_button=b;g.cap=CAP_NONE;changed();return 0;}}if(GetTickCount64()-g.cap_since>10000){g.cap=CAP_NONE;redraw=true;}}
        if (g.cap == CAP_BTN) {
            for (int i = 0; i < PAD_PHYS_MAX; i++) {
                PadPhys p; pad_phys_poll(i, &p);
                uint32_t mask=cv_binding_mask(&p),pressed = mask & ~g.pad_prev[i];
                g.pad_prev[i] = mask;
                for (int b = 0; b < CV_BINDING_COUNT && pressed; b++)
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
    case WM_DESTROY: cv_texture_export_cancel(); PostQuitMessage(0); return 0;
    }
    if (m == WM_TOGGLE_MENU) { toggle_menu(); return 0; }
    if (m == WM_TOGGLE_TEXTURES) { toggle_textures("F9"); return 0; }
    if(m==WM_TOGGLE_SRT) {toggle_srt();return 0;}
    if(m==WM_UPDATE) {upd_message(wp,lp);return 0;}
    if(m==WM_TEST_MAPS) {if(g.running)cv_test_tools_show(g.hwnd,g.s.game,g_lang,int(wp));return 0;}
    if(m==WM_ITEM_MENU) {if(g.running&&g.s.items_menu)cv_item_menu_toggle(g.s.game,g_lang);return 0;}   /* off: F6 does nothing */
    return DefWindowProcW(h, m, wp, lp);
}

void set_env(const char* k, const std::string& v) { _putenv_s(k, v.c_str()); }

/* In-game icons (button_icons.cpp): auto follows the first controller. */
void set_icon_env()
{
    const char* v = g.s.layout == 4 ? "switch" : g.s.layout == 3 ? "keyboard" : g.s.layout == 2 ? "auto" : g.s.layout == 1 ? "xbox" : "ps";
    set_env("CV_BUTTON_ICONS", v);
    /* The controls follow the icons, so the 360 manual shown is what the pad
     * does: Xbox icons, 360 layout (on any controller); auto, Xbox pads only.
     * Switch icons go with the PlayStation layout (ZL/ZR are L2/R2). */
    set_env("PAD_360_LAYOUT", g.s.layout == 2 || g.s.layout == 3 ? "auto" : g.s.layout == 1 ? "on" : "off");
}
void set_env_default(const char* k, const char* v) { if (!getenv(k)) _putenv_s(k, v); }

/* The shader loading note's words, in the launcher's language. The game
 * reads them as it draws, so a language picked while it runs shows too. */
void set_note_text_env()
{
    set_env("SHADER_NOTE_TEXT", save_utf8(fs::path(std::wstring(T(S_SH_NOTE_LOADING)) + L"|" + T(S_SH_NOTE_LOADED))));
    set_env("SCREENSHOT_TEXT", save_utf8(fs::path(std::wstring(T(S_SHOT_SAVED)) + L"|" + T(S_SHOT_FAILED))));
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
    set_env("SRT_FREE",g.s.srt_free?"1":"0");set_env("SRT_SEPARATE",g.s.srt_separate?"1":"0");
    set_env("SRT_INVENTORY_SCALE",std::to_string(g.s.srt_inventory_scale));
    set_env("SRT_MAIN_X",std::to_string(g.s.srt_main_x));set_env("SRT_MAIN_Y",std::to_string(g.s.srt_main_y));
    set_env("SRT_INV_X",std::to_string(g.s.srt_inv_x));set_env("SRT_INV_Y",std::to_string(g.s.srt_inv_y));
    set_env("SRT_BORDER",g.s.srt_border?"1":"0");
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
    if(g.s.tex_dir.empty()){
        std::error_code ec;fs::create_directories(tex_folder()/texture_game_code(),ec);
    }
    set_env("TEXTURE_DIR", save_utf8(tex_folder()));
    set_env("TEXTURE_GAME", save_utf8(fs::path(texture_game_code())));
    set_env("TEXTURE_DUMP_DIR", save_utf8(tex_dump_folder()));
    texpack_set_dump_directory(tex_dump_folder().c_str());
    texpack_set_dump_format(g.s.tex_dds);
    // Built-in game localization is independent of optional user texture mods.
    const auto translation=texture_executable_directory().parent_path()/L"assets"/L"ptbr-textures"/GAME_CODE;
    const bool translated=g.s.game_text_ptbr && std::wcscmp(GAME_CODE,L"NPJB00135")!=0;
    set_env("CV_PTBR_TEXTURE_DIR",translated?save_utf8(translation):"");
    set_env("TEXTURE_REPLACE", g.s.tex_replace ? "1" : "0");
    set_env("TEXTURE_DUMP", g.s.tex_dump && !g.s.tex_dump_all ? "1" : "0");
    texpack_flags_changed();
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
    const char* scales[] = { "0.75", "1", "1.35" };
    set_env("PERF_OVERLAY_SCALE", scales[g.s.overlay_size]);
    set_env("PERF_OVERLAY_BG", g.s.overlay_text_only ? "0" : "1");
}

/* PAD_BTNMAP / PAD_KEYMAP from the settings: read by the pad at start, and by
 * the button icons whenever they change (the keys drawn in the game). */
void set_map_env()
{
    std::string bm, km;
    for (int a = 0; a < PA_COUNT; a++) {
        bm += std::string(k_pad_action_id[a]) + "=";
        km += std::string(k_pad_action_id[a]) + "=";
        for (int k = 0; k < PAD_BTN_SLOTS; k++) bm += std::string(k ? "+" : "") + cv_binding_id(g.s.btn[a][k]);
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
    /* The comparison build is deployed only to JP. Do not silently hand off
     * to a sibling executable that does not implement the comparison policy. */
    if(opt30_comparison()) { g.page=P_JOGO; g.play_msg=T(S_EBOOT_MISMATCH); return; }
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
    if(s.game_text_ptbr && std::wcscmp(GAME_CODE,L"NPJB00135")==0) {
        MessageBoxW(g.hwnd,ptbr_ui(3),ptbr_ui(0),MB_OK|MB_ICONINFORMATION);return;
    }
    wchar_t text_exe[MAX_PATH]={};GetModuleFileNameW(nullptr,text_exe,MAX_PATH);
    const fs::path text_root=fs::path(text_exe).parent_path().parent_path()/L"assets"/GAME_CODE;
    if(s.game_text_ptbr && !fs::is_regular_file(text_root/L"rm_0000.arc") && !fs::is_regular_file(text_root/L"rdx_lnk/rm_0000.arc.cvp")) {
        MessageBoxW(g.hwnd,L"PT-BR translation files missing: rdx_lnk/rm_0000.arc.cvp",ptbr_ui(0),MB_OK|MB_ICONERROR);return;
    }
    set_env("CV_PTBR_ASSET_ROOT",s.game_text_ptbr?narrow(text_root.wstring()):"");
    /* Match the tested normal CMD when opening the executable directly.
     * An explicit compiler override remains available to diagnostic launchers. */
    set_env("SHADER_COMPILER",s.shader_compiler?"fxc":"dxc");
    std::fprintf(stderr,"[SUPPORT-SHADERS] requested=%s; change requires restart; separate compiler caches\n",s.shader_compiler?"fxc":"dxc");
    /* SPU task threads are reused instead of one CreateThread per task
     * (~2000 tasks/s in the heavy JP room): ~50 -> ~53 real FPS uncapped.
     * An explicit SPU_TASK_POOL=0 still selects the old detached threads. */
    const char* pool_env=std::getenv("SPU_TASK_POOL");
    if(!pool_env||!*pool_env)set_env("SPU_TASK_POOL","1");
    // Preserve the normal runner's port-root cache even when launched by an
    // editor, shortcut, or Explorer with a different working directory.
    SetCurrentDirectoryW(port_directory().c_str());
    set_env("PS3_VFS_ROOT", narrow(s.game));
    set_env("CV_TROPHIES",s.trophies?"1":"0");
    set_env("CV_TROPHY_NOTIFICATIONS",s.trophy_notifications?"1":"0");
    set_env("CV_TROPHY_CORNER",std::to_string(s.trophy_corner));
    cv_trophy_popup::corner(s.trophy_corner);
    set_env("CV_TROPHY_LANGUAGE",std::to_string(g_lang));
    set_env("PS3_SAVE_ROOT", s.saves.empty() ? "" : save_utf8(save_folder()));
    const auto rap = rap_for_game(s, GAME_CODE);
    if (!rap.empty()) set_env("PS3_RAP_DIR", narrow(fs::path(rap).parent_path().wstring()));
    if (!s.fw.empty()) set_env("PS3_DEV_FLASH", narrow(s.fw));
    set_env("RES_SCALE", k_res[s.res].scale);
    set_env("WINDOW_MODE", k_mode_env[s.mode]);
    set_env("AA_MODE", std::to_string(s.aa));
    set_env("RSX_ANISO", "0");
    /* Opt-in 60 FPS: preserve native 30 Hz gameplay, blend render poses.
     * The accelerated RAW benchmark remains a separate diagnostic shortcut. */
    const char* diagnostic_env=std::getenv("CV_FPS_DIAGNOSTIC");
    const bool fps_diagnostic=diagnostic_env&&std::strcmp(diagnostic_env,"1")==0;
    /* Uncapped benchmark (its own shortcut only): vblank at 1000 Hz and no
     * frame generation, so the counter shows the real ceiling. */
    const char* unlocked_env=std::getenv("CV_FPS_UNLOCKED");
    const bool fps_unlocked=fps_diagnostic&&unlocked_env&&std::strcmp(unlocked_env,"1")==0;
    if(opt30_comparison() || (!fps_diagnostic && s.fps==0)) {
        set_env("CV_FPS_TEST_60", "");set_env("CV_FPS_BENCHMARK_RAW", "");
        set_env("CV_RENDER_INTERPOLATION", "");set_env("VBLANK_HZ", "60");set_env("CV_POSE_COMPUTE","");set_env("CV_FSR_FRAME_GENERATION","");
    } else if (s.fps == 1 || fps_diagnostic) {
        set_env("CV_FPS_TEST_60", "1");
        if(!fps_diagnostic)set_env("CV_FPS_BENCHMARK_RAW", "");
        if(!fps_diagnostic)set_env("CV_RENDER_INTERPOLATION", "1");
        set_env("VBLANK_HZ", fps_unlocked?"1000":"60");
        if(!fps_diagnostic){set_env("CV_POSE_CPU_BLEND","1");set_env("CV_POSE_CPU","");set_env("CV_POSE_COMPUTE",s.pose_device?"1":"");}
    }
    if(!opt30_comparison()&&(s.fps==1||fps_diagnostic)){
        /* Existing CPU/GPU benchmarks remain comparable after saving FSR on. */
        set_env("CV_FSR_FRAME_GENERATION",!fps_unlocked&&s.frame_generation&&frame_generation_allowed()?"1":"");
    }
    set_env("VSYNC",!opt30_comparison()&&!fps_diagnostic&&s.fps==1&&s.vsync&&!(s.frame_generation&&frame_generation_allowed())?"1":"");
    if(!fps_diagnostic) {
        for(const char* name:{"CV_POSE_PROFILE","CV_POSE_SEARCH_PROFILE","CV_FRAME_PROFILE",
                             "CV_FRAME_DETAIL","CV_PRODUCER_PROFILE"})set_env(name,"");
    }
    set_env("CV_BRIGHTNESS", std::to_string(s.brightness));
    set_env("CV_CONTRAST", std::to_string(s.contrast));
    set_env("CV_SATURATION", std::to_string(s.saturation));
    rsx_d3d12_set_color_adjust(s.brightness, s.contrast);
    rsx_d3d12_set_saturation(s.saturation);
    set_env("CV_SHARPNESS", std::to_string(s.sharpness));
    rsx_d3d12_set_sharpness(s.sharpness);
    set_icon_env();
    set_rumble_env();
    std::string pl;
    for (int i = 0; i < NPATCHES; i++)
        if (s.patch[i]) pl += (pl.empty() ? "" : ",") + std::string(k_patches[i].id);
    set_env("PS3_PATCHES", pl);
    cv_cheats_set(s.patch[2],s.patch[3]);
    cv_item_menu_enable(s.items_menu);
    cv_focus_pause_enable(s.focus_pause);   /* also when nothing changed before Jogar */
    cv_focus_pause_label(T(S_PAUSED));
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
    g.page = P_JOGO;   /* the first tab */
    InvalidateRect(g.hwnd, NULL, FALSE);
    SetEvent(s_play_ev);
}

DWORD WINAPI launcher_thread(void*)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi; ULONG_PTR gtok;
    GdiplusStartup(&gtok, &gsi, NULL);
    g.s = load_settings();
    cv_enhanced_set(8,g.s.music_volume);cv_enhanced_set(9,g.s.sfx_volume);cv_enhanced_set(10,g.s.battle_unlocked);cv_enhanced_set(11,g.s.film_filter);cv_enhanced_set(12,g.s.dynamic_shadows);cv_enhanced_set(13,g.s.combat_hud);cv_enhanced_set(14,g.s.manual_reload);cv_manual_reload_bindings_set(g.s.reload_key,g.s.reload_button);cv_quick::configure(g.s.quick);
    init_social_links();
    for(int i=0;i<2;i++)g.social_urls[i]=social_url(i);
    cv_test_tools_enable(g.s.test_tools);
    cv_item_menu_enable(g.s.items_menu);   /* the saved choice, before any change */
    set_icon_env();   /* the button capture below reads the layout it implies */
    set_rumble_env();
    g.banner = load_png("BANNER");
    g.castle=load_png("LAUNCHER_CASTLE");
    if(Bitmap* full=load_png("LAUNCHER_LOGO")){
        // Remove only the exported transparent padding in the UI's bitmap;
        // the approved source PNG is kept byte-for-byte in launcher_art.
        if(full->GetWidth()==1630&&full->GetHeight()==965){
            g.brand_logo=full->Clone(Rect(40,148,1570,774),PixelFormat32bppPARGB);
            if(g.brand_logo&&g.brand_logo->GetLastStatus()==Ok){delete full;full=nullptr;}
            else{delete g.brand_logo;g.brand_logo=nullptr;}
        }
        if(full)g.brand_logo=full;
    }

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"cv_launcher";
    wc.hIcon = LoadIcon(GetModuleHandleW(NULL), MAKEINTRESOURCE(1));
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int frame_w=ww-W,frame_h=wh-H;
    const double scale=std::min({1.0,double(wa.right-wa.left-frame_w-24)/W,double(wa.bottom-wa.top-frame_h-24)/H});
    ww=int(W*scale)+frame_w;wh=int(H*scale)+frame_h;
    g.hwnd = CreateWindowExW(0, L"cv_launcher", L"Resident Evil Code: Veronica X (PC)", style,
                             wa.left + ((wa.right - wa.left) - ww) / 2, wa.top + ((wa.bottom - wa.top) - wh) / 2,
                             ww, wh, NULL, NULL, wc.hInstance, NULL);
    cv_srt_set_position_notify([](){if(g.hwnd)PostMessageW(g.hwnd,WM_SRT_POSITIONS,0,0);});
    BOOL dark = TRUE; DwmSetWindowAttribute(g.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    ShowWindow(g.hwnd, SW_SHOW);
    SetForegroundWindow(g.hwnd);
    SetTimer(g.hwnd, 1, 30, NULL);
    {   /* Updates: delete what an install replaced, say it happened, look for a newer release. */
        wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL, self, MAX_PATH);
        fs::path dir = fs::path(self).parent_path();
#ifdef CV_DISTRIBUTION
        if(dir.filename()==L"bin" && fs::is_regular_file(dir.parent_path()/L"Code Veronica.exe"))
            dir=dir.parent_path();
#endif
        if (const char* d = getenv("CV_UPDATE_DONE"); d && *d)
            g.play_msg = g_upd.status = upd_fill(ux(upd_text::UPDATED), k_launcher_version);
        std::thread([dir] { cv_update::cleanup(dir); }).detach();
        const char* autoplay = getenv("CV_LAUNCHER_AUTOPLAY");
        const char* off = getenv("CV_UPDATE_DISABLE");
        if (g.s.check_updates && !(autoplay && *autoplay) && !(off && *off == '1')) upd_check(false);
    }
    /* Started by the other version's launcher for this version's folder:
     * play at once with the settings it saved. Once only. */
    if (const char* a = getenv("CV_LAUNCHER_AUTOPLAY"); a && *a) {
        _putenv_s("CV_LAUNCHER_AUTOPLAY", "");
        g_version_picked = true;   /* the other launcher already chose this version */
        click(ID_PLAY);
    }
    /* CV_UPDATE_PREVIEW=download|install: show the update card, for screenshots. */
    if (const char* u = getenv("CV_UPDATE_PREVIEW"); u && *u) {
        g_upd.step = strcmp(u, "install") == 0 ? UPD_INSTALLING : UPD_DOWNLOADING;
        g_upd.prog.total = 48u << 20; g_upd.prog.done = 21u << 20;
    }
    /* CV_LAUNCHER_PAGE=<n>: open on page n (Page order), for screenshots. */
    if (const char* p = getenv("CV_LAUNCHER_PAGE"); p && atoi(p) > 0 && atoi(p) < P_COUNT)
        click(ID_NAV + atoi(p));
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { if(cv_test_tools_message(&msg))continue;TranslateMessage(&msg); DispatchMessageW(&msg); }
    cv_test_tools_close();
    cv_item_menu_close();
    /* closed before Jogar: the main thread sees this thread end */
    delete g.banner; g.banner = nullptr;
    image_fit_forget();
    delete g.brand_logo;g.brand_logo=nullptr;
    delete g.castle;g.castle=nullptr;for(int i=0;i<3;i++){delete g.cover[i];delete g.cover_bg[i];g.cover[i]=g.cover_bg[i]=nullptr;}
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
// Effects are separate from the original packed display/color settings.
// Retired anisotropy bits are never returned, including without a launcher window.
static int cv_pc_fallback_effects = -1;
extern "C" int cv_pc_settings_effects_read(void)
{
    if (g.hwnd && IsWindow(g.hwnd)) return int(SendMessageW(g.hwnd, WM_PC_SETTINGS_READ_EFFECTS, 0, 0));
    if (cv_pc_fallback_effects < 0) {
        const char* sharp=getenv("CV_SHARPNESS");
        const int sharpness=std::clamp(sharp?atoi(sharp):_wtoi(ini_get(L"tela",L"nitidez",L"0").c_str()),0,100)/10*10;
        cv_pc_fallback_effects=sharpness;
    }
    return cv_pc_fallback_effects;
}
extern "C" void cv_quick_pins_save(uint32_t packed){
 if(g.hwnd&&PostMessageW(g.hwnd,WM_APP+450,WPARAM(packed),0))return;
 for(int d=0;d<4;d++){auto key=L"atalhos_item_"+std::to_wstring(d);const int id=int((packed>>(d*8))&255)-1;ini_set(L"jogo",key.c_str(),std::to_wstring(id));}
}
extern "C" void cv_pc_settings_quick_read(void){auto settings=load_settings();cv_quick::configure(settings.quick);}
extern "C" void cv_pc_settings_reload_bindings_read(void){
 const int key=std::clamp(_wtoi(ini_get(L"jogo",L"recarga_tecla",L"82").c_str()),0,255);
#ifdef CV_REGION_JP
 const wchar_t* fallback=L"8";
#else
 const wchar_t* fallback=L"1";
#endif
 const int button=std::clamp(_wtoi(ini_get(L"jogo",L"recarga_botao",fallback).c_str()),-1,PB_COUNT-1);cv_manual_reload_bindings_set(key,button);
}
extern "C" int cv_pc_settings_reload_read(void){return _wtoi(ini_get(L"jogo",L"recarga_manual",L"0").c_str())!=0;}
extern "C" int cv_pc_settings_hud_read(void){return _wtoi(ini_get(L"jogo",L"hud_combate",L"0").c_str())!=0;}
extern "C" int cv_pc_settings_shadows_read(void){
 if(g.hwnd&&IsWindow(g.hwnd))return cv_enhanced_get(12);
 return _wtoi(ini_get(L"tela",L"sombras_dinamicas",L"1").c_str())!=0;
}
extern "C" int cv_pc_settings_film_read(void){
 if(g.hwnd&&IsWindow(g.hwnd))return cv_enhanced_get(11);
 const char* override_value=getenv("CV_FILM_FILTER");
 return override_value&&*override_value?atoi(override_value)!=0:_wtoi(ini_get(L"tela",L"filtro_filme",L"1").c_str())!=0;
}
extern "C" void cv_pc_settings_change(int which,int value)
{
    static const int maximum[]={4,2,2,160,130,200,100,4,100,100,1,1,1,1,1};
    if(which==7)return; // Prevent stale callers from re-enabling retired anisotropy.
    if (which<0 || which>=15 || value<0 || value>maximum[which] || (which==6 && value%10!=0)) return;
    if (g.hwnd && IsWindow(g.hwnd)) { SendMessageW(g.hwnd,WM_PC_SETTINGS_CHANGE,WPARAM(which),LPARAM(value)); return; }
    if(which==14){cv_enhanced_set(14,value);ini_set(L"jogo",L"recarga_manual",std::to_wstring(value));return;}
    if(which==13){cv_enhanced_set(13,value);ini_set(L"jogo",L"hud_combate",std::to_wstring(value));return;}
    if(which==12){cv_enhanced_set(12,value);ini_set(L"tela",L"sombras_dinamicas",std::to_wstring(value));return;}
    if(which==11){cv_enhanced_set(11,value);ini_set(L"tela",L"filtro_filme",std::to_wstring(value));return;}
    if(which>=8){cv_enhanced_set(which,value);ini_set(which==10?L"extras":L"audio",which==8?L"musica":which==9?L"efeitos":L"battle_game",std::to_wstring(value));return;}
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
    } else if(which==5) {
        ini_set(L"tela",L"saturacao",std::to_wstring(value));
        cv_pc_fallback_saturation=value;
        rsx_d3d12_set_saturation(value);
    } else if(which==6) {
        cv_pc_fallback_effects=(cv_pc_settings_effects_read()&~127)|value;
        ini_set(L"tela",L"nitidez",std::to_wstring(value));rsx_d3d12_set_sharpness(value);
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
