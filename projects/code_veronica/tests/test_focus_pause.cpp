// Pause on focus loss: which windows count as the game's focus, entering and
// leaving the pause, and the option turned off (cv_focus_pause.h).
#include "cv_focus_pause.h"
#include <cassert>
#include <cstdio>
using cv_focus::Sample;
using cv_focus::Tracker;
static Sample in_game() { Sample s; s.game_window = true; s.foreground_ours = true; return s; }
static Sample away() { Sample s; s.game_window = true; return s; }
int main() {
    const unsigned long long g = Tracker::kGraceMs;
    // The port's own windows (options menu, F7 maps, SRT): all this process's.
    assert(cv_focus::game_has_focus(in_game()));
    // Desktop, another program, nothing in front during Alt+Tab.
    assert(!cv_focus::game_has_focus(away()));
    // Minimized, even with the options menu in front.
    { Sample s = in_game(); s.minimized = true; assert(!cv_focus::game_has_focus(s)); }
    // Before Jogar there is no game window: the launcher alone never pauses.
    { Sample s; assert(cv_focus::game_has_focus(s)); Tracker t; assert(!t.update(true, s, 0)); assert(!t.update(true, s, 10 * g)); }

    // Clicking elsewhere pauses after the grace; coming back resumes at once.
    { Tracker t;
      assert(!t.update(true, in_game(), 1000));
      assert(!t.update(true, away(), 2000));
      assert(!t.update(true, away(), 2000 + g - 1));
      assert(t.update(true, away(), 2000 + g)); assert(t.paused());
      assert(t.update(true, away(), 60000));
      assert(!t.update(true, in_game(), 60001)); assert(!t.paused()); }

    // Focus handed between the port's windows (a blink of "nothing in front")
    // never pauses, and a later blink starts its own grace.
    { Tracker t;
      assert(!t.update(true, away(), 100));
      assert(!t.update(true, in_game(), 100 + g / 2));
      assert(!t.update(true, away(), 100 + 2 * g));
      assert(!t.update(true, away(), 100 + 3 * g - 1));
      assert(t.update(true, away(), 100 + 3 * g)); }

    // Minimizing pauses at once; restoring resumes.
    { Tracker t; Sample s = in_game(); s.minimized = true;
      assert(t.update(true, s, 5)); assert(!t.update(true, in_game(), 6)); }

    // Option off: nothing changes, and turning it off mid-pause resumes.
    { Tracker t; Sample m = away(); m.minimized = true;
      assert(!t.update(false, away(), 0)); assert(!t.update(false, away(), 100 * g));
      assert(!t.update(false, m, 100 * g)); assert(!t.paused());
      assert(t.update(true, m, 100 * g + 1));
      assert(!t.update(false, m, 100 * g + 2)); assert(!t.paused());
      // Turned back on while still away: a fresh grace, not an instant pause.
      assert(!t.update(true, away(), 200 * g));
      assert(t.update(true, away(), 201 * g)); }

    // The game window closing while paused (process ending) leaves no pause.
    { Tracker t; assert(t.update(true, away(), 0) == false); assert(t.update(true, away(), g));
      Sample gone; assert(!t.update(true, gone, g + 1)); }

    std::puts("PASS: focus decision, grace, minimize, resume, option off");
    return 0;
}
