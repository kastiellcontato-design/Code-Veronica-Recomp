#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
namespace veronica_pkg {
struct Progress { uint64_t done=0,total=0; std::wstring file; };
struct Info { std::string title_id,version; uint64_t bytes=0;unsigned files=0; };
using Notify=std::function<void(const Progress&)>;
// Read-only input. No license creation or executable decryption.
Info inspect(const std::filesystem::path& pkg);
std::filesystem::path extract(const std::filesystem::path& pkg,
 const std::filesystem::path& parent,std::atomic<bool>& cancel,const Notify& notify);
}
