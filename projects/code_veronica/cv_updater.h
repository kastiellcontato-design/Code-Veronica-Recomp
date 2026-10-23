/* cv_updater.h -- the launcher's automatic update from the GitHub releases.
 *
 * check:    GET releases/latest (or CV_UPDATE_URL: another URL or a local
 *           JSON file, for tests), take its tag as the version and its first
 *           .zip asset as the package.
 * download: the package into %TEMP%\CodeVeronicaUpdate, with progress, its
 *           size and (when GitHub gives one) its SHA-256 checked.
 * install:  unpack it (Windows' own tar.exe) and copy its files over the
 *           port's folder. A running executable or DLL cannot be overwritten
 *           but can be renamed, so each file replaced is first renamed to
 *           <name>.cvupd-old; any failure puts every file back. The caller
 *           then starts the new executable and leaves; on the next start
 *           cv_update_cleanup() deletes the old copies.
 *
 * Never written, even when a package carries them: settings (*.ini), saves
 * and game data (gamedata, dev_hdd0, saves), caches (shader_cache,
 * eboot_cache), screenshots, mods and textures, the game folder (JOGO), logs.
 * Files the package does not have are left alone.
 */
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cv_update {

const wchar_t* const k_default_url =
    L"https://api.github.com/repos/kastiellcontato-design/re-cvx-ps3recomp/releases/latest";

/* What went wrong, for the launcher to name in the player's language. */
enum Error { OK, ERR_NETWORK, ERR_NO_RELEASE, ERR_NO_PACKAGE, ERR_DOWNLOAD, ERR_CANCELED, ERR_CHECKSUM,
             ERR_EXTRACT, ERR_BAD_PACKAGE, ERR_WRITE };

struct Release {
    std::wstring tag, name, notes;         /* tag_name ("v1.1"), name, body */
    std::wstring asset_url, asset_name;    /* the package: the first .zip asset */
    uint64_t asset_size = 0;
    std::string sha256;                    /* lower-case hex from the asset's digest; "" if none */
};

struct Progress {
    std::atomic<uint64_t> done{0}, total{0};
    std::atomic<bool> cancel{false};
};

/* <0, 0, >0 like strcmp, numerically per dotted part ("v1.10" > "v1.9");
 * a leading v and a suffix after '-' or '+' are ignored. */
int compare_versions(const std::wstring& a, const std::wstring& b);
/* True for a relative path the update must never write (see above). */
bool is_protected(const std::filesystem::path& relative);
/* A releases/latest JSON document into *out; false if it has no tag. */
bool parse_release(const std::string& json, Release* out);

/* CV_UPDATE_URL if set, else k_default_url. */
std::wstring release_url();
/* Fetches the latest release; OK with *out filled, or the error. */
Error fetch_latest(Release* out);
/* The package into dest_dir; *zip is the file written. */
Error download(const Release& r, const std::filesystem::path& dest_dir, Progress* p, std::filesystem::path* zip);
/* Unpacks zip and copies it over install_dir (the folder of exe_name). */
Error install(const std::filesystem::path& zip, const std::filesystem::path& install_dir, const std::wstring& exe_name);
/* Deletes the .cvupd-old copies an earlier install left, once the process
 * in CV_UPDATE_DONE (the one that installed) has ended. */
void cleanup(const std::filesystem::path& install_dir);
/* Where downloads and unpacked packages go. */
std::filesystem::path work_dir();

}  // namespace cv_update
