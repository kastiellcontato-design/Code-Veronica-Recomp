/* rsx_texture_pack.h -- texture mods: the game's textures written out as PNG
 * files, and PNG/DDS files used in their place.
 *
 * A texture is known by a 64-bit hash of its guest bytes (with its size and
 * format), and every file name starts with that hash in 16 hex digits:
 * "<hash>_<w>x<h>_<format>.png". A replacement may be any size; it keeps only
 * the hash at the start of its name. The files are only listed at start
 * (names, not data), and each one is decoded when the game first shows its
 * texture, so a pack of tens of gigabytes costs no more than the textures on
 * screen.
 *
 * Environment (set by the launcher):
 *   TEXTURE_DIR      the mods folder (UTF-8); default "textures" beside the exe
 *   TEXTURE_GAME     the game's code: <dir>\<code> is used when it exists, so
 *                    one folder can hold a pack for each version
 *   TEXTURE_REPLACE  1: use the PNG/DDS files in place of the textures
 *   TEXTURE_DUMP     1: write each new texture to the dump folder
 *   TEXTURE_DUMP_DIR the dump folder (UTF-8); default <dir>\<code>\dump
 * Both switches are read again every half second.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct texpack_image {
    uint32_t format, opaque;          /* DXGI format; DDS opaque-alpha metadata */
    uint32_t row_bytes[16], rows[16];
    uint32_t w, h, levels;          /* levels: the full mip chain, made from the PNG */
    uint8_t* data;                  /* native BC blocks or pixels, levels one after another */
    size_t level_off[16];
    size_t bytes;
} texpack_image;

/* DDS opaque-alpha metadata applies to every component sourcing alpha.
 * D3D12 component selectors: 3 = memory alpha, 5 = constant one. */
static inline uint32_t texpack_alpha_mapping(uint32_t mapping,int opaque) {
    if(opaque)for(uint32_t c=0;c<4;c++)if(((mapping>>(c*3))&7)==3)
        mapping=(mapping&~(7u<<(c*3)))|(5u<<(c*3));
    return mapping;
}
int texpack_replace_on(void);
int texpack_dump_on(void);
/* The switches were just changed: read them again now, not within half a second. */
void texpack_flags_changed(void);
/* Set the dump destination without restarting or initializing the mod index.
 * Queued images retain their destination; changing it resets per-folder dedup. */
int texpack_set_dump_directory(const wchar_t* directory);
/* Changes only when the output folder changes: cached GPU textures re-dump. */
uint32_t texpack_dump_generation(void);
void texpack_set_dump_format(int dds);
int texpack_dump_is_dds(void);
/* Owned tightly packed original BC1/2/3 blocks, exported without decoding. */
void texpack_dump_dds(uint64_t hash,uint32_t w,uint32_t h,const char* name,uint32_t format,uint8_t* data,size_t bytes);
uint64_t texpack_hash(const void* p, size_t n, uint32_t w, uint32_t h, uint32_t fmt);

/* -1: the folder is still being listed; 0: no replacement; 1: there is one. */
int texpack_lookup(uint64_t hash);
/* Queue its decode (a listed hash only). */
void texpack_request(uint64_t hash);
/* The next decode that finished: 1 with *img (free it with texpack_free), -1
 * if its file could not be read, 0 when none is waiting. */
int texpack_next(uint64_t* hash, texpack_image** img);
void texpack_free(texpack_image* img);

/* 1 the first time a hash is asked about (it is then marked written). */
int texpack_dump_wanted(uint64_t hash);
/* Write rgba (w*h*4, malloc'd; the writer frees it) as <hash>_<w>x<h>_<name>.png. */
void texpack_dump(uint64_t hash, uint32_t w, uint32_t h, const char* fmt_name, uint8_t* rgba);
/* BC1/BC2/BC3 (bc = 1, 2, 3) blocks, rows of blocks pitch bytes apart, to RGBA8. */
void texpack_decode_bc(int bc, const uint8_t* blocks, size_t pitch, uint32_t w, uint32_t h, uint8_t* rgba);

#ifdef __cplusplus
}
#endif
