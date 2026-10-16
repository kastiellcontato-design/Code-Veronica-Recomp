/* Skip the opening logos and the intro movie (patch flag cv_skip_logos, a
 * launcher switch): the game opens on its title screen.
 *
 * The CAPCOM and MT FRAMEWORK logos are one layout animation played by the
 * uBio_cvLogo unit. Its last step (func_0020140C) waits until the animation's
 * frame (a float at +0x34 of the layout, the unit's +0x3C) passes 0x333, then
 * marks the logo done and the game goes on to the intro movie and the title.
 * With the flag on, the frame is put past that point first, so the logo ends
 * as soon as it has started.
 *
 * The title sequence (func_0002B35C, state at 0x00BBE8FC) sets up the title,
 * then in state 7 plays movie 16 (the intro, movie/mv_016.pam), waits on it in
 * states 8 and 9, and in state -1 closes and hands over to PRESS START. With
 * the flag on, state 7 goes straight to -1, so the movie never starts.
 *
 * Both wrappers are built by tools/cv_native_menu_hooks.py. The addresses
 * named here are the European ones; cv_version.h has each version's.
 */
#include "ppu_recomp.h"
#include "cv_version.h"
#include <cstdint>
#include <cstring>

void CV_OG_LOGO(ppu_context*);
void CV_OG_TITLE(ppu_context*);
extern "C" volatile unsigned char* ps3_patch_flag(const char* name);

static bool skip_on()
{
    static volatile unsigned char* skip = nullptr;
    if (!skip) skip = ps3_patch_flag("cv_skip_logos");
    return *skip != 0;
}

void CV_FN_TITLE(ppu_context* ctx)
{
    const uint32_t kTitleState = CV_TITLE_STATE;
    if (skip_on() && vm_read32(kTitleState) == 7) vm_write32(kTitleState, 0xFFFFFFFFu);   /* past the intro movie */
    CV_OG_TITLE(ctx);
}

void CV_FN_LOGO(ppu_context* ctx)
{
    if (skip_on()) {
        const uint32_t layout = vm_read32((uint32_t)ctx->gpr[3] + 0x3C);
        if (layout) {
            const float last = 820.0f;   /* one past the 0x333 the check waits for */
            uint32_t bits;
            std::memcpy(&bits, &last, 4);
            vm_write32(layout + 0x34, bits);
        }
    }
    CV_OG_LOGO(ctx);
}
