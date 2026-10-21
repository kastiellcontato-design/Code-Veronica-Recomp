#pragma once
/* The in-game item menu (F6): the catalog, the placement rules and the one
 * guest write, kept free of windows so tests/test_items.cpp drives them.
 *
 * The inventory is the game's own (CV_SRT_INVENTORY + character * 0x40): the
 * equipped entry's number, then 11 entries `FLAGS ID QUANTITY(16)`. Entry 0 is
 * the fixed slot (lighter, lockpick); entries 1..10 are a packed list, as the
 * game keeps it when an item is used up. The game shows 8 of them, or 10 once
 * the Side Pack was used: bit 0x08000000 of state +0x6C (EU func_0019FA5C,
 * the inventory screen, and func_00136FD4 read it; JP and US the same). Every
 * item takes one entry; the wide weapons are only drawn wider.
 *
 * The menu asks, the game frame writes: the game is paused while the menu is
 * open, so each choice joins a queue and the menu shows the inventory as it
 * will be. frame(), on the PPU thread at the start of the game's frame
 * function, takes the queue once the game runs again and, for each entry,
 * checks again that a live character is in a running room with none of the
 * game's screens open but its inventory (a removal: that screen or plain
 * gameplay, no door, event or fade) and that the entry is still what the
 * menu saw.
 * One word per item put in; a removal moves the entries below it up, as the
 * game does. Nothing else in guest memory changes. */
#include <atomic>
#include <cstdint>
#include <vector>

namespace cv_items {

enum Category { WEAPONS, AMMO, HEALING, KEY_ITEMS, OTHER, CATEGORY_COUNT };

struct Info {
    int id;
    Category category;
    bool counted;      // the entry's quantity is shown and chosen
    int quantity;      // proposed quantity
    int maximum;       // highest the menu offers
    bool percent;      // assault rifle, submachine guns, M1P: 0..100 %
};
// Every item the menu offers, in display order. No unknown IDs.
const std::vector<Info>& catalog();
const Info* info(int id);
std::vector<int> items(Category category);

constexpr int kEntries = 11;   // entry 0 is the fixed slot

struct Inventory {
    bool valid = false;        // a live character with a loaded room
    bool writable = false;     // ... its room running (or the game's inventory screen), no other screen: an item may be put in
    bool can_remove = false;   // ... and in plain gameplay or the game's inventory screen: a removal too
    bool playing = false;      // ... in plain gameplay, no screen at all
    int character = -1;        // 0 Claire, 1 Chris, 2 Steve, 3 Wesker
    int capacity = 8;          // usable entries after the fixed one: 8 or 10
    int equipped = 0;          // 0 none, else entry + 1
    uint32_t entry[kEntries] = {};
    int id(int i) const { return int((entry[i] >> 16) & 255); }
    int quantity(int i) const { return int(entry[i] & 0xffff); }
    bool empty(int i) const { return id(i) == 0; }
    bool is_equipped(int i) const { return equipped == i + 1; }
    int first_empty() const;   // 1..capacity, or -1
    int used() const;          // occupied entries among 1..capacity
};

// Reads the live inventory. Safe from any thread: reads only.
Inventory read();
uint32_t state_base();
// One stderr line with the words read() judged by, when the menu refuses.
void report_refusal(const char* what);

enum Result {
    R_NONE, R_QUEUED, R_DONE,
    R_FULL,          // no empty entry, and no entry chosen to replace
    R_EQUIPPED,      // the chosen entry holds the equipped weapon
    R_BAD,           // unknown item, quantity or entry
    R_NOT_PLAYING,   // a game screen, loading, no character (removal: also a door or event)
    R_CHANGED,       // another character, or the entry changed meanwhile
    R_DISABLED,      // the launcher option is off
    R_BUSY           // the queue is full
};

// target: 0 = the first empty entry, 1..10 = that entry (replacing it).
// Returns the entry that would be written (1..10), or -1 with `why` set.
int place(const Inventory& inventory, int id, int target, Result& why);
// The entry's word: flags clear, the item, its quantity (1 when uncounted,
// 0 for the knife, as the game stores them).
uint32_t encode(int id, int quantity);
int clamp_quantity(int id, int quantity);

void set_enabled(bool on);   // off: the queue is dropped and nothing is accepted
bool enabled();
constexpr int kQueue=10;
// `entry` comes from place() on the inventory the menu shows; `expected` is
// that entry's word there (0 when empty).
Result request(int character,int id,int quantity,int entry,uint32_t expected);
// Takes the item in `entry` out, as the game does when one is used up: the
// entries below move up and the equipped number follows. Never the fixed
// slot or the equipped weapon.
Result request_remove(int character,int entry,uint32_t expected);
int pending();
// The live inventory with the queued items in place: what the menu shows.
void apply_pending(Inventory& inventory);
struct Outcome { Result result; int entry, id; bool removed = false; };
// Outcomes written by frame() after `serial`; `serial` moves to the latest.
std::vector<Outcome> outcomes(unsigned& serial);
unsigned outcome_serial();
// Game thread, once per game frame.
void frame();

}  // namespace cv_items
