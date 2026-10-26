#pragma once
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace cv_trophies {
struct Trophy {int id=0,grade=0;bool hidden=false;std::wstring name,description;std::vector<unsigned char> icon;};
struct Catalog {std::string commid;std::wstring title;std::vector<Trophy> items;};
struct Snapshot {std::shared_ptr<const Catalog> catalog;std::array<uint64_t,128> unlocked{};std::wstring error;bool enabled=true,notifications=true;};
Snapshot view(const std::filesystem::path& game,int language,bool refresh=false);
void options(bool enabled,bool notifications,int language);
void start(const std::filesystem::path& game,int language,bool enabled,bool notifications);
bool reset();
bool has_unlocked();
// Testable storage override; never invoked by the launcher UI.
void storage_for_test(const std::filesystem::path& directory);
}
