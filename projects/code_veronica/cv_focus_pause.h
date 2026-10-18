#pragma once
/* Pause when the game window loses focus: the decision alone, free of Win32
 * so the tests can drive it (tests/test_focus_pause.cpp). cv_focus_pause.cpp
 * samples the desktop and applies the result. */
namespace cv_focus {

struct Sample {
    bool game_window = false;      // the game window exists (Jogar was pressed)
    bool foreground_ours = false;  // the foreground window belongs to this process
    bool minimized = false;        // the game window is minimized
};

// The port's own windows -- the options menu (the launcher), the F7 map
// window, the SRT, any later one -- are this process's windows, so they keep
// the game running. No foreground window at all (the moment between two
// windows during Alt+Tab) counts as away; a minimized game is away even with
// the options menu in front. Before the game window exists nothing pauses.
inline bool game_has_focus(const Sample& s)
{
    return !s.game_window || (s.foreground_ours && !s.minimized);
}

class Tracker {
    bool paused_ = false, away_ = false;
    unsigned long long away_since_ = 0;
public:
    // Focus moving between the port's own windows passes through "nothing in
    // front" for a few milliseconds; waiting this long keeps that from
    // pausing. Minimizing pauses at once.
    static constexpr unsigned long long kGraceMs = 150;

    // Returns whether the game should be paused now.
    bool update(bool enabled, const Sample& s, unsigned long long now_ms)
    {
        if (!enabled || game_has_focus(s)) { away_ = paused_ = false; return false; }
        if (!away_) { away_ = true; away_since_ = now_ms; }
        if (s.minimized || now_ms - away_since_ >= kGraceMs) paused_ = true;
        return paused_;
    }
    bool paused() const { return paused_; }
};

}  // namespace cv_focus
