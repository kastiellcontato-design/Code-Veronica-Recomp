/* cv_updater.cpp -- see cv_updater.h. */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include "cv_updater.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <functional>
#include <vector>
#include <set>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace cv_update {
namespace {

std::wstring from_utf8(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}
std::string to_utf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }

/* ------------------------------------------------------------ JSON
 * Just enough of it for the releases API: objects, arrays, strings (with
 * \u escapes, surrogate pairs included), and the scalars skipped over. */
struct JValue {
    enum Type { NUL, SCALAR, STR, ARR, OBJ } t = NUL;
    std::string s;
    std::vector<JValue> a;
    std::vector<std::pair<std::string, JValue>> o;
    const JValue* get(const char* key) const
    {
        for (auto& kv : o) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string str(const char* key) const { const JValue* v = get(key); return v && v->t == STR ? v->s : ""; }
};
struct JParser {
    const char *p, *e;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++; }
    static void put_utf8(std::string& out, uint32_t c)
    {
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | c >> 6); out += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += char(0xE0 | c >> 12); out += char(0x80 | (c >> 6 & 0x3F)); out += char(0x80 | (c & 0x3F)); }
        else { out += char(0xF0 | c >> 18); out += char(0x80 | (c >> 12 & 0x3F)); out += char(0x80 | (c >> 6 & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    }
    bool hex4(uint32_t* v)
    {
        if (e - p < 4) return false;
        *v = 0;
        for (int i = 0; i < 4; i++, p++) {
            const char c = *p; *v <<= 4;
            if (c >= '0' && c <= '9') *v |= c - '0';
            else if (c >= 'a' && c <= 'f') *v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') *v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool string(std::string& out)
    {
        if (p >= e || *p != '"') return false;
        for (p++; p < e && *p != '"'; ) {
            if (*p != '\\') { out += *p++; continue; }
            if (++p >= e) return false;
            const char c = *p++;
            switch (c) {
            case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'u': {
                uint32_t u; if (!hex4(&u)) return false;
                if (u >= 0xD800 && u < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    const char* back = p; p += 2; uint32_t lo;
                    if (hex4(&lo) && lo >= 0xDC00 && lo < 0xE000) u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    else p = back;
                }
                put_utf8(out, u); break; }
            default: out += c;   /* \" \\ \/ */
            }
        }
        if (p >= e) return false;
        p++; return true;
    }
    bool value(JValue& v, int depth)
    {
        if (depth > 64) return false;
        ws(); if (p >= e) return false;
        if (*p == '"') { v.t = JValue::STR; return string(v.s); }
        if (*p == '{') {
            v.t = JValue::OBJ; p++; ws();
            if (p < e && *p == '}') { p++; return true; }
            for (;;) {
                ws(); std::string k; if (!string(k)) return false;
                ws(); if (p >= e || *p++ != ':') return false;
                v.o.emplace_back(std::move(k), JValue{});
                if (!value(v.o.back().second, depth + 1)) return false;
                ws(); if (p >= e) return false;
                if (*p == ',') { p++; continue; }
                if (*p == '}') { p++; return true; }
                return false;
            }
        }
        if (*p == '[') {
            v.t = JValue::ARR; p++; ws();
            if (p < e && *p == ']') { p++; return true; }
            for (;;) {
                v.a.emplace_back();
                if (!value(v.a.back(), depth + 1)) return false;
                ws(); if (p >= e) return false;
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; return true; }
                return false;
            }
        }
        /* numbers, true, false, null: not needed, only skipped */
        const char* start = p;
        while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') p++;
        if (p == start) return false;
        v.t = std::string(start, p) == "null" ? JValue::NUL : JValue::SCALAR;
        v.s.assign(start, p);
        return true;
    }
};

/* ------------------------------------------------------------ transfers */
bool is_web(const std::wstring& url)
{
    return !_wcsnicmp(url.c_str(), L"https://", 8) || !_wcsnicmp(url.c_str(), L"http://", 7);
}
/* A local path, also given as file:///C:/... */
fs::path local_path(std::wstring url)
{
    if (!_wcsnicmp(url.c_str(), L"file:///", 8)) url = url.substr(8);
    else if (!_wcsnicmp(url.c_str(), L"file://", 7)) url = url.substr(7);
    return fs::path(url);
}

/* Reads url into sink, chunk by chunk; sink returns false to stop. GitHub's
 * asset links redirect to its storage host, which WinHTTP follows. */
Error fetch(const std::wstring& url, const std::function<bool(const char*, size_t)>& sink, Progress* p)
{
    if (!is_web(url)) {
        std::ifstream f(local_path(url), std::ios::binary);
        if (!f) return ERR_NETWORK;
        f.seekg(0, std::ios::end); if (p) p->total = uint64_t(f.tellg()); f.seekg(0);
        std::vector<char> buf(1 << 20);
        while (f) {
            f.read(buf.data(), buf.size());
            const size_t n = size_t(f.gcount());
            if (!n) break;
            if (p && p->cancel) return ERR_CANCELED;
            if (!sink(buf.data(), n)) return ERR_DOWNLOAD;
            if (p) p->done += n;
        }
        return OK;
    }
    URL_COMPONENTS uc = { sizeof uc };
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    wchar_t extra[2048] = {}; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return ERR_NETWORK;
    const std::wstring object = std::wstring(path) + extra;
    Error err = ERR_NETWORK;
    HINTERNET s = WinHttpOpen(L"CodeVeronicaPort-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"CodeVeronicaPort-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : nullptr;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r) {
        WinHttpSetTimeouts(r, 15000, 15000, 20000, 30000);
        const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream;q=0.9, */*;q=0.8\r\n"
                             L"X-GitHub-Api-Version: 2022-11-28\r\n";
        DWORD status = 0, sz = sizeof status;
        if (WinHttpSendRequest(r, hdr, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, nullptr) &&
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr)) {
            if (status == 404) err = ERR_NO_RELEASE;
            else if (status != 200) err = ERR_NETWORK;
            else {
                DWORD len = 0; sz = sizeof len;
                if (p && WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &len, &sz, nullptr))
                    p->total = len;
                std::vector<char> buf(256 * 1024);
                err = OK;
                for (;;) {
                    if (p && p->cancel) { err = ERR_CANCELED; break; }
                    DWORD got = 0;
                    if (!WinHttpReadData(r, buf.data(), DWORD(buf.size()), &got)) { err = ERR_DOWNLOAD; break; }
                    if (!got) break;
                    if (!sink(buf.data(), got)) { err = ERR_DOWNLOAD; break; }
                    if (p) p->done += got;
                }
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return err;
}

struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr;
    Sha256()
    {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
            BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    }
    ~Sha256() { if (h) BCryptDestroyHash(h); if (alg) BCryptCloseAlgorithmProvider(alg, 0); }
    void add(const char* d, size_t n) { if (h) BCryptHashData(h, (PUCHAR)d, ULONG(n), 0); }
    std::string hex()
    {
        unsigned char out[32]; if (!h || BCryptFinishHash(h, out, 32, 0) != 0) return "";
        char s[65]; for (int i = 0; i < 32; i++) snprintf(s + 2 * i, 3, "%02x", out[i]);
        return s;
    }
};

std::wstring short_path(const fs::path& p)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetShortPathNameW(p.c_str(), buf, MAX_PATH * 2);
    return n && n < MAX_PATH * 2 ? std::wstring(buf) : p.wstring();
}

const wchar_t* const k_cleanup_list = L".cvupd-cleanup";

bool safe_path(const fs::path& rel)
{
    if(rel.empty()||rel.is_absolute()||rel.has_root_path())return false;
    for(const auto& part:rel){
        const auto w=part.wstring();
        if(w.empty()||w==L"."||w==L".."||w.back()==L'.'||w.back()==L' '||
           w.find_first_of(L"<>:\"|?*")!=std::wstring::npos)return false;
        for(auto c:w)if(c<32)return false;
        const auto stem=lower(w.substr(0,w.find(L'.')));
        if(stem==L"con"||stem==L"prn"||stem==L"aux"||stem==L"nul"||
           (stem.size()==4&&(stem.substr(0,3)==L"com"||stem.substr(0,3)==L"lpt")&&stem[3]>=L'1'&&stem[3]<=L'9'))return false;
    }
    return true;
}
/* A link or junction between base and p (base itself and what is above it
 * are the player's own: a port kept under a junctioned or OneDrive folder
 * still updates). */
bool reparse_in_path(const fs::path& base,fs::path p)
{
    const auto top=lower(fs::absolute(base).lexically_normal().wstring());
    p=fs::absolute(p).lexically_normal();
    while(!p.empty()&&lower(p.wstring())!=top){
        const DWORD a=GetFileAttributesW(p.c_str());
        if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))return true;
        const auto next=p.parent_path();if(next==p)break;p=next;
    }
    return false;
}
uint16_t u16(const unsigned char* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t u32(const unsigned char* p){return uint32_t(u16(p))|(uint32_t(u16(p+2))<<16);}
// Inspect ZIP directory records before invoking tar: no traversal, aliases,
// links, encrypted entries, multi-volume/ZIP64 archives, or decompression bombs.
bool valid_zip(const fs::path& zip)
{
    std::ifstream in(zip,std::ios::binary);if(!in)return false;
    in.seekg(0,std::ios::end);const auto length=in.tellg();if(length<22)return false;
    const auto tail_size=std::min<uint64_t>(uint64_t(length),65557);
    std::vector<unsigned char> tail(size_t(tail_size),0);in.seekg(length-std::streamoff(tail_size));
    in.read(reinterpret_cast<char*>(tail.data()),tail.size());if(!in)return false;
    size_t end=tail.size();
    for(size_t at=tail.size()-22;;--at){
        if(u32(&tail[at])==0x06054b50&&at+22+u16(&tail[at+20])==tail.size()){end=at;break;}
        if(!at)break;
    }
    if(end==tail.size())return false;
    const auto* e=&tail[end];const auto count=u16(e+10);const uint64_t bytes=u32(e+12),offset=u32(e+16);
    if(u16(e+4)||u16(e+6)||u16(e+8)!=count||!count||count>10000||count==65535||
       bytes==0xffffffffu||offset==0xffffffffu||offset+bytes>uint64_t(length)-tail_size+end)return false;
    in.clear();in.seekg(std::streamoff(offset));uint64_t used=0,total=0;std::set<std::wstring> names;
    for(unsigned i=0;i<count;i++){
        unsigned char h[46];in.read(reinterpret_cast<char*>(h),46);
        if(!in||u32(h)!=0x02014b50||(u16(h+8)&1)||u16(h+34))return false;
        const auto n=u16(h+28),extra=u16(h+30),comment=u16(h+32);
        const auto attr=u32(h+38),mode=(attr>>16)&0170000;
        if(mode&&mode!=0100000&&mode!=0040000)return false;
        if(attr&FILE_ATTRIBUTE_REPARSE_POINT)return false;
        const uint64_t compressed=u32(h+20),uncompressed=u32(h+24),local=u32(h+42);
        if(compressed==0xffffffffu||uncompressed==0xffffffffu||local>=offset||!n)return false;
        total+=uncompressed;if(total>(uint64_t(8)<<30))return false;
        std::string name(n,'\0');in.read(name.data(),n);if(!in||name.find('\0')!=std::string::npos)return false;
        std::replace(name.begin(),name.end(),'\\','/');if(name.back()=='/')name.pop_back();
        const auto w=from_utf8(name);if(!safe_path(fs::path(w))||!names.insert(lower(w)).second)return false;
        const auto next=in.tellg();in.seekg(std::streamoff(local));unsigned char lh[30];in.read(reinterpret_cast<char*>(lh),30);
        if(!in||u32(lh)!=0x04034b50||u16(lh+26)!=n)return false;
        std::string lname(n,'\0');in.read(lname.data(),n);if(!in)return false;
        std::replace(lname.begin(),lname.end(),'\\','/');if(!lname.empty()&&lname.back()=='/')lname.pop_back();
        if(lname!=name||uint64_t(local)+30+u16(lh+26)+u16(lh+28)+compressed>offset)return false;
        in.clear();in.seekg(next+std::streamoff(extra+comment));used+=46+n+extra+comment;
        if(!in||used>bytes)return false;
    }
    return used==bytes;
}
struct InstallLock {
    HANDLE h=nullptr;bool acquired=false;
    explicit InstallLock(const fs::path& p){
        uint64_t hash=1469598103934665603ull;
        for(auto c:lower(fs::absolute(p).lexically_normal().wstring())){hash^=uint16_t(c);hash*=1099511628211ull;}
        const auto name=L"Local\\CodeVeronicaUpdate-"+std::to_wstring(hash);
        h=CreateMutexW(nullptr,FALSE,name.c_str());if(h){const auto w=WaitForSingleObject(h,0);acquired=w==WAIT_OBJECT_0||w==WAIT_ABANDONED;}
    }
    ~InstallLock(){if(acquired)ReleaseMutex(h);if(h)CloseHandle(h);}
};


}  // namespace

/* ------------------------------------------------------------ public */
int compare_versions(const std::wstring& a, const std::wstring& b)
{
    auto parts = [](std::wstring v) {
        std::vector<std::wstring> out;
        size_t i = 0;
        while (i < v.size() && (v[i] == L'v' || v[i] == L'V' || v[i] == L' ')) i++;
        const size_t end = v.find_first_of(L"-+ ", i);
        v = v.substr(i, end == std::wstring::npos ? std::wstring::npos : end - i);
        for (size_t at = 0; at <= v.size(); ) {
            const size_t dot = std::min(v.find(L'.', at), v.size());
            const std::wstring part = v.substr(at, dot - at);
            if (part.empty() || part.find_first_not_of(L"0123456789") != std::wstring::npos) return std::vector<std::wstring>{};
            const auto nz=part.find_first_not_of(L'0');
            out.push_back(nz==std::wstring::npos?L"0":part.substr(nz));
            at = dot + 1;
        }
        return out;
    };
    std::vector<std::wstring> x = parts(a), y = parts(b);
    if (x.empty() || y.empty()) return 0;   /* not a version: never "newer" */
    x.resize(std::max(x.size(), y.size()),L"0"); y.resize(x.size(),L"0");
    for (size_t i = 0; i < x.size(); i++) {
        if(x[i].size()!=y[i].size())return x[i].size()<y[i].size()?-1:1;
        if(x[i]!=y[i])return x[i]<y[i]?-1:1;
    }
    return 0;
}

bool is_protected(const fs::path& relative)
{
    static const wchar_t* const dirs[] = { L"gamedata", L"dev_hdd0", L"dev_flash", L"saves", L"shader_cache",
        L"eboot_cache", L"appearance_cache", L"screenshots", L"mods", L"textures", L"jogo", L"save-library-backups", L"trophies", L"codeveronicalibrary" };
    if (!safe_path(relative)) return true;
    for (const auto& part : relative) if (part == L".." || part == L".") return true;
    // Organized packages put both runtimes and personal data below bin/.
    if(lower(relative.begin()->wstring())==L"bin") {
        fs::path nested;auto part=relative.begin();++part;
        for(;part!=relative.end();++part)nested/=*part;
        if(!nested.empty())return is_protected(nested);
    }
    const std::wstring first = lower(relative.begin()->wstring());
    for (const wchar_t* d : dirs) if (first == d) return true;
    const std::wstring name = lower(relative.filename().wstring()), ext = lower(relative.extension().wstring());
    if (ext == L".cvsav" || ext == L".ini" || ext == L".log" || ext == L".cvupd-old") return true;
    if (name.rfind(L"run-log", 0) == 0 || name == L"cv_f7_saves.txt" || name == k_cleanup_list) return true;
    return false;
}

// Only the port package is an update. Auxiliary/source ZIPs must never be
// selected, regardless of the ordering returned by GitHub. Normalize the
// separators GitHub uses when a release asset is renamed with spaces.
static bool is_port_package(const std::wstring& filename) {
    const auto name=lower(filename);
    if(name.size()<4||name.substr(name.size()-4)!=L".zip")return false;
    std::wstring compact;
    for(wchar_t c:name.substr(0,name.size()-4)) {
        if(c==L'.'||c==L'-'||c==L'_'||c==L' ')continue;
        compact+=c;
    }
    for(const wchar_t* prefix:{L"codeveronicarecompreleasev",L"codeveronicarecomppcv",L"codeveronicapcv",L"veronicaportv"}) {
        const std::wstring start(prefix);
        if(compact.rfind(start,0)!=0||compact.size()==start.size())continue;
        bool numeric=true;
        for(size_t i=start.size();i<compact.size();++i)if(compact[i]<L'0'||compact[i]>L'9'){numeric=false;break;}
        if(numeric)return true;
    }
    return false;
}

bool parse_release(const std::string& json, Release* out)
{
    JValue root; JParser jp{ json.data(), json.data() + json.size() };
    if (!out||!jp.value(root, 0) || root.t != JValue::OBJ) return false;
    jp.ws();if(jp.p!=jp.e)return false;
    for(const char* key:{"draft","prerelease"})if(const auto* v=root.get(key);v&&v->s=="true")return false;
    Release r;
    r.tag = from_utf8(root.str("tag_name"));
    r.name = from_utf8(root.str("name"));
    r.notes = from_utf8(root.str("body"));
    if (r.tag.empty()) return false;

    if (const JValue* assets = root.get("assets"); assets && assets->t == JValue::ARR)
        for (const JValue& a : assets->a) {
            const std::wstring name = from_utf8(a.str("name"));
            if (!is_port_package(name) || !r.asset_name.empty()) continue;
            r.asset_size=0;r.sha256.clear();
            r.asset_name = name;
            r.asset_url = from_utf8(a.str("browser_download_url"));
            if (const JValue* sz = a.get("size"); sz && sz->t == JValue::SCALAR) r.asset_size = _strtoui64(sz->s.c_str(), nullptr, 10);
            const std::string digest = a.str("digest");
            if (digest.rfind("sha256:", 0) == 0 && digest.size() == 7 + 64) {
                r.sha256 = digest.substr(7);
                for (auto& c : r.sha256) c = char(tolower((unsigned char)c));
                if(r.sha256.find_first_not_of("0123456789abcdef")!=std::string::npos)return false;
            }
        }
    *out = r;
    return true;
}

std::wstring release_url()
{
    wchar_t buf[2048];
    const DWORD n = GetEnvironmentVariableW(L"CV_UPDATE_URL", buf, 2048);
    return n && n < 2048 ? std::wstring(buf) : std::wstring(k_default_url);
}

fs::path work_dir()
{
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    return fs::path(tmp) / L"CodeVeronicaUpdate" / std::to_wstring(GetCurrentProcessId());
}

Error fetch_latest(Release* out)
{
    std::string json;
    const Error e = fetch(release_url(), [&](const char* d, size_t n) {
        if (json.size() + n > (8u << 20)) return false;   /* a release page is a few KB */
        json.append(d, n); return true; }, nullptr);
    if (e != OK) return e == ERR_DOWNLOAD ? ERR_NETWORK : e;
    if (!parse_release(json, out)) return ERR_NO_RELEASE;
    if (out->asset_url.empty()) return ERR_NO_PACKAGE;
    return OK;
}

Error download(const Release& r, const fs::path& dest_dir, Progress* p, fs::path* zip)
{
    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    std::wstring name = fs::path(r.asset_name).filename().wstring();
    if (name.empty() || name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos) name = L"update.zip";
    const fs::path out = dest_dir / name, part = dest_dir / (name + L".part");
    fs::remove(part, ec);
    Sha256 hash;
    {
        std::ofstream f(part, std::ios::binary);
        if (!f) return ERR_WRITE;
        if (p) { p->done = 0; p->total = r.asset_size; }
        const Error e = fetch(r.asset_url, [&](const char* d, size_t n) {
            if((p&&p->cancel)||(r.asset_size&&uint64_t(f.tellp())+n>r.asset_size)||uint64_t(f.tellp())+n>(uint64_t(2)<<30))return false;
            f.write(d, std::streamsize(n)); hash.add(d, n); return bool(f); }, p);
        if (e == OK) f.close();
        if (e != OK || !f) { f.close(); fs::remove(part, ec); return e != OK ? e : ERR_WRITE; }
    }
    if ((r.asset_size && fs::file_size(part, ec) != r.asset_size) || (!r.sha256.empty() && hash.hex() != r.sha256)) {
        fs::remove(part, ec); return ERR_CHECKSUM;
    }
    fs::remove(out, ec);
    fs::rename(part, out, ec);
    if (ec) return ERR_WRITE;
    *zip = out;
    return OK;
}

Error install(const fs::path& zip, const fs::path& install_dir, const std::wstring& exe_name)
{
    std::error_code ec;
    if(fs::path(exe_name).filename()!=fs::path(exe_name)||is_protected(exe_name))return ERR_BAD_PACKAGE;
    if(!valid_zip(zip))return ERR_BAD_PACKAGE;
    InstallLock lock(install_dir);if(!lock.acquired)return ERR_WRITE;
    const fs::path staging = work_dir() / (L"unpacked-"+std::to_wstring(GetTickCount64()));
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (ec) return ERR_EXTRACT;
    /* Windows 10 1803+ ships bsdtar, which reads zip. Short paths keep a
     * user name outside the ANSI code page from reaching its narrow argv. */
    {
        wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
        const std::wstring tar = std::wstring(sys) + L"\\tar.exe";
        std::wstring cmd = L"\"" + tar + L"\" -xf \"" + short_path(zip) + L"\" -C \"" + short_path(staging) + L"\"";
        STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(tar.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            return ERR_EXTRACT;
        DWORD code = 1;
        if (WaitForSingleObject(pi.hProcess, 10 * 60 * 1000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
        else TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        if (code != 0) return ERR_EXTRACT;
    }
    /* The files may sit in one top folder (CodeVeronica-v1.1\...). */
    fs::path root = staging;
    {
        std::vector<fs::path> top;
        for (auto& e : fs::directory_iterator(staging, ec)) top.push_back(e.path());
        if (top.size() == 1 && fs::is_directory(top[0], ec)) root = top[0];
    }
    if (!fs::is_regular_file(root / exe_name, ec)) return ERR_BAD_PACKAGE;

    std::vector<fs::path> files;   /* relative to root */
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if(reparse_in_path(staging,it->path()))return ERR_BAD_PACKAGE;
        if (!it->is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(it->path(), root, ec);
        if (!ec && !is_protected(rel)) {
            if(reparse_in_path(install_dir,install_dir/rel))return ERR_BAD_PACKAGE;
            files.push_back(rel);
        }
    }
    if (ec) return ERR_EXTRACT;

    /* Rename what is there, copy the new file in; undo it all on a failure. */
    struct Done { fs::path dest, old; };
    std::vector<Done> done;
    auto rollback = [&] {
        for (auto d = done.rbegin(); d != done.rend(); ++d) {
            DeleteFileW(d->dest.c_str());
            if (!d->old.empty()) MoveFileExW(d->old.c_str(), d->dest.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    };
    for (const fs::path& rel : files) {
        const fs::path src = root / rel, dest = install_dir / rel;
        fs::create_directories(dest.parent_path(), ec);
        if(ec){rollback();return ERR_WRITE;}
        Done d{ dest, {} };
        if (fs::exists(dest, ec)) {
            fs::path old = dest; old += L".cvupd-old";
            if (fs::exists(old, ec) && !DeleteFileW(old.c_str())) { old = dest; old += L"." + std::to_wstring(GetTickCount64()) + L".cvupd-old"; }
            if (!MoveFileExW(dest.c_str(), old.c_str(), 0)) { rollback(); return ERR_WRITE; }
            d.old = old;
        }
        if (!CopyFileW(src.c_str(), dest.c_str(), FALSE)) {
            DeleteFileW(dest.c_str());
            if (!d.old.empty()) MoveFileExW(d.old.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING);
            rollback(); return ERR_WRITE;
        }
        done.push_back(d);
    }
    /* The old copies, for cleanup() once this process has gone. */
    {
        std::ofstream list(install_dir / k_cleanup_list, std::ios::binary | std::ios::app);
        for (const Done& d : done) if (!d.old.empty()) list << to_utf8(d.old.wstring()) << "\n";
        list.close();if(!list){rollback();return ERR_WRITE;}
    }
    fs::remove_all(staging, ec);
    fs::remove(zip, ec);
    return OK;
}

void cleanup(const fs::path& install_dir)
{
    wchar_t pid[32];
    if (GetEnvironmentVariableW(L"CV_UPDATE_DONE", pid, 32)) {
        SetEnvironmentVariableW(L"CV_UPDATE_DONE", nullptr);   /* not for the processes this one starts */
        if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(_wtoi(pid)))) { WaitForSingleObject(h, 15000); CloseHandle(h); }
    }
    const fs::path list_path = install_dir / k_cleanup_list;
    std::ifstream in(list_path, std::ios::binary);
    if (!in) return;
    std::vector<std::wstring> left;
    for (std::string line; std::getline(in, line); ) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const fs::path old_path=fs::path(from_utf8(line)).lexically_normal();
        const fs::path normalized_dir=fs::absolute(install_dir).lexically_normal();
        const auto rel=old_path.lexically_relative(normalized_dir);
        if(!safe_path(rel)||reparse_in_path(normalized_dir,old_path))continue;
        const std::wstring old = old_path.wstring();
        /* only the copies this updater made, inside the port's folder */
        const std::wstring dir = lower(normalized_dir.wstring()) + L"\\";
        if (old.size() < 10 || lower(old.substr(old.size() - 10)) != L".cvupd-old" ||
            lower(old).compare(0, dir.size(), dir) != 0) continue;
        bool gone = false;
        for (int i = 0; i < 20 && !gone; i++) {
            gone = DeleteFileW(old.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
            if (!gone) Sleep(250);
        }
        if (!gone) left.push_back(old);
    }
    in.close();
    std::error_code ec;
    if (left.empty()) fs::remove(list_path, ec);
    else { std::ofstream out(list_path, std::ios::binary | std::ios::trunc); for (auto& l : left) out << to_utf8(l) << "\n"; }
    // Do not touch a staging directory another updater thread may be using.
}

}  // namespace cv_update
