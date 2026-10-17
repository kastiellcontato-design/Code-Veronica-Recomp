/* cv_version.h -- the guest addresses this port's own code relies on, for each
 * version of the game it is built against (EU, JP and US).
 *
 * The port is built once per version: the lift (src/recomp, src/recomp_jp)
 * is that version's own EBOOT, and CMake defines CV_REGION_JP for the
 * Japanese one (build-release-jp.cmd). Everything else -- the runtime, the
 * video backend, the launcher -- is the same code.
 *
 * The Japanese EBOOT (NPJB00135, BIOHAZARD CODE: Veronica Kanzenban) has the
 * same 17,045 functions in the same order as the European one (NPEB00553);
 * 17,006 are the same size. Each address below was found in the Japanese
 * EBOOT by matching the code around the European one with branch offsets and
 * immediates masked, and checked by comparing the functions word by word:
 * the hooked functions differ only in the address constants they load.
 * The data block they use moved by -0x10280 (0xBBE8F8 -> 0xBAE678,
 * 0xBC3370 -> 0xBB30F0, 0xAD33DC -> 0xAC315C).
 *
 * US NPUB30467: independently lifted, TOC 0xA7AE70, menu/title data
 * moved by -0x10300 from EU. Hook instructions were matched and checked
 * against the supplied SHA-1 93d060e5019d877df39cd091c28efff920b98e49.
 */
#pragma once

#if defined(CV_REGION_JP)
#define CV_TITLE_ID        "NPJB00135"
/* SRT: read-only guest state. Source of address facts: Kapdap (MIT). */
#define CV_SRT_TIME          0x00BB3E38u
#define CV_SRT_ROOM          0x00BB3E4Cu
#define CV_SRT_STATUS        0x00BDE709u
#define CV_SRT_HEALTH        0x00BDEA9Cu
#define CV_SRT_CHARACTER     0x00BB361Cu
#define CV_SRT_INVENTORY     0x00BB3808u
#define CV_SRT_ENEMIES       0x00BDEBF8u
#define CV_SRT_ENEMY_COUNT   0x00BDE318u
#define CV_SRT_DIFFICULTY    0x00BB3624u
#define CV_SRT_SAVES         0x00BB3618u
#define CV_SRT_RETRY         0x00BB3E42u
#define CV_SRT_FAS           0x00BB3E40u
#define CV_SRT_RDX           0x00BDE16Cu

#define CV_TOC             0x00A7AE70u
/* OPTION menu (native_pc_menu.cpp) */
#define CV_MENU_CURSOR     0x00BAE690u
#define CV_MENU_INPUT      0x00BB30F0u
#define CV_MENU_REPEAT     0x00BB30F4u
#define CV_MENU_DIALOG     0x00AC315Cu
#define CV_MENU_PULSE      0x00BAE6ACu
#define CV_MENU_ROW_TEXT   0x00BAEB98u   /* the menu draw's row buffer: EU 0xBBED98, JP 0xBAEB18 */
#define CV_FN_MENU_RESET   func_0002A038
#define CV_FN_MENU_INPUT   func_0002EF38
#define CV_FN_MENU_COLOR   func_001BB688
#define CV_FN_MENU_SOUND   func_00195928
#define CV_FN_MENU_FRAME   func_0002B0B0
#define CV_OG_MENU_RESET   cv_original_0002A038
#define CV_OG_MENU_INPUT   cv_original_0002EF38
#define CV_OG_MENU_COLOR   cv_original_001BB688
/* opening logos and intro movie (boot_skip.cpp) */
#define CV_TITLE_STATE     0x00BAE67Cu
#define CV_FN_TITLE        func_0002AEC4
#define CV_FN_LOGO         func_001FF7C0
#define CV_OG_TITLE        cv_original_0002AEC4
#define CV_OG_LOGO         cv_original_001FF7C0
/* Remove Door Animations (patches.json has the same site) */
#define CV_DOOR_SKIP_SITE  0x001AE304u
/* The guest libc memset and memmove, done natively (cv_native_libc.cpp). */
#define CV_FN_MEMSET  func_007B5568
#define CV_OG_MEMSET  cv_original_007B5568
#define CV_FN_MEMMOVE func_007AD0E0
#define CV_OG_MEMMOVE cv_original_007AD0E0
#elif defined(CV_REGION_US)
#define CV_TITLE_ID        "NPUB30467"
/* SRT: read-only guest state. Source of address facts: Kapdap (MIT). */
#define CV_SRT_TIME          0x00BB3DB8u
#define CV_SRT_ROOM          0x00BB3DCCu
#define CV_SRT_STATUS        0x00BDE689u
#define CV_SRT_HEALTH        0x00BDEA1Cu
#define CV_SRT_CHARACTER     0x00BB359Cu
#define CV_SRT_INVENTORY     0x00BB3788u
#define CV_SRT_ENEMIES       0x00BDEB78u
#define CV_SRT_ENEMY_COUNT   0x00BDE298u
#define CV_SRT_DIFFICULTY    0x00BB35A4u
#define CV_SRT_SAVES         0x00BB3598u
#define CV_SRT_RETRY         0x00BB3DC2u
#define CV_SRT_FAS           0x00BB3DC0u
#define CV_SRT_RDX           0x00BDE0ECu

#define CV_TOC             0x00A7AE70u
#define CV_MENU_CURSOR     0x00BAE610u
#define CV_MENU_INPUT      0x00BB3070u
#define CV_MENU_REPEAT     0x00BB3074u
#define CV_MENU_DIALOG     0x00AC30DCu
#define CV_MENU_PULSE      0x00BAE62Cu
#define CV_MENU_ROW_TEXT   0x00BAEB18u
#define CV_FN_MENU_RESET   func_0002A038
#define CV_FN_MENU_INPUT   func_0002EF60
#define CV_FN_MENU_COLOR   func_001BB5E0
#define CV_FN_MENU_SOUND   func_00195968
#define CV_FN_MENU_FRAME   func_0002B0D8
#define CV_OG_MENU_RESET   cv_original_0002A038
#define CV_OG_MENU_INPUT   cv_original_0002EF60
#define CV_OG_MENU_COLOR   cv_original_001BB5E0
#define CV_TITLE_STATE     0x00BAE5FCu
#define CV_FN_TITLE        func_0002AEEC
#define CV_FN_LOGO         func_001FF46C
#define CV_OG_TITLE        cv_original_0002AEEC
#define CV_OG_LOGO         cv_original_001FF46C
#define CV_DOOR_SKIP_SITE  0x001AE25Cu
/* The guest libc memset and memmove, done natively (cv_native_libc.cpp). */
#define CV_FN_MEMSET  func_007B4C78
#define CV_OG_MEMSET  cv_original_007B4C78
#define CV_FN_MEMMOVE func_007AC7F0
#define CV_OG_MEMMOVE cv_original_007AC7F0
#else
#define CV_TITLE_ID        "NPEB00553"
/* SRT: read-only guest state. Source of address facts: Kapdap (MIT). */
#define CV_SRT_TIME          0x00BC40B8u
#define CV_SRT_ROOM          0x00BC40CCu
#define CV_SRT_STATUS        0x00BEE989u
#define CV_SRT_HEALTH        0x00BEED1Cu
#define CV_SRT_CHARACTER     0x00BC389Cu
#define CV_SRT_INVENTORY     0x00BC3A88u
#define CV_SRT_ENEMIES       0x00BEEE78u
#define CV_SRT_ENEMY_COUNT   0x00BEE598u
#define CV_SRT_DIFFICULTY    0x00BC38A4u
#define CV_SRT_SAVES         0x00BC3898u
#define CV_SRT_RETRY         0x00BC40C2u
#define CV_SRT_FAS           0x00BC40C0u
#define CV_SRT_RDX           0x00BEE3ECu

#define CV_TOC             0x00A8AE70u
#define CV_MENU_CURSOR     0x00BBE910u
#define CV_MENU_INPUT      0x00BC3370u
#define CV_MENU_REPEAT     0x00BC3374u
#define CV_MENU_DIALOG     0x00AD33DCu
#define CV_MENU_PULSE      0x00BBE92Cu
#define CV_MENU_ROW_TEXT   0x00BBEE18u
#define CV_FN_MENU_RESET   func_0002A468
#define CV_FN_MENU_INPUT   func_0002F3D0
#define CV_FN_MENU_COLOR   func_001BD060
#define CV_FN_MENU_SOUND   func_00197308
#define CV_FN_MENU_FRAME   func_0002B548
#define CV_OG_MENU_RESET   cv_original_0002A468
#define CV_OG_MENU_INPUT   cv_original_0002F3D0
#define CV_OG_MENU_COLOR   cv_original_001BD060
#define CV_TITLE_STATE     0x00BBE8FCu
#define CV_FN_TITLE        func_0002B35C
#define CV_FN_LOGO         func_0020140C
#define CV_OG_TITLE        cv_original_0002B35C
#define CV_OG_LOGO         cv_original_0020140C
#define CV_DOOR_SKIP_SITE  0x001AFCCCu
/* The guest libc memset and memmove, done natively (cv_native_libc.cpp). */
#define CV_FN_MEMSET  func_007B6C38
#define CV_OG_MEMSET  cv_original_007B6C38
#define CV_FN_MEMMOVE func_007AE7B0
#define CV_OG_MEMMOVE cv_original_007AE7B0
#endif
