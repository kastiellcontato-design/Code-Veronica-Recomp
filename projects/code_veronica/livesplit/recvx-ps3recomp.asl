// SPDX-FileCopyrightText: 2025 Kapdap <kapdap@pm.me>
//
// SPDX-License-Identifier: MIT
/*  Resident Evil/Biohazard: Code: Veronica Auto Splitter
 *  https://github.com/kapdap/re-cvx-autosplitter
 *
 *  Copyright 2025 Kapdap <kapdap@pm.me>
 *
 *  Use of this source code is governed by an MIT-style
 *  license that can be found in the LICENSE file or at
 *  https://opensource.org/licenses/MIT.
 *
 *  ps3recomp port support: this copy also attaches to the PC port's
 *  executables (MyGameRecomp.exe, MyGameRecomp_JP.exe, MyGameRecomp_US.exe).
 *  Each exports g_cv_livesplit: "PS3RECOMP-CVX", then where the PS3's memory
 *  starts in the process (offset 16) and the release code (offset 24). The
 *  game's variables sit at their PS3 addresses from there, as in RPCS3, so
 *  the PS3 pointer tables below serve the port unchanged.
 */

state("rpcs3") {}
state("MyGameRecomp") {}
state("MyGameRecomp_JP") {}
state("MyGameRecomp_US") {}
state("pcsx2") {}
state("pcsx2-qtx64") {}
state("pcsx2-qt") {}
state("dolphin") {}

startup
{
    // Path to split log
    vars.logPath = "Components/recvx.txt";

    // Split tracker
    vars.splits = new Dictionary<string, bool>();

    // Inventory split tracker
    vars.itemSplits = new Dictionary<byte, bool>();
    vars.itemIndex = new Dictionary<byte, string>();

    // Killed split tracker
    vars.killedSplits = new Dictionary<short, bool>();
    vars.killedIndex = new Dictionary<short, string>();
    vars.killedMap = new Dictionary<int, short>();
    vars.killedItr = 0;

    // Events split tracker
    vars.eventsSplits = new Dictionary<short, bool>();
    vars.eventsIndex = new Dictionary<short, string>();
    vars.eventsMap = new Dictionary<int, short>();
    vars.eventsItr = 0;

    // Adds a split
    vars.AddSplit = (Action<string, bool, string, string>)((id, default_value, description, parent) => {
        vars.splits.Add(id, false);
        settings.Add(id, default_value, description, parent);
    });

    // Adds an inventory split
    vars.AddItemSplit = (Action<byte, string, bool, string, string>)((code, id, default_value, description, parent) => {
        vars.itemSplits.Add(code, false);
        vars.itemIndex.Add(code, id); // Map item code to split name
        settings.Add(id, default_value, description, parent);
    });

    // Adds a killed split
    vars.AddKilledSplit = (Action<short, string, bool, string, string>)((code, id, default_value, description, parent) => {
        vars.killedSplits.Add(code, false);
        vars.killedIndex.Add(code, id);
        vars.killedMap.Add(vars.killedItr++, code);
        settings.Add(id, default_value, description, parent);
    });

    // Adds an event split
    vars.AddEventsSplit = (Action<short, string, bool, string, string>)((code, id, default_value, description, parent) => {
        vars.eventsSplits.Add(code, false);
        vars.eventsIndex.Add(code, id);
        vars.eventsMap.Add(vars.eventsItr++, code);
        settings.Add(id, default_value, description, parent);
    });

    // Resets split tracker
    vars.ResetSplits = (Action) (() => {
        List<string> keys = new List<string>(vars.splits.Keys);
        foreach(string key in keys)
            vars.splits[key] = false;
    });

    // Resets door split tracker
    vars.ResetDoorSplits = (Action) (() => {
        vars.doorsIndex = 0;
    });

    // Resets inventory split tracker
    vars.ResetItemSplits = (Action) (() => {
        List<byte> keys = new List<byte>(vars.itemSplits.Keys);
        foreach(byte key in keys)
            vars.itemSplits[key] = false;
    });

    // Resets killed split tracker
    vars.ResetKilledSplits = (Action) (() => {
        List<short> keys = new List<short>(vars.killedSplits.Keys);
        foreach(short key in keys)
            vars.killedSplits[key] = false;
    });

    // Resets events split tracker
    vars.ResetEventsSplits = (Action) (() => {
        List<short> keys = new List<short>(vars.eventsSplits.Keys);
        foreach(short key in keys)
            vars.eventsSplits[key] = false;
    });

    // Swaps 2 byte values
    vars.SwapBytes = (Func<ushort, ushort>)((value) => {
        int b1 = (value >> 0) & 0xff;
        int b2 = (value >> 8) & 0xff;

        return (ushort) (b1 << 8 | b2 << 0);
    });

    // Swaps 4 byte values
    vars.SwapBytesInt = (Func<uint, uint>)((value) => {
        return ((value & 0x000000ff) << 24) +
            ((value & 0x0000ff00) << 8) +
            ((value & 0x00ff0000) >> 8) +
            ((value & 0xff000000) >> 24);
    });

    // Checks if a specific bit in value is set
    vars.BitCheck = new Func<byte, int, bool>((byte val, int b) => (val & (1 << b)) != 0);

    settings.Add("time", true, "Timing Methods");
    settings.Add("LRT", false, "Load Remover Time", "time");
    settings.SetToolTip("LRT", "Removes door load time");

    settings.Add("doorSplits", true, "Doors (Only enable 1 option)");
    settings.SetToolTip("doorSplits", "Enables door splits for the selected route");
    settings.Add("anySplits", false, "any% (336 splits)", "doorSplits");
    settings.Add("anySplitsDS", false, "any% (Doorskip) (338 splits)", "doorSplits");
    settings.Add("rocketSplits", false, "Rocket Launcher (334 splits)", "doorSplits");
    settings.Add("rocketSplitsDS", false, "Rocket Launcher (Doorskip) (336 splits)", "doorSplits");
    settings.Add("knifeSplits", false, "Knife Only (336 splits)", "doorSplits");
    settings.Add("knifeSplitsDS", false, "Knife Only (Doorskip) (338 splits)", "doorSplits");
    settings.Add("srankSplits", false, "S-Rank (344 splits)", "doorSplits");
    settings.Add("srankSplitsDS", false, "S-Rank (Glitchless) (Doorskip) (346 splits)", "doorSplits");
    settings.Add("srankSplitsDSGL", false, "S-Rank (Glitched) (Doorskip) (348 splits)", "doorSplits");
    settings.Add("basicSplits", false, "Basic (All doors)", "doorSplits");
    settings.SetToolTip("basicSplits", "Splits on every door transition");

    settings.Add("bosses", true, "Bosses");
    vars.AddEventsSplit(215, "bandersnatchSpawned", false, "Bandersnatch (Spawned)", "bosses");
    vars.AddKilledSplit(133, "bandersnatchKilled", false, "Bandersnatch (Killed)", "bosses");
    vars.AddEventsSplit(129, "drZombieSpawned", false, "Dr. Zombie (Spawned)", "bosses");
    vars.AddKilledSplit(35, "drZombieKilled", false, "Dr. Zombie (Killed)", "bosses");
    vars.AddEventsSplit(169, "tyrant1Spawned", false, "Palace Tyrant (Spawned)", "bosses");
    vars.AddKilledSplit(89, "tyrant1Killed", false, "Palace Tyrant (Killed)", "bosses");
    vars.AddEventsSplit(269, "tyrant2Spawned", false, "Plane Tyrant (Spawned)", "bosses");
    vars.AddKilledSplit(185, "tyrant2Killed", false, "Plane Tyrant (Killed)", "bosses");
    vars.AddEventsSplit(305, "nosferatuSpawned", false, "Nosferatu (Spawned)", "bosses");
    vars.AddKilledSplit(205, "nosferatuKilled", false, "Nosferatu (Killed)", "bosses");
    vars.AddKilledSplit(248, "gulpWormKilled", false, "Tomb Gulp Worm (Killed)", "bosses");
    vars.AddEventsSplit(346, "albinoidSpawned", false, "Albinoid (Spawned)", "bosses");
    vars.AddKilledSplit(221, "albinoidKilled", false, "Albinoid (Killed)", "bosses");
    vars.AddKilledSplit(357, "alexia1Killed", false, "Alexia 1 (Killed)", "bosses");
    vars.AddSplit("alexia2Spawned", false, "Alexia 2 (Spawned)", "bosses");
    vars.AddKilledSplit(348, "alexia2Killed", false, "Alexia 2 (Killed)", "bosses");
    vars.AddKilledSplit(349, "alexia3Killed", false, "Alexia 3 (Killed)", "bosses");

    settings.Add("eventsNew", true, "Events");
    vars.AddEventsSplit(95, "partTwoEvent", false, "Antartic Transport Terminal", "eventsNew");
    settings.SetToolTip("partTwoEvent", "Splits at the start of part two");
    vars.AddEventsSplit(304, "helipadEvent", false, "Antartic Helipad", "eventsNew");
    settings.SetToolTip("helipadEvent", "Splits when Claire and Steve arrive at the helipad");
    vars.AddEventsSplit(366, "chrisEvent", false, "Start as Chris", "eventsNew");
    settings.SetToolTip("chrisEvent", "Splits when taking control of Chris on Rockfort Island");
    vars.AddSplit("endGame", true, "End Game", "eventsNew");
    settings.SetToolTip("endGame", "Splits when the game rank screen is displayed");

    settings.Add("weapongroup", true, "Weapons");
    vars.AddSplit("combatKnife", false, "Combat Knife", "weapongroup");
    vars.AddItemSplit(0x09, "handgun", false, "Handgun", "weapongroup");
    vars.AddSplit("subMachineGun", false, "Sub Machine Gun", "weapongroup");
    vars.AddItemSplit(0x21, "goldLugers", false, "Gold Lugers", "weapongroup");
    vars.AddItemSplit(0x06, "grenadeLauncher", false, "Grenade Launcher", "weapongroup");
    vars.AddItemSplit(0x07, "bowGun", false, "Bow Gun", "weapongroup");
    vars.AddItemSplit(0x04, "shotgun", false, "Shotgun", "weapongroup");
    vars.AddItemSplit(0x0B, "linearLauncher", false, "Linear Launcher", "weapongroup");

    settings.Add("keygroup", true, "Keys (Rockfort)");
    vars.AddItemSplit(0x54, "padlockKey", false, "Padlock Key", "keygroup");
    vars.AddItemSplit(0x44, "goldKey", false, "Gold Key", "keygroup");
    vars.AddItemSplit(0x43, "silverKey", false, "Silver Key", "keygroup");
    vars.AddItemSplit(0x4B, "airportKey", false, "Airport Key", "keygroup");
    vars.AddItemSplit(0x61, "chemStorageKey", false, "Chem. Storage Key", "keygroup");
    vars.AddItemSplit(0x60, "turnTableKey", false, "Turn Table Key", "keygroup");

    settings.Add("keyantarcticagroup", true, "Keys (Antarctica)");
    vars.AddItemSplit(0x5A, "miningRoomKey", false, "Mining Room Key", "keyantarcticagroup");
    vars.AddItemSplit(0x59, "machineRoomKey", false, "Machine Room Key", "keyantarcticagroup");
    vars.AddItemSplit(0x36, "craneKey", false, "Crane Key", "keyantarcticagroup");
    vars.AddItemSplit(0x5C, "sterileRoomKey", false, "Sterile Room Key", "keyantarcticagroup");

    settings.Add("toolsgroup", true, "Tools");
    vars.AddItemSplit(0x52, "extinguisher", false, "Extinguisher", "toolsgroup");
    vars.AddItemSplit(0x35, "steeringWheel", false, "Steering Wheel", "toolsgroup");
    vars.AddItemSplit(0x4D, "skeletonPicture", false, "Skeleton Picture", "toolsgroup");
    vars.AddItemSplit(0x33, "glassEye", false, "Glass Eye", "toolsgroup");
    vars.AddItemSplit(0x67, "rustedSword", false, "Rusted Sword", "toolsgroup");
    vars.AddItemSplit(0x34, "pianoRoll", false, "Piano Roll", "toolsgroup");
    vars.AddItemSplit(0x41, "controlLever", false, "Control Lever", "toolsgroup");
    vars.AddItemSplit(0x5B, "barCodeSticker", false, "Bar Code Sticker", "toolsgroup");
    vars.AddItemSplit(0x27, "gasMask", false, "Gas Mask", "toolsgroup");
    vars.AddItemSplit(0x5E, "batteryPack", false, "Battery Pack", "toolsgroup");
    vars.AddItemSplit(0x5D, "doorKnob", false, "Door Knob", "toolsgroup");
    vars.AddItemSplit(0x64, "tankObject", false, "Tank Object", "toolsgroup");
    vars.AddItemSplit(0x51, "halberd", false, "Halberd", "toolsgroup");
    vars.AddItemSplit(0x75, "paperWeight", false, "Paper Weight", "toolsgroup");

    settings.Add("cardgroup", true, "Cards");
    vars.AddItemSplit(0x3E, "biohazardCard", false, "Biohazard Card", "cardgroup");
    vars.AddItemSplit(0x4C, "emblemCard", false, "Emblem Card", "cardgroup");
    vars.AddItemSplit(0x69, "securityCard", false, "Security Card", "cardgroup");

    settings.Add("emblemgroup", true, "Emblems");
    vars.AddItemSplit(0x3B, "hawkEmblem", false, "Hawk Emblem", "emblemgroup");
    vars.AddItemSplit(0x56, "spAlloyEmblem", false, "Sp. Alloy Emblem", "emblemgroup");
    vars.AddItemSplit(0x53, "briefcase", false, "Briefcase", "emblemgroup");
    vars.AddItemSplit(0x55, "tg01", false, "TG-01", "emblemgroup");

    settings.Add("proofgroup", true, "Proofs");
    vars.AddItemSplit(0x46, "navyProof", false, "Navy Proof", "proofgroup");
    vars.AddItemSplit(0x45, "armyProof", false, "Army Proof", "proofgroup");
    vars.AddSplit("airForceProof", false, "Air Force Proof", "proofgroup");
    vars.AddSplit("airForceProofChris", false, "Air Force Proof (Chris)", "proofgroup");

    settings.Add("jewelgroup", true, "Family Jewels");
    vars.AddItemSplit(0x2B, "alexandersPierce", false, "Alexander's Pierce", "jewelgroup");
    vars.AddItemSplit(0x6B, "alexiasChoker", false, "Alexia's Choker", "jewelgroup");
    vars.AddItemSplit(0x2D, "alfredsRing", false, "Alfred's Ring", "jewelgroup");
    vars.AddItemSplit(0x2C, "alexandersJewel", false, "Alexander's Jewel", "jewelgroup");
    vars.AddItemSplit(0x6C, "alexiasJewel", false, "Alexia's Jewel", "jewelgroup");
    vars.AddItemSplit(0x2E, "alfredsJewel", false, "Alfred's Jewel", "jewelgroup");

    settings.Add("tigergroup", true, "Tiger Jewels");
    vars.AddItemSplit(0x70, "blueJewel", false, "Blue Jewel", "tigergroup");
    vars.AddItemSplit(0x6F, "redJewel", false, "Red Jewel", "tigergroup");

    settings.Add("dragonflygroup", true, "Dragonflies");
    vars.AddItemSplit(0x77, "silverDragonfly", false, "Silver Dragonfly", "dragonflygroup");
    vars.AddItemSplit(0x76, "silverDragonflyObject", false, "Silver Dragonfly (No Wings)", "dragonflygroup");
    vars.AddItemSplit(0x42, "goldDragonfly", false, "Gold Dragonfly", "dragonflygroup");
    vars.AddItemSplit(0x4F, "goldDragonflyObject", false, "Gold Dragonfly (No Wings)", "dragonflygroup");
    vars.AddItemSplit(0x78, "wingObject", false, "Wing Object (First Pickup)", "dragonflygroup");

    settings.Add("antgroup", true, "Ants");
    vars.AddItemSplit(0x3C, "queenAntObject", false, "Queen Ant Object", "antgroup");
    vars.AddItemSplit(0x3D, "kingAntObject", false, "King Ant Object", "antgroup");

    settings.Add("valvegroup", true, "Valves");
    vars.AddItemSplit(0x57, "valveHandle", false, "Valve Handle", "valvegroup");
    vars.AddSplit("octaValveHandle", false, "Octa Valve Handle", "valvegroup");
    vars.AddSplit("octaValveHandleChris", false, "Octa Valve Handle (Chris)", "valvegroup");
    vars.AddItemSplit(0x72, "sqValveHandle", false, "Sq. Valve Handle", "valvegroup");
    vars.AddItemSplit(0x71, "squaresocket", false, "Square Socket", "valvegroup");

    settings.Add("clementgroup", true, "Clement");
    vars.AddItemSplit(0x63, "clementSigma", false, "Clement Sigma", "clementgroup");
    vars.AddItemSplit(0x62, "clementAlpha", false, "Clement Alpha", "clementgroup");
    vars.AddItemSplit(0x90, "clementMixture", false, "Clement Mixture", "clementgroup");

    settings.Add("plategroup", true, "Eagle Plates");
    vars.AddSplit("eaglePlateDiorama", false, "Eagle Plate (Diorama Room)", "plategroup");
    vars.AddSplit("eaglePlateUmbrella", false, "Eagle Plate (Umbrella Meeting Room)", "plategroup");
    vars.AddSplit("eaglePlateWaterPool", false, "Eagle Plate (Water Pool)", "plategroup");

    settings.Add("musicboxgroup", true, "Music Box Plates");
    vars.AddSplit("musicBoxPlate", false, "Music Box Plate", "musicboxgroup");
    vars.AddSplit("musicBoxPlateChris", false, "Music Box Plate (Chris)", "musicboxgroup");

    settings.Add("containers", true, "Containers");
    vars.AddItemSplit(0x74, "earthenwareVase", false, "Earthenware Vase", "containers");
    settings.SetToolTip("earthenwareVase", "Contains the Queen Ant Object");
    vars.AddItemSplit(0x7E, "plantPot", false, "Plant Pot", "containers");
    settings.SetToolTip("plantPot", "Contains the Machine Room Key");
    vars.AddItemSplit(0x79, "crystal", false, "Crystal", "containers");
    settings.SetToolTip("crystal", "Contains the Security Card for Claire");
    vars.AddItemSplit(0x6A, "securityFile", false, "Security File", "containers");
    settings.SetToolTip("securityFile", "Contains the Security Card for Chris");

    // Items that are not normally used in a speedrun
    settings.Add("othergroup", false, "Other");
    vars.AddItemSplit(0x40, "detonator", false, "Detonator", "othergroup");
    vars.AddItemSplit(0x95, "emptyExtinguisher", false, "Empty Extinguisher", "othergroup");
    vars.AddItemSplit(0x68, "hemostatic", false, "Hemostatic", "othergroup");
    vars.AddItemSplit(0x49, "idCard", false, "ID Card", "othergroup");
    vars.AddItemSplit(0x48, "keyWithTag", false, "Key With Tag", "othergroup");
    settings.SetToolTip("keyWithTag", "Changes to Storage Key when examined.");
    vars.AddItemSplit(0x32, "lockpick", false, "Lockpick", "othergroup");
    vars.AddItemSplit(0x88, "lugerReplica", false, "Luger Replica", "othergroup");
    vars.AddItemSplit(0x73, "serum", false, "Serum", "othergroup");

    // Weapons that are not normally used in a speedrun
    settings.Add("otherweapongroup", false, "Other (Weapons)");
    vars.AddItemSplit(0x02, "assaultRifle", false, "Assault Rifle", "otherweapongroup");
    vars.AddItemSplit(0x0A, "customHandgun", false, "Custom Handgun", "otherweapongroup");
    vars.AddItemSplit(0x83, "enhancedHandgun", false, "Enhanced Handgun", "otherweapongroup");
    vars.AddItemSplit(0x8E, "m1p", false, "M-100P", "otherweapongroup");
    vars.AddItemSplit(0x20, "magnum", false, "Magnum", "otherweapongroup");
    vars.AddItemSplit(0x03, "sniperRifle", false, "Sniper Rifle", "otherweapongroup");
    vars.AddSplit("subMachineGunChris", false, "Sub Machine Gun (Chris)", "otherweapongroup");

    settings.Add("events", true, "Events (Old)");
    settings.SetToolTip("events", "Disable door splits to enable legacy event splits");
    vars.AddSplit("tyrantPlane", false, "Tyrant Plane Fight", "events");
    settings.SetToolTip("tyrantPlane", "Splits when Claire opens the door to the cargo hold");
    vars.AddSplit("antarcticaBase", false, "Antartic Transport Terminal", "events");
    settings.SetToolTip("antarcticaBase", "Splits during the cutscene when the plane crashes into the Antartic Transport Terminal");
    vars.AddSplit("nosferatuFight", false, "Nosferatu Fight", "events");
    settings.SetToolTip("nosferatuFight", "Splits during the cutscene when Claire and Steve arrive at the helipad");
    vars.AddSplit("startChris", false, "Start as Chris", "events");
    settings.SetToolTip("startChris", "Splits during cutscene when Chris arrives at Rockfort Island");

    // Splitter Options
    settings.Add("optionsGroup", false, "Options");
    settings.Add("logSplits", false, "Log Splits", "optionsGroup");
    settings.SetToolTip("logSplits", "Split log is saved to LiveSplit/" + vars.logPath);
    settings.Add("debugSplits", false, "Debug Splits", "optionsGroup");
    settings.SetToolTip("debugSplits", "Use DbgView application to view split log");

    // Contributors list
    settings.Add("contributors", false, "Contributors");
    settings.Add("contributors1", false, "TheDementedSalad (https://www.twitch.tv/thedementedsalad)", "contributors");
    settings.SetToolTip("contributors1", "Added support for load remover time");
    settings.Add("contributors2", false, "redshines (https://www.twitch.tv/redshines)", "contributors");
    settings.SetToolTip("contributors2", "Door split routes and testing");

    // Application information
    settings.Add("infogroup", false, "Info");
    settings.Add("infogroup1", false, "Resident Evil: Code: Veronica Auto Splitter by Kapdap", "infogroup");
    settings.Add("infogroup2", false, "Website: https://github.com/kapdap/re-cvx-autosplitter", "infogroup");
    settings.Add("infogroup3", false, "Last Update: 2025-08-27T19:35:00+1200", "infogroup");
}

init
{
    IntPtr basePointer = IntPtr.Zero; // Emulator virtual memory base pointer
    IntPtr productPointer = IntPtr.Zero; // Product code memory pointer

    IntPtr timePtr = IntPtr.Zero;
    IntPtr roomPtr = IntPtr.Zero;
    IntPtr screenPtr = IntPtr.Zero;
    IntPtr healthPtr = IntPtr.Zero;
    IntPtr statusPtr = IntPtr.Zero;
    IntPtr characterPtr = IntPtr.Zero;
    IntPtr inventoryPtr = IntPtr.Zero;
    IntPtr doorFlagPtr = IntPtr.Zero;
    IntPtr enemyPtr = IntPtr.Zero;
    IntPtr killedPtr = IntPtr.Zero;
    IntPtr eventsPtr = IntPtr.Zero;

    vars.lastSplit = String.Empty; // Name of the most recent split
    vars.productCode = String.Empty; // Used to detect when the game release has changed
    vars.isBigEndian = false; // Console uses big endian (e.g. PS3 and GCN)
    vars.console = String.Empty; // Console code (PS3, PS2, GCN)
    vars.doorsIndex = 0; // Current door split index
    vars.doorsEnabled = // Door splits enabled
        settings["basicSplits"]    ||
        settings["anySplits"]      ||
        settings["anySplitsDS"]    ||
        settings["rocketSplits"]   ||
        settings["rocketSplitsDS"] ||
        settings["knifeSplits"]    ||
        settings["knifeSplitsDS"]  ||
        settings["srankSplits"]    ||
        settings["srankSplitsDS"]  ||
        settings["srankSplitsDSGL"];

    // Supported game release product codes and associated memory pointers
    vars.productPointers = new Dictionary<string, Dictionary<string, int>>();
    vars.productPointers.Add("GCDJ08", new Dictionary<string, int>
    { // [GCN] [JP] Biohazard: Code: Veronica Kanzenban
        { "timePtr", 0x00438B9C },
        { "roomPtr", 0x00438BB0 },
        { "screenPtr", 0x00438349 },
        { "healthPtr", 0x004378FC },
        { "statusPtr", 0x00437579 },
        { "doorFlagPtr", 0x00438363 },
        { "characterPtr", 0x00438380 },
        { "inventoryPtr", 0x0043856C },
        { "enemyPtr", 0x0041C1F0 },
        { "killedPtr", 0x0043848C },
        { "eventsPtr", 0x0043838C }
    });
    vars.productPointers.Add("GCDE08", new Dictionary<string, int>
    { // [GCN] [US] Resident Evil: Code: Veronica X
        { "timePtr", 0x004345BC },
        { "roomPtr", 0x004345D0 },
        { "screenPtr", 0x00433D69 },
        { "healthPtr", 0x0043331C },
        { "statusPtr", 0x00432F99 },
        { "doorFlagPtr", 0x00433D83 },
        { "characterPtr", 0x00433DA0 },
        { "inventoryPtr", 0x00433F8C },
        { "enemyPtr", 0x00417C10 },
        { "killedPtr", 0x00433EAC },
        { "eventsPtr", 0x00433DAC }
    });
    vars.productPointers.Add("GCDP08", new Dictionary<string, int>
    { // [GCN] [EU] Resident Evil: Code: Veronica X
        { "timePtr", 0x00438B5C },
        { "roomPtr", 0x00438B70 },
        { "screenPtr", 0x00438309 },
        { "healthPtr", 0x004378BC },
        { "statusPtr", 0x00437539 },
        { "doorFlagPtr", 0x00438323 },
        { "characterPtr", 0x00438340 },
        { "inventoryPtr", 0x0043852C },
        { "enemyPtr", 0x0041C1B0 },
        { "killedPtr", 0x0043844C },
        { "eventsPtr", 0x0043834C }
    });
    vars.productPointers.Add("SLPM_650.22", new Dictionary<string, int>
    { // [PS2] [JP] Biohazard: Code: Veronica Kanzenban
        { "timePtr", 0x004314A0 },
        { "roomPtr", 0x004314B4 },
        { "screenPtr", 0x00430C4C },
        { "healthPtr", 0x004301FC },
        { "statusPtr", 0x0042FE6A },
        { "doorFlagPtr", 0x00430C64 },
        { "characterPtr", 0x00430C84 },
        { "inventoryPtr", 0x00430E70 },
        { "enemyPtr", 0x00403DE0 },
        { "killedPtr", 0x00430D90 },
        { "eventsPtr", 0x00430C90 }
    });
    vars.productPointers.Add("SLUS_201.84", new Dictionary<string, int>
    { // [PS2] [US] Resident Evil: Code: Veronica X
        { "timePtr", 0x004339A0 },
        { "roomPtr", 0x004339B4 },
        { "screenPtr", 0x0043314C },
        { "healthPtr", 0x004326FC },
        { "statusPtr", 0x0043236A },
        { "doorFlagPtr", 0x00433164 },
        { "characterPtr", 0x00433184 },
        { "inventoryPtr", 0x00433370 },
        { "enemyPtr", 0x004062E0 },
        { "killedPtr", 0x00433290 },
        { "eventsPtr", 0x00433190 }
    });
    vars.productPointers.Add("SLES_503.06", new Dictionary<string, int>
    { // [PS2] [EU] Resident Evil: Code: Veronica X
        { "timePtr", 0x0044A1D0 },
        { "roomPtr", 0x0044A1E4 },
        { "screenPtr", 0x0044997C },
        { "healthPtr", 0x00448F2C },
        { "statusPtr", 0x00448B9A },
        { "doorFlagPtr", 0x00449994 },
        { "characterPtr", 0x004499B4 },
        { "inventoryPtr", 0x00449BA0 },
        { "enemyPtr", 0x0041CB10 },
        { "killedPtr", 0x00449AC0 },
        { "eventsPtr", 0x004499C0 }
    });
    vars.productPointers.Add("NPUB30467", new Dictionary<string, int>
    { // [PS3] [US] Resident Evil Code: Veronica X HD
        { "timePtr", 0x00BB3DB8 },
        { "roomPtr", 0x00BB3DCC },
        { "screenPtr", 0x00BB3565 },
        { "healthPtr", 0x00BDEA1C },
        { "statusPtr", 0x00BDE689 },
        { "doorFlagPtr", 0x00BB357F },
        { "characterPtr", 0x00BB359C },
        { "inventoryPtr", 0x00BB3788 },
        { "enemyPtr", 0x00BDEB78 },
        { "killedPtr", 0x00BB36A8 },
        { "eventsPtr", 0x00BB35A8 }
    });
    vars.productPointers.Add("NPEB00553", new Dictionary<string, int>
    { // [PS3] [EU] Resident Evil Code: Veronica X
        { "timePtr", 0x00BC40B8 },
        { "roomPtr", 0x00BC40CC },
        { "screenPtr", 0x00BC3865 },
        { "healthPtr", 0x00BEED1C },
        { "statusPtr", 0x00BEE989 },
        { "doorFlagPtr", 0x00BC387F },
        { "characterPtr", 0x00BC389C },
        { "inventoryPtr", 0x00BC3A88 },
        { "enemyPtr", 0x00BEEE78 },
        { "killedPtr", 0x00BC39A8 },
        { "eventsPtr", 0x00BC38A8 }
    });
    vars.productPointers.Add("NPJB00135", new Dictionary<string, int>
    { // [PS3] [JP] Biohazard Code: Veronica Kanzenban
        { "timePtr", 0x00BB3E38 },
        { "roomPtr", 0x00BB3E4C },
        { "screenPtr", 0x00BB35E5 },
        { "healthPtr", 0x00BDEA9C },
        { "statusPtr", 0x00BDE709 },
        { "doorFlagPtr", 0x00BB35FF },
        { "characterPtr", 0x00BB361C },
        { "inventoryPtr", 0x00BB3808 },
        { "enemyPtr", 0x00BDEBF8 },
        { "killedPtr", 0x00BB3728 },
        { "eventsPtr", 0x00BB3628 }
    });

    vars.productOffsets = new Dictionary<string, List<int>>();
    vars.productOffsets.Add("PS2", new List<int>()
    {
        0x00012610,
        0x000155D0,
        0x00015B90
    });

    // Any% Glitched and Glitchless Non-Doorskip Non-LTR
    vars.anySplits = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  778,
        786,  785,  789,  790,  768,  771,  772,  771,  768,  770,
        777,  776,  769,  785,  784,  785,  789,  790,  780,  782,
        780,  769,  776,  781,  782,  781,  776,  777,  770,  768,
        771,  772,  771,  768,  770,  777,  268,  256,  258,  263,
        260,  263,  258,  264,  265,  266,  512,  513,  515,  517,
        515,  513,  512,  266,  265,  264,  258,  263,  262,  263,
        258,  256,  268,  10,   3,    5,    8,    16,   11,   12,
        11,   13,   14,   15,   14,   13,   11,   16,   8,    5,
        3,    10,   268,  256,  258,  267,  258,  264,  265,  266,
        512,  513,  515,  516,  518,  519,  518,  517,  516,  515,
        513,  512,  266,  265,  264,  258,  256,  269,  270,  1024,
        1025, 1280, 1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026,
        1028, 791,  268,  256,  269,  270,  1027, 1024, 1025, 1280,
        1281, 1280, 1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542,
        1543, 1542, 1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541,
        1542, 1543, 1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542,
        1546, 1814, 1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793,
        1798, 1799, 1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806,
        1810, 1809, 1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794,
        1811, 1793, 1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049,
        2048, 2049, 2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052,
        1805, 1808, 1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798,
        1793, 1811, 1794, 1817, 1802, 1817, 1794, 1811, 1793, 1798,
        1800, 2313, 2304, 2307, 2308, 2307, 2304, 2309, 2310, 2309,
        2314, 2317, 2316, 2318, 2316, 2320, 2321, 2322, 2321, 2320,
        2316, 2318, 2316, 2317, 2314, 2315, 2314, 2309, 2314, 2317,
        2319, 2330, 2331, 2333, 2332, 2336, 2324, 2325, 2324, 2336,
        2320, 2336, 2324, 2327, 2328, 2327, 2329, 2327, 2324, 2326,
        2331, 2333, 2334, 2335, 2334
    };

    // Any% Glitched and Glitchless Doorskip LTR
    vars.anySplitsDS = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  778,
        786,  785,  789,  790,  768,  771,  772,  771,  768,  770,
        777,  776,  769,  785,  784,  785,  789,  790,  780,  782,
        780,  769,  776,  781,  782,  781,  776,  777,  770,  768,
        771,  772,  771,  768,  770,  777,  268,  256,  258,  263,
        260,  263,  258,  264,  265,  266,  512,  513,  515,  517,
        515,  513,  512,  266,  265,  264,  258,  263,  262,  263,
        258,  256,  268,  10,   3,    5,    8,    16,   11,   12,
        11,   13,   14,   15,   14,   13,   11,   16,   8,    5,
        3,    10,   268,  256,  258,  267,  258,  264,  265,  266,
        512,  513,  515,  516,  518,  519,  518,  517,  516,  515,
        513,  512,  266,  265,  264,  258,  256,  269,  270,  1024,
        1025, 1280, 1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026,
        1028, 791,  268,  256,  269,  270,  1027, 1024, 1025, 1280,
        1281, 1280, 1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542,
        1543, 1542, 1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541,
        1542, 1543, 1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542,
        1546, 1814, 1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793,
        1798, 1799, 1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806,
        1810, 1809, 1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794,
        1811, 1793, 1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049,
        2048, 2049, 2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052,
        1805, 1808, 1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798,
        1793, 1811, 1794, 1817, 1802, 1817, 1794, 1811, 1793, 1795,
        1804, 1792, 1800, 2313, 2304, 2307, 2308, 2307, 2304, 2309,
        2310, 2309, 2314, 2317, 2316, 2318, 2316, 2320, 2321, 2322,
        2321, 2320, 2316, 2318, 2316, 2317, 2314, 2315, 2314, 2309,
        2314, 2317, 2319, 2330, 2331, 2333, 2332, 2336, 2324, 2325,
        2324, 2336, 2320, 2336, 2324, 2327, 2328, 2327, 2329, 2327,
        2324, 2326, 2331, 2333, 2334, 2335, 2334
    };

    // Rocket Launcher Non-Doorskip Non-LTR
    vars.rocketSplits = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  778,
        786,  785,  789,  790,  768,  771,  772,  771,  768,  770,
        777,  776,  769,  785,  789,  790,  780,  782,  780,  769,
        776,  781,  782,  781,  776,  777,  770,  768,  771,  772,
        771,  768,  770,  777,  268,  256,  258,  263,  260,  263,
        258,  264,  265,  266,  512,  513,  515,  517,  515,  513,
        512,  266,  265,  264,  258,  263,  262,  263,  258,  256,
        268,  10,   3,    5,    8,    16,   11,   12,   11,   13,
        14,   15,   14,   13,   11,   16,   8,    5,    3,    10,
        268,  256,  258,  267,  258,  264,  265,  266,  512,  513,
        515,  516,  518,  519,  518,  517,  516,  515,  513,  512,
        266,  265,  264,  258,  256,  269,  270,  1024, 1025, 1280,
        1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026, 1028, 791,
        268,  256,  269,  270,  1027, 1024, 1025, 1280, 1281, 1280,
        1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542, 1543, 1542,
        1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541, 1542, 1543,
        1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542, 1546, 1814,
        1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793, 1798, 1799,
        1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806, 1810, 1809,
        1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794, 1811, 1793,
        1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049, 2048, 2049,
        2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052, 1805, 1808,
        1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798, 1793, 1811,
        1794, 1817, 1802, 1817, 1794, 1811, 1793, 1798, 1800, 2313,
        2304, 2307, 2308, 2307, 2304, 2309, 2310, 2309, 2314, 2317,
        2316, 2318, 2316, 2320, 2321, 2322, 2321, 2320, 2316, 2318,
        2316, 2317, 2314, 2315, 2314, 2309, 2314, 2317, 2319, 2330,
        2331, 2333, 2332, 2336, 2324, 2325, 2324, 2336, 2320, 2336,
        2324, 2327, 2328, 2327, 2329, 2327, 2324, 2326, 2331, 2333,
        2334, 2335, 2334
    };

    // Rocket Launcher Doorskip LTR
    vars.rocketSplitsDS = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  778,
        786,  785,  789,  790,  768,  771,  772,  771,  768,  770,
        777,  776,  769,  785,  789,  790,  780,  782,  780,  769,
        776,  781,  782,  781,  776,  777,  770,  768,  771,  772,
        771,  768,  770,  777,  268,  256,  258,  263,  260,  263,
        258,  264,  265,  266,  512,  513,  515,  517,  515,  513,
        512,  266,  265,  264,  258,  263,  262,  263,  258,  256,
        268,  10,   3,    5,    8,    16,   11,   12,   11,   13,
        14,   15,   14,   13,   11,   16,   8,    5,    3,    10,
        268,  256,  258,  267,  258,  264,  265,  266,  512,  513,
        515,  516,  518,  519,  518,  517,  516,  515,  513,  512,
        266,  265,  264,  258,  256,  269,  270,  1024, 1025, 1280,
        1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026, 1028, 791,
        268,  256,  269,  270,  1027, 1024, 1025, 1280, 1281, 1280,
        1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542, 1543, 1542,
        1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541, 1542, 1543,
        1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542, 1546, 1814,
        1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793, 1798, 1799,
        1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806, 1810, 1809,
        1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794, 1811, 1793,
        1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049, 2048, 2049,
        2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052, 1805, 1808,
        1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798, 1793, 1811,
        1794, 1817, 1802, 1817, 1794, 1811, 1793, 1795, 1804, 1792,
        1800, 2313, 2304, 2307, 2308, 2307, 2304, 2309, 2310, 2309,
        2314, 2317, 2316, 2318, 2316, 2320, 2321, 2322, 2321, 2320,
        2316, 2318, 2316, 2317, 2314, 2315, 2314, 2309, 2314, 2317,
        2319, 2330, 2331, 2333, 2332, 2336, 2324, 2325, 2324, 2336,
        2320, 2336, 2324, 2327, 2328, 2327, 2329, 2327, 2324, 2326,
        2331, 2333, 2334, 2335, 2334
    };

    // Knife Only Non-Doorskip
    vars.knifeSplits = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  778,
        786,  785,  789,  790,  768,  771,  772,  771,  768,  770,
        777,  776,  769,  785,  789,  790,  780,  782,  780,  769,
        776,  781,  782,  781,  776,  777,  770,  768,  771,  772,
        771,  768,  770,  777,  268,  256,  258,  263,  260,  263,
        258,  264,  265,  266,  512,  513,  515,  517,  515,  513,
        512,  266,  265,  264,  258,  263,  262,  263,  258,  256,
        268,  10,   3,    5,    8,    16,   11,   12,   11,   13,
        14,   15,   14,   13,   11,   16,   8,    5,    3,    10,
        268,  256,  258,  267,  258,  264,  265,  266,  512,  513,
        515,  516,  518,  519,  518,  517,  516,  515,  513,  512,
        266,  265,  264,  258,  256,  269,  270,  1024, 1025, 1280,
        1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026, 1028, 791,
        268,  256,  269,  270,  1027, 1024, 1025, 1280, 1281, 1280,
        1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542, 1543, 1542,
        1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541, 1542, 1543,
        1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542, 1546, 1814,
        1815, 1814, 1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793,
        1798, 1799, 1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806,
        1810, 1809, 1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794,
        1811, 1793, 1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049,
        2048, 2049, 2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052,
        1805, 1808, 1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798,
        1793, 1811, 1794, 1817, 1802, 1817, 1794, 1811, 1793, 1798,
        1800, 2313, 2304, 2307, 2308, 2307, 2304, 2309, 2310, 2309,
        2314, 2317, 2316, 2318, 2316, 2320, 2321, 2322, 2321, 2320,
        2316, 2318, 2316, 2317, 2314, 2315, 2314, 2309, 2314, 2317,
        2319, 2330, 2331, 2333, 2332, 2336, 2324, 2325, 2324, 2336,
        2320, 2336, 2324, 2327, 2328, 2327, 2329, 2327, 2324, 2326,
        2331, 2333, 2334, 2335, 2334
    };

    // Knife Only Doorskip LTR
    vars.knifeSplitsDS = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  778,
        786,  785,  789,  790,  768,  771,  772,  771,  768,  770,
        777,  776,  769,  785,  789,  790,  780,  782,  780,  769,
        776,  781,  782,  781,  776,  777,  770,  768,  771,  772,
        771,  768,  770,  777,  268,  256,  258,  263,  260,  263,
        258,  264,  265,  266,  512,  513,  515,  517,  515,  513,
        512,  266,  265,  264,  258,  263,  262,  263,  258,  256,
        268,  10,   3,    5,    8,    16,   11,   12,   11,   13,
        14,   15,   14,   13,   11,   16,   8,    5,    3,    10,
        268,  256,  258,  267,  258,  264,  265,  266,  512,  513,
        515,  516,  518,  519,  518,  517,  516,  515,  513,  512,
        266,  265,  264,  258,  256,  269,  270,  1024, 1025, 1280,
        1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026, 1028, 791,
        268,  256,  269,  270,  1027, 1024, 1025, 1280, 1281, 1280,
        1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542, 1543, 1542,
        1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541, 1542, 1543,
        1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542, 1546, 1814,
        1815, 1814, 1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793,
        1798, 1799, 1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806,
        1810, 1809, 1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794,
        1811, 1793, 1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049,
        2048, 2049, 2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052,
        1805, 1808, 1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798,
        1793, 1811, 1794, 1817, 1802, 1817, 1794, 1811, 1793, 1795,
        1804, 1792, 1800, 2313, 2304, 2307, 2308, 2307, 2304, 2309,
        2310, 2309, 2314, 2317, 2316, 2318, 2316, 2320, 2321, 2322,
        2321, 2320, 2316, 2318, 2316, 2317, 2314, 2315, 2314, 2309,
        2314, 2317, 2319, 2330, 2331, 2333, 2332, 2336, 2324, 2325,
        2324, 2336, 2320, 2336, 2324, 2327, 2328, 2327, 2329, 2327,
        2324, 2326, 2331, 2333, 2334, 2335, 2334
    };

    // S-Rank Glitched and Glitchless Non-Doorskip Non-LTR
    vars.srankSplits = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  779,
        783,  778,  786,  785,  789,  790,  768,  771,  772,  771,
        768,  770,  777,  776,  769,  785,  784,  785,  789,  790,
        780,  782,  780,  769,  776,  781,  782,  781,  776,  777,
        770,  768,  771,  772,  771,  768,  770,  777,  268,  256,
        258,  263,  260,  263,  258,  264,  265,  266,  512,  513,
        515,  517,  515,  513,  512,  266,  265,  264,  258,  263,
        262,  263,  258,  256,  268,  10,   3,    5,    8,    16,
        11,   12,   11,   13,   14,   15,   14,   13,   11,   16,
        8,    5,    3,    2,    1,    0,    1,    2,    3,    10,
        268,  256,  258,  267,  258,  264,  265,  266,  512,  513,
        515,  516,  518,  519,  518,  517,  516,  515,  513,  512,
        266,  265,  264,  258,  256,  269,  270,  1024, 1025, 1280,
        1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026, 1028, 791,
        268,  256,  269,  270,  1027, 1024, 1025, 1280, 1281, 1280,
        1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542, 1543, 1542,
        1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541, 1542, 1543,
        1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542, 1546, 1814,
        1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793, 1798, 1799,
        1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806, 1810, 1809,
        1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794, 1811, 1793,
        1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049, 2048, 2049,
        2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052, 1805, 1808,
        1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798, 1793, 1811,
        1794, 1817, 1802, 1817, 1794, 1811, 1793, 1798, 1800, 2313,
        2304, 2307, 2308, 2307, 2304, 2309, 2310, 2309, 2314, 2317,
        2316, 2318, 2316, 2320, 2321, 2322, 2321, 2320, 2316, 2318,
        2316, 2317, 2314, 2315, 2314, 2309, 2314, 2317, 2319, 2330,
        2331, 2333, 2332, 2336, 2324, 2325, 2324, 2336, 2320, 2336,
        2324, 2327, 2328, 2327, 2329, 2327, 2324, 2326, 2331, 2333,
        2334, 2335, 2334
    };

    // S-Rank Glitchless Doorskip
    vars.srankSplitsDS = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  779,
        783,  778,  786,  785,  789,  790,  768,  771,  772,  771,
        768,  770,  777,  776,  769,  785,  784,  785,  789,  790,
        780,  782,  780,  769,  776,  781,  782,  781,  776,  777,
        770,  768,  771,  772,  771,  768,  770,  777,  268,  256,
        258,  263,  260,  263,  258,  264,  265,  266,  512,  513,
        515,  517,  515,  513,  512,  266,  265,  264,  258,  263,
        262,  263,  258,  256,  268,  10,   3,    5,    8,    16,
        11,   12,   11,   13,   14,   15,   14,   13,   11,   16,
        8,    5,    3,    2,    1,    0,    1,    2,    3,    10,
        268,  256,  258,  267,  258,  264,  265,  266,  512,  513,
        515,  516,  518,  519,  518,  517,  516,  515,  513,  512,
        266,  265,  264,  258,  256,  269,  270,  1024, 1025, 1280,
        1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026, 1028, 791,
        268,  256,  269,  270,  1027, 1024, 1025, 1280, 1281, 1280,
        1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542, 1543, 1542,
        1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541, 1542, 1543,
        1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542, 1546, 1814,
        1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793, 1798, 1799,
        1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806, 1810, 1809,
        1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794, 1811, 1793,
        1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049, 2048, 2049,
        2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052, 1805, 1808,
        1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798, 1793, 1811,
        1794, 1817, 1802, 1817, 1794, 1811, 1793, 1795, 1804, 1792,
        1800, 2313, 2304, 2307, 2308, 2307, 2304, 2309, 2310, 2309,
        2314, 2317, 2316, 2318, 2316, 2320, 2321, 2322, 2321, 2320,
        2316, 2318, 2316, 2317, 2314, 2315, 2314, 2309, 2314, 2317,
        2319, 2330, 2331, 2333, 2332, 2336, 2324, 2325, 2324, 2336,
        2320, 2336, 2324, 2327, 2328, 2327, 2329, 2327, 2324, 2326,
        2331, 2333, 2334, 2335, 2334
    };

    // S-Rank Glitched Doorskip
    vars.srankSplitsDSGL = new List<int>()
    {
        1,    2,    3,    5,    8,    9,    4,    9,    8,    5,
        3,    2,    3,    5,    8,    9,    8,    5,    3,    10,
        268,  256,  258,  263,  261,  263,  258,  263,  261,  263,
        258,  256,  269,  270,  1027, 1024, 1025, 1026, 1025, 1024,
        1027, 1029, 269,  256,  268,  777,  776,  769,  783,  779,
        783,  778,  786,  785,  789,  790,  768,  771,  772,  771,
        768,  770,  777,  776,  769,  783,  769,  785,  784,  785,
        789,  790,  780,  782,  780,  769,  776,  781,  782,  781,
        776,  777,  770,  768,  771,  772,  771,  768,  770,  777,
        268,  256,  258,  263,  260,  263,  258,  264,  265,  266,
        512,  513,  515,  517,  515,  513,  512,  266,  265,  264,
        258,  263,  262,  263,  258,  256,  268,  10,   3,    5,
        8,    16,   11,   12,   11,   13,   14,   15,   14,   13,
        11,   16,   8,    5,    3,    2,    1,    0,    1,    2,
        3,    10,   268,  256,  258,  267,  258,  264,  265,  266,
        512,  513,  515,  516,  518,  519,  518,  517,  516,  515,
        513,  512,  266,  265,  264,  258,  256,  269,  270,  1024,
        1025, 1280, 1025, 1024, 1025, 1026, 1025, 1024, 1025, 1026,
        1028, 791,  268,  256,  269,  270,  1027, 1024, 1025, 1280,
        1281, 1280, 1536, 1539, 1541, 1544, 1541, 1545, 1541, 1542,
        1543, 1542, 1541, 1545, 1541, 1539, 1540, 1539, 1536, 1541,
        1542, 1543, 1542, 1541, 1538, 1541, 1536, 1539, 1541, 1542,
        1546, 1814, 1815, 1804, 1792, 1800, 1792, 1804, 1795, 1793,
        1798, 1799, 1803, 1797, 1803, 1799, 1798, 1793, 1795, 1806,
        1810, 1809, 1804, 1809, 1810, 1806, 1795, 1793, 1811, 1794,
        1811, 1793, 1798, 1799, 1803, 1808, 1805, 2052, 2050, 2049,
        2048, 2049, 2050, 2049, 2048, 2049, 2048, 2049, 2050, 2052,
        1805, 1808, 1807, 1806, 1813, 1812, 1796, 1812, 1813, 1798,
        1793, 1811, 1794, 1817, 1802, 1817, 1794, 1811, 1793, 1795,
        1804, 1792, 1800, 2313, 2304, 2307, 2308, 2307, 2304, 2309,
        2310, 2309, 2314, 2317, 2316, 2318, 2316, 2320, 2321, 2322,
        2321, 2320, 2316, 2318, 2316, 2317, 2314, 2315, 2314, 2309,
        2314, 2317, 2319, 2330, 2331, 2333, 2332, 2336, 2324, 2325,
        2324, 2336, 2320, 2336, 2324, 2327, 2328, 2327, 2329, 2327,
        2324, 2326, 2331, 2333, 2334, 2335, 2334
    };

    // Read flags from indexed memory addresses
    vars.ReadFlag = (Func<IntPtr, int, bool>)((pointer, id) => {
        int idx = (id / 32);
        int bit = (id % 32);

        IntPtr address = IntPtr.Add(pointer, idx * 4);

        uint buffer = 0;
        memory.ReadValue<uint>(address, out buffer);

        int value = vars.isBigEndian ? (int)vars.SwapBytesInt(buffer) : (int)buffer;

        return ((value >> (31 - bit)) & 1) != 0;
    });

    // Log splits
    vars.LogSplit = (Action<string>)((text) => {
        vars.lastSplit = text;

        string log = timer.CurrentTime[timer.CurrentTimingMethod] + ": " + text;

        if (settings["logSplits"])
            File.AppendAllText(vars.logPath, log + System.Environment.NewLine);

        if (settings["debugSplits"])
            print("recvx split: " + log);
    });

    // Update split status
    vars.UpdateSplit = (Func<string, bool>)((split) => {
        if (!vars.splits[split] && settings[split])
        {
            vars.splits[split] = true;
            vars.LogSplit(split);
            return true;
        }

        return false;
    });

    // Update item split status
    vars.UpdateItemSplit = (Func<byte, bool>)((id) => {
        if (!vars.itemSplits[id] && settings[vars.itemIndex[id]])
        {
            vars.itemSplits[id] = true;
            vars.LogSplit(vars.itemIndex[id] + " (" + id.ToString() + ") (" + settings[vars.itemIndex[id]].ToString() + ")");
            return true;
        }

        return false;
    });

    // Update door split status
    vars.UpdateDoorSplit = (Func<int, int, bool>)((id, prevId) => {
        if (
             settings["basicSplits"] || 
            (settings["anySplits"]       && vars.anySplits[vars.doorsIndex] == id)      ||
            (settings["anySplitsDS"]     && vars.anySplitsDS[vars.doorsIndex] == id)    ||
            (settings["rocketSplits"]    && vars.rocketSplits[vars.doorsIndex] == id)   ||
            (settings["rocketSplitsDS"]  && vars.rocketSplitsDS[vars.doorsIndex] == id) ||
            (settings["knifeSplits"]     && vars.knifeSplits[vars.doorsIndex] == id)    ||
            (settings["knifeSplitsDS"]   && vars.knifeSplitsDS[vars.doorsIndex] == id)  ||
            (settings["srankSplits"]     && vars.srankSplits[vars.doorsIndex] == id)    ||
            (settings["srankSplitsDS"]   && vars.srankSplitsDS[vars.doorsIndex] == id)  ||
            (settings["srankSplitsDSGL"] && vars.srankSplitsDSGL[vars.doorsIndex] == id)
        )
        {
            vars.LogSplit("Door " + prevId + " to " + id);
            vars.doorsIndex++;
            return true;
        }

        return false;
    });

    // Update killed split status
    vars.UpdateKilledSplit = (Func<short, bool>)((id) => {
        if (!vars.killedSplits[id] && settings[vars.killedIndex[id]])
        {
            vars.killedSplits[id] = true;
            vars.LogSplit(vars.killedIndex[id] + " (" + id.ToString() + ")");
            return true;
        }

        return false;
    });

    // Update events split status
    vars.UpdateEventsSplit = (Func<short, bool>)((id) => {
        if (!vars.eventsSplits[id] && settings[vars.eventsIndex[id]])
        {
            vars.eventsSplits[id] = true;
            vars.LogSplit(vars.eventsIndex[id] + " (" + id.ToString() + ")");
            return true;
        }

        return false;
    });

    // Sets memory pointers for the detected game release
    vars.UpdatePointers = (Action) (() => {
        string product = (string)vars.productCode ?? String.Empty;
        if (vars.productPointers.ContainsKey(product))
        {
            timePtr = IntPtr.Add(basePointer, vars.productPointers[product]["timePtr"]);
            roomPtr = IntPtr.Add(basePointer, vars.productPointers[product]["roomPtr"]);
            screenPtr = IntPtr.Add(basePointer, vars.productPointers[product]["screenPtr"]);
            healthPtr = IntPtr.Add(basePointer, vars.productPointers[product]["healthPtr"]);
            statusPtr = IntPtr.Add(basePointer, vars.productPointers[product]["statusPtr"]);
            characterPtr = IntPtr.Add(basePointer, vars.productPointers[product]["characterPtr"]);
            inventoryPtr = IntPtr.Add(basePointer, vars.productPointers[product]["inventoryPtr"]);
            doorFlagPtr = IntPtr.Add(basePointer, vars.productPointers[product]["doorFlagPtr"]);
            enemyPtr = IntPtr.Add(basePointer, vars.productPointers[product]["enemyPtr"]);
            killedPtr = IntPtr.Add(basePointer, vars.productPointers[product]["killedPtr"]);
            eventsPtr = IntPtr.Add(basePointer, vars.productPointers[product]["eventsPtr"]);
        }
        else
        {
            timePtr = IntPtr.Zero;
            roomPtr = IntPtr.Zero;
            screenPtr = IntPtr.Zero;
            healthPtr = IntPtr.Zero;
            statusPtr = IntPtr.Zero;
            characterPtr = IntPtr.Zero;
            inventoryPtr = IntPtr.Zero;
            doorFlagPtr = IntPtr.Zero;
            enemyPtr = IntPtr.Zero;
            killedPtr = IntPtr.Zero;
            eventsPtr = IntPtr.Zero;
        }
    });

    // Set emulator base pointer and product pointer
    vars.UpdateProcess = (Action) (() => {
        switch ((string)game.ProcessName.ToLower())
        {
            case "dolphin":
                try
                {
                    // https://github.com/Jujstme/Autosplitters/blob/e6adbd04390cabd0a594f0b0ad47a64884a64b03/Template_GameCube.asl#L37
                    basePointer = game.MemoryPages(true).First(p => p.Type == MemPageType.MEM_MAPPED && p.State == MemPageState.MEM_COMMIT && (int)p.RegionSize == 0x2000000).BaseAddress;
                }
                catch
                {
                    basePointer = IntPtr.Zero;
                }

                productPointer = basePointer;

                vars.isBigEndian = true;
                vars.console = "GCN";

                // Prevent debugger spam
                if (basePointer != IntPtr.Zero)
                {
                    print("dolphin basePointer: " + basePointer.ToString("X8"));
                    print("dolphin productPointer: " + productPointer.ToString("X8"));
                }
                break;

            case "pcsx2": // PCSX2 1.6
                basePointer = new IntPtr(0x20000000);
                productPointer = basePointer;

                vars.isBigEndian = false;
                vars.console = "PS2";
                
                print("pcsx2 basePointer: " + basePointer.ToString("X8"));
                print("pcsx2 productPointer: " + productPointer.ToString("X8"));
                break;

            case "pcsx2-qtx64": // PCSX2 1.7+
            case "pcsx2-qt": // PCSX2 2.4.0+
                IntPtr baseAddress = game.MainModule.BaseAddress;

                // Get pointer to EEmem function from PE header
                int directory = memory.ReadValue<int>(IntPtr.Add(baseAddress, 0x100));
                int functions = memory.ReadValue<int>(IntPtr.Add(baseAddress, directory + 0x1C));

                // TODO: Locate function by name instead of index
                int eemem = memory.ReadValue<int>(IntPtr.Add(baseAddress, functions + 0x04));

                basePointer = (IntPtr)(memory.ReadValue<long>(IntPtr.Add(baseAddress, eemem)));
                productPointer = basePointer;

                vars.isBigEndian = false;
                vars.console = "PS2";
                 
                print("pcsx2-qtx64 basePointer: " + basePointer.ToString("X16"));
                print("pcsx2-qtx64 productPointer: " + productPointer.ToString("X16"));
                break;

            case "mygamerecomp":
            case "mygamerecomp_jp":
            case "mygamerecomp_us":
                // The PC port: its exported g_cv_livesplit, found by name in the
                // executable's export table (no memory scan).
                basePointer = IntPtr.Zero;
                productPointer = IntPtr.Zero;
                vars.isBigEndian = true;
                vars.console = "PS3";
                try
                {
                    IntPtr rcModule = game.MainModule.BaseAddress;
                    int rcPe = memory.ReadValue<int>(IntPtr.Add(rcModule, 0x3C));
                    int rcExports = memory.ReadValue<int>(IntPtr.Add(rcModule, rcPe + 0x88));
                    int rcCount = memory.ReadValue<int>(IntPtr.Add(rcModule, rcExports + 0x18));
                    int rcFunctions = memory.ReadValue<int>(IntPtr.Add(rcModule, rcExports + 0x1C));
                    int rcNames = memory.ReadValue<int>(IntPtr.Add(rcModule, rcExports + 0x20));
                    int rcOrdinals = memory.ReadValue<int>(IntPtr.Add(rcModule, rcExports + 0x24));
                    for (int rcI = 0; rcI < rcCount; rcI++)
                    {
                        int rcName = memory.ReadValue<int>(IntPtr.Add(rcModule, rcNames + rcI * 4));
                        if (memory.ReadString(IntPtr.Add(rcModule, rcName), 32) != "g_cv_livesplit")
                            continue;
                        ushort rcOrdinal = memory.ReadValue<ushort>(IntPtr.Add(rcModule, rcOrdinals + rcI * 2));
                        IntPtr rcMarker = IntPtr.Add(rcModule, memory.ReadValue<int>(IntPtr.Add(rcModule, rcFunctions + rcOrdinal * 4)));
                        long rcGuest = memory.ReadValue<long>(IntPtr.Add(rcMarker, 16));
                        // Zero until the game has reserved its memory: try again next update.
                        if (memory.ReadString(rcMarker, 13) == "PS3RECOMP-CVX" && rcGuest != 0)
                        {
                            basePointer = new IntPtr(rcGuest);
                            productPointer = IntPtr.Add(rcMarker, 24);
                        }
                        break;
                    }
                }
                catch
                {
                    basePointer = IntPtr.Zero;
                    productPointer = IntPtr.Zero;
                }

                print("ps3recomp basePointer: " + basePointer.ToString("X16"));
                print("ps3recomp productPointer: " + productPointer.ToString("X16"));
                break;

            case "rpcs3":
                basePointer = new IntPtr(0x300000000);
                productPointer = IntPtr.Add(basePointer, 0x10010251);

                vars.isBigEndian = true;
                vars.console = "PS3";
                
                print("rpcs3 basePointer: " + basePointer.ToString("X16"));
                print("rpcs3 productPointer: " + productPointer.ToString("X16"));
                break;

            default:
                basePointer = IntPtr.Zero;
                productPointer = IntPtr.Zero;

                vars.isBigEndian = false;
                vars.console = String.Empty;
                
                print("default basePointer: " + basePointer.ToString("X8"));
                print("default productPointer: " + productPointer.ToString("X8"));
                break;
        }
    });

    // Detects if the game release has changed
    vars.UpdateProduct = (Action) (() => {
        string productCode = String.Empty;

        // Update the emulator base pointers if the product pointer is 0
        if (productPointer == IntPtr.Zero)
        {
            vars.UpdateProcess();
        }
        else
        {
            // Read the product code
            productCode = vars.ReadProduct(productPointer);

            // Update product code and memory pointers if the game release has changed
            if (vars.productCode != productCode)
            {
                // Update the base emulator pointers
                vars.UpdateProcess();

                // Re-read product code since productPointer will be different if the emulator changed
                vars.productCode = vars.ReadProduct(productPointer);

                if (!String.IsNullOrEmpty(vars.productCode))
                    print("productCode: " + vars.productCode);

                // Update game memory pointers
                vars.UpdatePointers();
            }
        }
    });

    // Read game release product code from memory
    vars.ReadProduct = (Func<IntPtr, string>)((pointer) => {
        switch ((string)game.ProcessName.ToLower())
        {
            case "dolphin":
                return memory.ReadString(pointer, 6);
            case "pcsx2":
            case "pcsx2-qt":
            case "pcsx2-qtx64":
                // Check all possible product code offsets
                foreach (int offset in vars.productOffsets["PS2"])
                {
                    string product = memory.ReadString(IntPtr.Add(pointer, offset), 11);
                    if (!String.IsNullOrEmpty(product) && vars.productPointers.ContainsKey(product))
                        return product;
                }
                break;
            case "rpcs3":
            case "mygamerecomp":
            case "mygamerecomp_jp":
            case "mygamerecomp_us":
                return memory.ReadString(pointer, 9);
        }

        return String.Empty;
    });

    // Updates game values
    vars.UpdateValues = (Action) (() => {
        uint time = 0; // In Game Timer
        ushort room = 0; // Room the character is in
        byte screen = 0; // Current game screen
        uint health = 0; // Character health
        byte status = 0; // Character status info
        byte character = 0; // Character ID (0 Claire, 1 Chris, 2 Steve, 3 Wesker)
        byte door = 0; // Current status of door for load remover time
        ushort enemy = 0; // Enemy type

        // Read values from memory
        memory.ReadValue<uint>(timePtr, out time);
        memory.ReadValue<ushort>(roomPtr, out room);
        memory.ReadValue<byte>(screenPtr, out screen);
        memory.ReadValue<uint>(healthPtr, out health);
        memory.ReadValue<byte>(statusPtr, out status);
        memory.ReadValue<byte>(characterPtr, out character);
        memory.ReadValue<byte>(doorFlagPtr, out door);
        memory.ReadValue<ushort>(IntPtr.Add(enemyPtr, 0x4), out enemy);

        current.slot = 0; // Inventory slot number of the equipped item
        current.ammo = 0; // Ammo count for the equipped weapon
        current.time = vars.isBigEndian ? (int)vars.SwapBytesInt(time) : (int)time;
        current.room = vars.SwapBytes(room); // Room bytes always need to be swapped
        current.screen = screen;
        current.health = vars.isBigEndian ? (int)vars.SwapBytesInt(health) : (int)health;
        current.status = "Normal";
        current.character = character;
        current.inventory = new byte[11]; // Current characters inventory
        current.door = door;
        current.enemy = vars.isBigEndian ? (short)vars.SwapBytes(enemy) : (short)enemy;
        current.killed = new bool[vars.killedSplits.Count]; // Killed enemy flags
        current.events = new bool[vars.eventsSplits.Count]; // Events flags

        // Check status for gassed or poison flags
        if ((status & 0x20) != 0)
            current.status = "Gassed";
        else if ((status & 0x08) != 0)
            current.status = "Poison";

        int index = -1; // Inventory array index

        // Pointer to the current characters inventory table
        IntPtr pointer = IntPtr.Add(inventoryPtr, (character * 0x40));

        // Read inventory of the current character
        for (int i = 0; i < 12; ++i)
        {
            uint item = 0; // Inventory entry

            // Read inventory entry from memory
            memory.ReadValue<uint>(pointer, out item);

            // Move pointer to the next entry
            pointer = IntPtr.Add(pointer, 0x4);

            // Convert to little endian if the console uses big endian
            item = vars.isBigEndian ? vars.SwapBytesInt(item) : item;

            // First entry is the slot number of the equipped item
            if (i == 0)
            {
                current.slot = item;
                continue;
            }

            // Remaing entries are inventory items
            byte[] bytes = BitConverter.GetBytes(item);

            // Set inventory index to the item ID
            current.inventory[++index] = bytes[2];

            // Read quantity if this item is equipped
            if (current.slot == (index + 1))
                current.ammo = BitConverter.ToInt16(bytes, 0);
        }

        foreach(int i in vars.killedMap.Keys) {
            current.killed[i] = vars.ReadFlag(killedPtr, vars.killedMap[i]);
        }

        foreach(int i in vars.eventsMap.Keys)
            current.events[i] = vars.ReadFlag(eventsPtr, vars.eventsMap[i]);
    });

    // Initialise values
    vars.UpdateProduct();
    vars.UpdateValues();
}

start
{
    // Start when IGT is 0, in the Prison Cell room and the 1st item slot contains the Lighter
    return (current.time == 0 && current.room == 0 && current.inventory[0] == 0x37);
}

update
{
    vars.UpdateProduct();
    vars.UpdateValues();

    if (timer.CurrentPhase == TimerPhase.NotRunning)
    {
        vars.ResetSplits();
        vars.ResetDoorSplits();
        vars.ResetItemSplits();
        vars.ResetKilledSplits();
        vars.ResetEventsSplits();
    }
}

split
{
    bool split = false;

    // End Game Rank Screen
    if (current.screen == 0x04)
        return vars.UpdateSplit("endGame");

    // Split when an inventory entry changes
    for (int i = 0; i < current.inventory.Length; i++)
    {
        byte item = current.inventory[i];

        // Continue if the entry has not changed
        if (item == old.inventory[i]) continue;

        switch (item)
        {
            // Items picked up more than once are tracked by the room they are found in
            case 0x08: // Combat Knife
                if (current.room == 0x0000) // Prison B1 Cells
                    // Prevents splitting when changing to Chris if the knife is skipped in the prison cells
                    return vars.UpdateSplit("combatKnife");
                break;

            case 0x22: // Sub Machine Gun
                if (current.room == 0x030A) // Military Training Facility Warehouse
                    return vars.UpdateSplit("subMachineGun");

                if (current.room == 0x0716) // Military Training Facility B3 Cave
                    return vars.UpdateSplit("subMachineGunChris");
                break;

            case 0x47: // Air Force Proof
                if (current.room == 0x0207) // Private Residence 4F Private Study
                    return vars.UpdateSplit("airForceProof");

                if (current.room == 0x0801) // Airport Bridge
                    return vars.UpdateSplit("airForceProofChris");
                break;

            case 0x4E: // Music Box Plate
                if (current.room == 0x0205) // Private Residence 2F Alexias Bedroom
                    return vars.UpdateSplit("musicBoxPlate");

                if (current.room == 0x0918) // Antartic Transport Terminal B4 Alexias Bedroom
                    return vars.UpdateSplit("musicBoxPlateChris");
                break;

            case 0x58: // Octo Valve Handle
                if (current.room == 0x0602) // Antartic Transport Terminal B1 Tool Storeroom
                    return vars.UpdateSplit("octaValveHandle");

                if (current.room == 0x0906) // Antartic Transport Terminal B1 Mining Room
                    return vars.UpdateSplit("octaValveHandleChris");
                break;

            case 0x38: // Eagle Plate
                if (current.room == 0x0304) // Military Training Facility 1F Diorama Room
                    return vars.UpdateSplit("eaglePlateDiorama");

                if (current.room == 0x0106) // Palace 1F Umbrella Meeting Room
                    return vars.UpdateSplit("eaglePlateUmbrella");

                if (current.room == 0x070A) // Military Training Facility B2 Water Pool
                    return vars.UpdateSplit("eaglePlateWaterPool");
                break;

            default: // Split the first time an item is added to the inventory
                if (vars.itemSplits.ContainsKey(item))
                    split = split || vars.UpdateItemSplit(item);
                break;
        }
    }

    // Change to the next room
    if (current.room != old.room)
    {
        if (vars.doorsEnabled)
            split = split || vars.UpdateDoorSplit(current.room, old.room);
        else
        {
            if (current.room == 0x0501) // Transport Plane Cargo Hold
                split = split || vars.UpdateSplit("tyrantPlane");

            if (current.room == 0x0600) // Antartic Transport Terminal B1 Silo
                split = split || vars.UpdateSplit("antarcticaBase");

            if (current.room == 0x060A) // Antartic Transport Terminal 1F Helipad
                split = split || vars.UpdateSplit("nosferatuFight");

            if (current.room == 0x0716) // Military Training Facility B3 Cave
                split = split || vars.UpdateSplit("startChris");
        }
    }

    if (current.enemy != old.enemy && current.enemy == 13)
        split = split || vars.UpdateSplit("alexia2Spawned");

    foreach(int i in vars.killedMap.Keys)
        if (current.killed[i] != old.killed[i] && current.killed[i])
            split = split || vars.UpdateKilledSplit(vars.killedMap[i]);

    foreach(int i in vars.eventsMap.Keys)
        if (current.events[i] != old.events[i] && current.events[i])
            split = split || vars.UpdateEventsSplit(vars.eventsMap[i]);

    return split;
}

gameTime
{
    if (!settings["LRT"])
        // Seconds = IGT / 60
        return TimeSpan.FromSeconds(current.time / 60.0);
}

isLoading
{
    return settings["LRT"] ? vars.BitCheck(current.door, 0) : true;
}