#pragma once
namespace cv_appearance {
struct AppearanceEntry { const char* name;const char* archive;const char* hands;unsigned file_id;bool male_motion=false; };
inline constexpr AppearanceEntry catalog[]={
 {"CLAIRE - ORIGINAL","pl00","pl00w00",10},
 {"CLAIRE - ALTERNATE","pl04","pl00w00",14},
 {"CHRIS","pl01","pl01w00",11,true},
 {"STEVE","pl02","pl02w00",12,true},
 {"WESKER","pl03","pl03w00",13,true},
 {"ZOMBIE - PRISONER 1","cvskin05",nullptr,14,true},
 {"ZOMBIE - PRISONER 2","cvskin06",nullptr,14,true},
 {"ZOMBIE - SOLDIER 1","cvskin07",nullptr,14,true},
 {"ZOMBIE - SOLDIER 2","cvskin08",nullptr,14,true},
 {"ZOMBIE - SOLDIER 3","cvskin09",nullptr,14,true},
 {"ZOMBIE - LAB COAT 1","cvskin10",nullptr,14,true},
 {"ZOMBIE - LAB COAT 2","cvskin11",nullptr,14,true},
 {"ZOMBIE - WORKER 1","cvskin12",nullptr,14,true},
 {"ZOMBIE - WORKER 2","cvskin13",nullptr,14,true},
 {"ZOMBIE - FROZEN 1","cvskin14",nullptr,14,true},
 {"ZOMBIE - FROZEN 2","cvskin15",nullptr,14,true},
 {"ZOMBIE - SOLDIER 4","cvskin16",nullptr,14,true},
 {"ZOMBIE - PRISONER 3","cvskin17",nullptr,14,true},
 {"ZOMBIE - SOLDIER 5","cvskin18",nullptr,14,true},
 {"ZOMBIE - SOLDIER 6","cvskin19",nullptr,14,true},
 {"ZOMBIE - SOLDIER 7","cvskin20",nullptr,14,true},
 {"ZOMBIE - BASE STAFF 1","cvskin21",nullptr,14,true},
 {"ZOMBIE - BASE STAFF 2","cvskin22",nullptr,14,true},
 {"ZOMBIE - CIVILIAN 1","cvskin23",nullptr,14,true},
 {"ZOMBIE - CIVILIAN 2","cvskin24",nullptr,14,true},
 {"ZOMBIE - HAZMAT SUIT","cvskin25",nullptr,14,true},
 {"ZOMBIE - DARK UNIFORM 1","cvskin26",nullptr,14,true},
 {"ZOMBIE - DARK UNIFORM 2","cvskin27",nullptr,14,true},
 {"ZOMBIE - BASE STAFF 3","cvskin28",nullptr,14,true},
 {"ZOMBIE - ANATOMIST","cvskin29",nullptr,14,true},
 {"ZOMBIE - PRISONER (SCENE)","cvskin30",nullptr,14,true},
 {"ALEXIA - FIRST FORM","cvskin31",nullptr,14,false},
 {"ALEXIA - HUMAN","cvskin32","pl00w00",14,false},
};
inline constexpr unsigned appearance_count=sizeof(catalog)/sizeof(catalog[0]);
inline constexpr unsigned original_appearance_count=5;
}
