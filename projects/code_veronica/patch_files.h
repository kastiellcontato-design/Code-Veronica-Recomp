/* patch_files.h -- patches for this game read from a folder of patch files in
 * RPCS3's format (patch.yml), for the launcher's Patches page.
 *
 * The game here is recompiled ahead of time, so a patch can only change what
 * the recompiled code still reads from memory: data (values, text, tables)
 * is written as the patch says, while an instruction change only works where
 * that exact change was lifted into this build as a switchable site (see
 * runtime/patches/ps3_patch.c). Other code patches are listed but cannot be
 * turned on.
 */
#pragma once
#include <string>
#include <vector>

struct FilePatch {
    std::wstring title, author, notes, file;   /* file: the name, no folder */
    std::string id;     /* stable across runs: the settings key and the runtime's */
    std::string spec;   /* "addr:hexbytes,..." for ps3_patch_writes */
    enum { OK, CODE, TYPE } status = OK;   /* CODE: changes code not lifted here; TYPE: a kind we cannot do */
    std::wstring bad_type;                 /* for TYPE */
};

/* Every patch for this game (its PPU hash, or its serial and version listed)
 * in the *.yml / *.yaml files of `folder` and its subfolders. `files`: how
 * many files were read. */
std::vector<FilePatch> patch_files_scan(const std::wstring& folder, int* files);
