/* patch_files.cpp -- see patch_files.h.
 *
 * RPCS3's patch.yml, as far as a patch list needs it:
 *
 *   Version: 1.2
 *   Anchors:
 *     some_name: &some_name
 *       - [ be32, 0x0001000, 0x60000000 ]
 *   PPU-df822d7d47de09c5672201db8cc633009037f54f:
 *     "Remove Door Animations":
 *       Games:
 *         "Resident Evil Code: Veronica X HD":
 *           NPEB00553: [ 01.00 ]
 *       Author: "Kapdap"
 *       Notes: "..."
 *       Patch Version: 1.0
 *       Patch:
 *         - [ be32, 0x001afccc, 0x38600000 ]
 *         - [ load, *some_name ]
 *
 * Read line by line, the place of a line given by the keys above it at
 * smaller indents. */
#include "patch_files.h"
#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
extern "C" const char* ps3_patch_site_flag(uint32_t addr, uint32_t word);

namespace {

/* This build runs one executable: NPEB00553 01.00 (EU), RPCS3's hash for it,
 * and its code, from the ELF's executable sections (.text and the import
 * stubs); everything past them is data. */
const char* const kHash = "PPU-df822d7d47de09c5672201db8cc633009037f54f";
const char* const kSerial = "NPEB00553";
const char* const kVersion = "01.00";
const uint32_t kCodeLo = 0x00010200, kCodeHi = 0x0086FF08;

std::wstring widen(const std::string& s)
{
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

std::string unquote(std::string s)
{
    s = trim(s);
    if (s.size() >= 2 && (s[0] == '"' || s[0] == '\'') && s.back() == s[0]) {
        s = s.substr(1, s.size() - 2);
        std::string out;   /* "\"" and "\\" inside double quotes */
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) { out += s[i + 1] == 'n' ? '\n' : s[i + 1]; i++; }
            else out += s[i];
        }
        return out;
    }
    return s;
}

/* "key: value" -> key, value (a key may be quoted and hold ':') */
bool split_key(const std::string& line, std::string& key, std::string& value)
{
    size_t i = 0;
    if (!line.empty() && (line[0] == '"' || line[0] == '\'')) {
        const char q = line[0];
        for (i = 1; i < line.size() && line[i] != q; i++) if (line[i] == '\\') i++;
        i++;
    }
    const size_t c = line.find(':', i);
    if (c == std::string::npos) return false;
    key = unquote(line.substr(0, c));
    value = trim(line.substr(c + 1));
    return true;
}

/* "[ a, b, "c, d" ]" -> a, b, c, d */
std::vector<std::string> split_list(const std::string& s)
{
    std::vector<std::string> out;
    std::string t = trim(s);
    if (!t.empty() && t[0] == '[') t = t.substr(1);
    if (!t.empty() && t.back() == ']') t.pop_back();
    std::string cur; char q = 0;
    for (char c : t) {
        if (q) { cur += c; if (c == q) q = 0; }
        else if (c == '"' || c == '\'') { q = c; cur += c; }
        else if (c == ',') { out.push_back(unquote(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(unquote(cur));
    return out;
}

struct Op { std::vector<std::string> f; };
struct Raw {
    std::string hash, title, author, notes;
    bool ours = false;   /* lists this game's serial and version */
    std::vector<Op> ops;
};

void put_be(std::string& hex, uint64_t v, int bytes, bool little)
{
    char b[3];
    for (int i = 0; i < bytes; i++) {
        const int sh = little ? 8 * i : 8 * (bytes - 1 - i);
        snprintf(b, sizeof b, "%02x", (unsigned)((v >> sh) & 0xFF));
        hex += b;
    }
}

/* One op -> "addr:hex" pieces; false (with the type) for what we cannot do. */
bool convert(const Op& op, const std::map<std::string, std::vector<Op>>& anchors, uint32_t base,
             std::vector<std::pair<uint32_t, std::string>>& out, std::string& bad, int depth = 0)
{
    if (op.f.empty()) return true;
    const std::string type = op.f[0];
    if (type == "load") {
        /* [ load, *anchor ] or [ load, offset, *anchor ] */
        uint32_t off = 0; std::string name;
        for (size_t i = 1; i < op.f.size(); i++) {
            if (!op.f[i].empty() && op.f[i][0] == '*') name = op.f[i].substr(1);
            else off = (uint32_t)strtoull(op.f[i].c_str(), nullptr, 0);
        }
        const auto a = anchors.find(name);
        if (a == anchors.end() || depth > 8) { bad = "load"; return false; }
        for (const Op& o : a->second)
            if (!convert(o, anchors, base + off, out, bad, depth + 1)) return false;
        return true;
    }
    if (op.f.size() < 3) { bad = type; return false; }
    const uint32_t addr = base + (uint32_t)strtoull(op.f[1].c_str(), nullptr, 0);
    const std::string& v = op.f[2];
    std::string hex;
    auto ival = [&]() -> uint64_t { return v[0] == '-' ? (uint64_t)strtoll(v.c_str(), nullptr, 0) : strtoull(v.c_str(), nullptr, 0); };
    if (type == "byte") put_be(hex, ival(), 1, false);
    else if (type == "be16" || type == "le16") put_be(hex, ival(), 2, type[0] == 'l');
    else if (type == "be32" || type == "le32" || type == "bd32") put_be(hex, ival(), 4, type[0] == 'l');
    else if (type == "be64" || type == "le64" || type == "bd64") put_be(hex, ival(), 8, type[0] == 'l');
    else if (type == "bef32" || type == "lef32") {
        const float f = (float)strtod(v.c_str(), nullptr); uint32_t u; memcpy(&u, &f, 4);
        put_be(hex, u, 4, type[0] == 'l');
    } else if (type == "bef64" || type == "lef64") {
        const double d = strtod(v.c_str(), nullptr); uint64_t u; memcpy(&u, &d, 8);
        put_be(hex, u, 8, type[0] == 'l');
    } else if (type == "utf8" || type == "c_utf8") {
        for (unsigned char c : v) put_be(hex, c, 1, false);
        if (type == "c_utf8") hex += "00";
    } else { bad = type; return false; }
    out.push_back({ addr, hex });
    return true;
}

uint32_t fnv(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

void read_file(const fs::path& path, std::vector<FilePatch>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::map<std::string, std::vector<Op>> anchors;
    std::vector<Raw> raws;
    std::vector<std::pair<int, std::string>> stack;   /* (indent, key) */
    std::string anchor;   /* the anchor whose list is being read */
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);   /* BOM */
        first = false;
        int indent = 0;
        while (indent < (int)line.size() && line[(size_t)indent] == ' ') indent++;
        /* a comment runs from a '#' outside quotes, at the start or after a space,
         * to the end ("PPU-...: # RESIDENT EVIL CODE: Veronica X (EU)") */
        {
            char q = 0;
            for (size_t i = 0; i < line.size(); i++) {
                const char c = line[i];
                if (q) { if (c == '\\') i++; else if (c == q) q = 0; }
                else if (c == '"' || c == '\'') q = c;
                else if (c == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) { line.resize(i); break; }
            }
        }
        const std::string t = trim(line);
        if (t.empty()) continue;
        while (!stack.empty() && stack.back().first >= indent) stack.pop_back();
        const std::string top = stack.empty() ? "" : stack[0].second;
        if (t[0] == '-') {   /* a list item: a patch op */
            Op op{ split_list(t.substr(1)) };
            if (top == "Anchors" && !anchor.empty()) anchors[anchor].push_back(op);
            else if (stack.size() >= 3 && stack[2].second == "Patch" && !raws.empty()) raws.back().ops.push_back(op);
            continue;
        }
        std::string key, value;
        if (!split_key(t, key, value)) continue;
        if (stack.empty()) {
            if (!value.empty() && key != "Anchors") continue;   /* Version: 1.2 */
            stack.push_back({ indent, key });
            continue;
        }
        if (top == "Anchors") {
            if (stack.size() == 1) { anchor = !value.empty() && value[0] == '&' ? value.substr(1) : key; stack.push_back({ indent, key }); }
            continue;
        }
        if (stack.size() == 1) {   /* a patch's title under the hash */
            Raw r; r.hash = top; r.title = key;
            raws.push_back(r);
            stack.push_back({ indent, key });
            continue;
        }
        Raw& r = raws.back();
        if (stack.size() == 2) {
            if (key == "Author") r.author = unquote(value);
            else if (key == "Notes") r.notes = unquote(value);
            if (value.empty()) stack.push_back({ indent, key });
            continue;
        }
        if (stack[2].second == "Games") {
            if (stack.size() == 3) { stack.push_back({ indent, key }); continue; }   /* the game's name */
            if (key == kSerial || key == "All")
                for (const std::string& ver : split_list(value))
                    if (ver == kVersion || ver == "All") r.ours = r.ours || key == kSerial;
            continue;
        }
        if (value.empty()) stack.push_back({ indent, key });
    }
    for (const Raw& r : raws) {
        if (r.hash != kHash && !(r.ours && r.hash.rfind("PPU-", 0) == 0)) continue;
        FilePatch p;
        p.title = widen(r.title); p.author = widen(r.author); p.notes = widen(r.notes);
        p.file = path.filename().wstring();
        std::vector<std::pair<uint32_t, std::string>> writes;
        std::string bad;
        for (const Op& op : r.ops)
            if (!convert(op, anchors, 0, writes, bad)) { p.status = FilePatch::TYPE; p.bad_type = widen(bad); break; }
        if (p.status == FilePatch::OK)
            for (const auto& w : writes) {
                const uint32_t len = (uint32_t)w.second.size() / 2;
                if (w.first + len <= kCodeLo || w.first >= kCodeHi) continue;   /* data */
                const uint32_t word = len == 4 ? (uint32_t)strtoul(w.second.c_str(), nullptr, 16) : 0;
                if (len != 4 || !ps3_patch_site_flag(w.first, word)) { p.status = FilePatch::CODE; break; }
            }
        char a[16];
        for (const auto& w : writes) {
            snprintf(a, sizeof a, "%x:", w.first);
            p.spec += (p.spec.empty() ? "" : ",") + std::string(a) + w.second;
        }
        if (writes.empty() && p.status == FilePatch::OK) continue;   /* nothing to do */
        std::string name;
        for (char c : r.title) if (isalnum((unsigned char)c) && name.size() < 32) name += c;
        snprintf(a, sizeof a, "_%08x", fnv(r.hash + "\n" + r.title + "\n" + p.spec));
        p.id = (name.empty() ? std::string("patch") : name) + a;
        out.push_back(p);
    }
}

}

std::vector<FilePatch> patch_files_scan(const std::wstring& folder, int* files)
{
    std::vector<FilePatch> out;
    int n = 0;
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(folder, ec))
        for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::wstring ext = it->path().extension().wstring();
            for (auto& c : ext) c = (wchar_t)towlower(c);
            if (ext != L".yml" && ext != L".yaml") continue;
            n++;
            read_file(it->path(), out);
        }
    if (files) *files = n;
    return out;
}
