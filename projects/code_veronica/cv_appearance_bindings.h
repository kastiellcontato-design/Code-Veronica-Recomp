#pragma once
// Matched against each region's own lift. Actor layouts and room-task bits
// are shared; absolute data pointers and guest entry points are not.
#if defined(CV_REGION_JP)
constexpr uint32_t kSys=0x00A79C34,kPlayer=0x00A79C3C;
constexpr uint32_t kPlayerArchive=0x00AB01A0,kEntityPool=0x00C0A7F8,kEndian=0x00BB34E0;
#elif defined(CV_REGION_US)
constexpr uint32_t kSys=0x00A79BD0,kPlayer=0x00A79BD8;
constexpr uint32_t kPlayerArchive=0x00AB00E4,kEntityPool=0x00C0A778,kEndian=0x00BB3460;
#define func_001DBCA0 func_001DBB20
#define cv_original_001DBCA0 cv_original_001DBB20
#define func_001DD208 func_001DD088
#define func_001DC3A4 func_001DC224
#define func_00197FC8 func_00198008
#define func_001E2324 func_001E2170
#define func_00031E14 func_00031E34
#define func_00167350 func_001673A0
#define func_000322AC func_000322CC
#define func_0003F7A4 func_0003F7FC
#define func_0003ECC0 func_0003ED18
#define cv_original_0003ECC0 cv_original_0003ED18
#define func_0004A3F8 func_0004A450
#define cv_original_0004A3F8 cv_original_0004A450
#else
constexpr uint32_t kSys=0x00A89BD0,kPlayer=0x00A89BD8;
constexpr uint32_t kPlayerArchive=0x00AC0430,kEntityPool=0x00C1AA78,kEndian=0x00BC3760;
#define func_001DBCA0 func_001DD940
#define cv_original_001DBCA0 cv_original_001DD940
#define func_001DD208 func_001DEEA8
#define func_001DC3A4 func_001DE044
#define func_00197FC8 func_001999A8
#define func_001E2324 func_001E410C
#define func_00031E14 func_000322A4
#define func_00167350 func_00168C98
#define func_000322AC func_0003273C
#define func_0003F7A4 func_0003FBFC
#define func_0003ECC0 func_0003F118
#define cv_original_0003ECC0 cv_original_0003F118
#define func_0004A3F8 func_0004A850
#define cv_original_0004A3F8 cv_original_0004A850
#endif
