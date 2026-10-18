#pragma once
#include "cv_srt.h"
#include <algorithm>
#include <array>
#include <cstdint>

// Display-only absence tolerance. Never writes guest memory or delays live HP.
namespace cv_srt_enemy_hold {
constexpr uint64_t absence_ms=1000;
struct Observation {
    bool identified=false, shown=false, remove=false;
    int type=0, slot=0;
    cv_srt_enemy enemy{};
};
class Cache {
    struct Entry {
        bool valid=false;
        int type=0, slot=0;
        uint64_t last_seen=0;
        cv_srt_enemy enemy{};
    };
    std::array<Entry,CV_SRT_MAX_ENEMIES> entries{};
    int room=-1, character=-1;
public:
    void clear() { *this=Cache{}; }
    void apply(cv_srt_state& state,
               const std::array<Observation,CV_SRT_MAX_ENEMIES>& observed,
               uint64_t now) {
        if(room!=state.room || character!=state.character) {
            clear(); room=state.room; character=state.character;
        }
        state.enemy_count=0;
        for(size_t i=0;i<entries.size();++i) {
            Entry& entry=entries[i];const Observation& current=observed[i];
            if(entry.valid && (now-entry.last_seen>=absence_ms ||
               (current.identified && (entry.type!=current.type || entry.slot!=current.slot))))
                entry={};
            if(current.shown) {
                cv_srt_enemy live=current.enemy;
                // A brief inactive read must not lower an estimated maximum
                // when the original reader sees the same actor become active.
                if(entry.valid && live.estimated)
                    live.maximum=std::max(live.maximum,entry.enemy.maximum);
                entry={live.hp>0,current.type,current.slot,now,live};
                state.enemies[state.enemy_count++]=live;
            } else if(current.remove) {
                entry={}; // confirmed zero/negative HP or explicit boss phase exclusion
            } else if(entry.valid) {
                state.enemies[state.enemy_count++]=entry.enemy;
            }
        }
    }
};
}
