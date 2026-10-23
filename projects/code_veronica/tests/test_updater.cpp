/* The launcher's updater (cv_updater.h) without the network: version order,
 * the paths it must never write, the releases JSON, and a whole
 * download -> install -> cleanup over a throwaway folder in %TEMP%. */
#include "../cv_updater.h"
#include "../launcher_update_i18n.h"
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace cv_update;

static void put(const fs::path& p, const std::string& s)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
static std::string get(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static bool run(std::wstring cmd)
{
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code == 0;
}

int main()
{

    // Exercise replacement of a mapped/running executable and restart in a
    // disposable installation; never launch the game or touch the real port.
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1&&std::wstring(argv[1])==L"--updated"){
        wchar_t self[MAX_PATH];GetModuleFileNameW(nullptr,self,MAX_PATH);
        const fs::path dir=fs::path(self).parent_path();cleanup(dir);
        put(dir/L"new-started.txt","updated launcher started");LocalFree(argv);return 0;
    }
    if(argc==3&&std::wstring(argv[1])==L"--apply-running"){
        wchar_t self[MAX_PATH];GetModuleFileNameW(nullptr,self,MAX_PATH);
        const auto file=fs::path(self);
        assert(install(argv[2],file.parent_path(),file.filename().wstring())==OK);
        SetEnvironmentVariableW(L"CV_UPDATE_DONE",std::to_wstring(GetCurrentProcessId()).c_str());
        std::wstring cmd=L"\""+file.wstring()+L"\" --updated";
        STARTUPINFOW si={sizeof si};PROCESS_INFORMATION pi={};
        assert(CreateProcessW(file.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,file.parent_path().c_str(),&si,&pi));
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);LocalFree(argv);return 0;
    }
    LocalFree(argv);
    for(const auto& row:cv_update_text::texts)for(const auto* text:row)assert(text&&*text);
    /* versions */
    assert(compare_versions(L"v1.1", L"v1.0") > 0);
    assert(compare_versions(L"v1.0", L"v1.0") == 0);
    assert(compare_versions(L"v1.0.1", L"v1.0") > 0);
    assert(compare_versions(L"1.10", L"v1.9") > 0);
    assert(compare_versions(L"v2.0-beta", L"v2.0") == 0);
    assert(compare_versions(L"v0.9", L"v1.0") < 0);
    assert(compare_versions(L"latest", L"v1.0") == 0);   /* not a version: never newer */

    assert(compare_versions(L"v999999999999999999999",L"v999999999999999999998")>0);
    assert(compare_versions(L"v01.000",L"v1.0.0")==0);
    /* what an update never writes */
    assert(is_protected(L"cv_launcher.ini"));
    assert(is_protected(L"gamedata/dev_hdd0/home/00000001/savedata/x.bin"));
    assert(is_protected(L"shader_cache/a.cso"));
    assert(is_protected(L"Screenshots/a.png"));
    assert(is_protected(L"mods/textures/x.png"));
    assert(is_protected(L"JOGO/NPEB00553/USRDIR/EBOOT.BIN"));
    assert(is_protected(L"run-log.txt"));
    assert(is_protected(L"../evil.exe"));
    assert(is_protected(L"C:/Windows/evil.exe"));
    assert(is_protected(L"C:evil.exe"));
    assert(is_protected(L"CON.exe"));
    assert(is_protected(L"saves./evil.exe"));
    assert(is_protected(L"file.dll:stream"));
    assert(!is_protected(L"MyGameRecomp.exe"));
    assert(!is_protected(L"avcodec-61.dll"));
    assert(!is_protected(L"AMD-FSR-LICENSE.md"));

    /* the releases API */
    {
        Release r;
        const std::string json = R"({"tag_name":"v1.2","name":"Port v1.2","body":"## Novidades\r\n- **Atualizador** \u00e9 novo \ud83d\ude00",
            "draft":false,"assets":[{"name":"notes.txt","browser_download_url":"https://x/notes.txt","size":3},
            {"name":"CodeVeronica-PC-v1.2.zip","browser_download_url":"https://x/a.zip","size":12345,
             "digest":"sha256:ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789"}]})";
        assert(parse_release(json, &r));
        assert(r.tag == L"v1.2" && r.name == L"Port v1.2");
        assert(r.asset_name == L"CodeVeronica-PC-v1.2.zip" && r.asset_url == L"https://x/a.zip" && r.asset_size == 12345);
        assert(r.sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
        assert(r.notes.find(L"\u00e9 novo \U0001F600") != std::wstring::npos);
        assert(!parse_release("{\"message\":\"Not Found\"}", &r));
        assert(!parse_release("{broken", &r));
        assert(!parse_release(R"({"tag_name":"v9.0"}garbage)",&r));
        assert(!parse_release(R"({"tag_name":"v9.0","draft":true})",&r));
        assert(!parse_release(R"({"tag_name":"v9.0","prerelease":true})",&r));
        assert(parse_release(R"({"tag_name":"v1.2","assets":[{"name":"source.zip","browser_download_url":"wrong"},{"name":"CodeVeronica-PC-v1.2.zip","browser_download_url":"right"}]})",&r));
        assert(r.asset_url==L"right");
    }

    /* download from a local file, install over a fake port folder, clean up */
    const fs::path base = fs::temp_directory_path() / (L"cv_updater_test-"+std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path port = base / L"port ção", pkg = base / L"pkg" / L"CodeVeronica-v9.9";
    put(port / L"MyGameRecomp.exe", "old exe");
    put(port / L"avcodec-61.dll", "old dll");
    put(port / L"cv_launcher.ini", "settings");
    put(port / L"gamedata/dev_hdd0/save.bin", "my save");
    put(port / L"shader_cache/a.cso", "cache");
    put(pkg / L"MyGameRecomp.exe", "new exe");
    put(pkg / L"avcodec-61.dll", "new dll");
    put(pkg / L"novo.dll", "added");
    put(pkg / L"cv_launcher.ini", "packaged settings");
    put(pkg / L"gamedata/dev_hdd0/save.bin", "packaged save");
    const fs::path zip_src = base / L"release.zip";
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    assert(run(L"\"" + std::wstring(sys) + L"\\tar.exe\" -a -c -f \"" + zip_src.wstring() + L"\" -C \"" +
               pkg.parent_path().wstring() + L"\" CodeVeronica-v9.9"));

    Release r; r.tag = L"v9.9"; r.asset_name = L"CodeVeronica-PC-v9.9.zip"; r.asset_url = zip_src.wstring();
    r.asset_size = fs::file_size(zip_src);
    Progress p; fs::path zip;
    p.cancel=true;
    assert(download(r,base/L"cancel",&p,&zip)==ERR_CANCELED);
    p.cancel=false;
    r.sha256 = std::string(64, '0');
    assert(download(r, base / L"dl", &p, &zip) == ERR_CHECKSUM);
    r.sha256.clear();
    {const auto result=download(r, base/L"dl", &p, &zip);
     if(result!=OK)std::fprintf(stderr,"download error=%d done=%llu size=%llu\n",int(result),(unsigned long long)p.done.load(),(unsigned long long)r.asset_size);
     assert(result==OK);}
    assert(p.done == r.asset_size && p.total == r.asset_size && fs::file_size(zip) == r.asset_size);

    assert(install(zip, port, L"Outro.exe") == ERR_BAD_PACKAGE);   /* this executable must be in it */
    {const auto result=download(r, base/L"dl", &p, &zip);
     if(result!=OK)std::fprintf(stderr,"download error=%d done=%llu size=%llu\n",int(result),(unsigned long long)p.done.load(),(unsigned long long)r.asset_size);
     assert(result==OK);}
    assert(install(zip, port, L"MyGameRecomp.exe") == OK);
    assert(get(port / L"MyGameRecomp.exe") == "new exe");
    assert(get(port / L"avcodec-61.dll") == "new dll");
    assert(get(port / L"novo.dll") == "added");
    assert(get(port / L"cv_launcher.ini") == "settings");
    assert(get(port / L"gamedata/dev_hdd0/save.bin") == "my save");
    assert(get(port / L"shader_cache/a.cso") == "cache");
    assert(get(port / L"MyGameRecomp.exe.cvupd-old") == "old exe");

    /* a locked file (as the running exe's DLLs would be, without delete
     * sharing) cannot be renamed: the install undoes itself */
    put(pkg / L"MyGameRecomp.exe", "newer exe");
    assert(run(L"\"" + std::wstring(sys) + L"\\tar.exe\" -a -c -f \"" + zip_src.wstring() + L"\" -C \"" +
               pkg.parent_path().wstring() + L"\" CodeVeronica-v9.9"));
    r.asset_size = fs::file_size(zip_src);
    {const auto result=download(r, base/L"dl", &p, &zip);
     if(result!=OK)std::fprintf(stderr,"download error=%d done=%llu size=%llu\n",int(result),(unsigned long long)p.done.load(),(unsigned long long)r.asset_size);
     assert(result==OK);}
    {
        HANDLE lock = CreateFileW((port / L"novo.dll").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        assert(lock != INVALID_HANDLE_VALUE);
        assert(install(zip, port, L"MyGameRecomp.exe") == ERR_WRITE);
        CloseHandle(lock);
    }
    assert(get(port / L"MyGameRecomp.exe") == "new exe");   /* rolled back */
    assert(get(port / L"avcodec-61.dll") == "new dll");

    const fs::path outside=base/L"outside.cvupd-old";
    put(outside,"leave me");
    {std::ofstream list(port/L".cvupd-cleanup",std::ios::app);
     const auto bytes=(port/L".."/L"outside.cvupd-old").u8string(); list.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());list<<"\n";}
    SetEnvironmentVariableW(L"CV_UPDATE_DONE", std::to_wstring(GetCurrentProcessId() + 4000000).c_str());   /* no such process */
    cleanup(port);
    assert(!fs::exists(port / L"MyGameRecomp.exe.cvupd-old"));
    assert(!fs::exists(port / L".cvupd-cleanup"));
    assert(fs::exists(port / L"MyGameRecomp.exe"));
    assert(get(outside)=="leave me");

    wchar_t fixtures[2048];
    if(GetEnvironmentVariableW(L"CV_UPDATER_FIXTURES",fixtures,2048)){
        for(const auto& f:fs::directory_iterator(fixtures)){
            const auto result=install(f.path(),port,L"MyGameRecomp.exe");
            if(result!=ERR_BAD_PACKAGE)std::fprintf(stderr,"bad zip unexpectedly accepted, error=%d\n",int(result));
            assert(result==ERR_BAD_PACKAGE);
            assert(get(port/L"MyGameRecomp.exe")=="new exe");
        }
    }
    wchar_t url[2048];
    if(GetEnvironmentVariableW(L"CV_UPDATE_URL",url,2048)){
        Release http;assert(fetch_latest(&http)==OK);assert(http.tag==L"v9.9");
        assert(download(http,base/L"http",&p,&zip)==OK);
        assert(install(zip,port,L"MyGameRecomp.exe")==OK);
        assert(get(port/L"MyGameRecomp.exe")=="HTTP fake updated executable");
        assert(get(port/L"cv_launcher.ini")=="settings");
        assert(get(port/L"gamedata/dev_hdd0/save.bin")=="my save");
        cleanup(port);
    }
    /* the port reached through a junction (C:\Jogos -> D:\Jogos, OneDrive):
     * links above the port's folder are the player's and do not block it */
    {
        const fs::path link = base / L"atalho";
        assert(run(L"cmd /c mklink /J \"" + link.wstring() + L"\" \"" + port.wstring() + L"\""));
        put(pkg / L"MyGameRecomp.exe", "through junction");
        assert(run(L"\"" + std::wstring(sys) + L"\\tar.exe\" -a -c -f \"" + zip_src.wstring() + L"\" -C \"" +
                   pkg.parent_path().wstring() + L"\" CodeVeronica-v9.9"));
        r.asset_size = fs::file_size(zip_src);
        assert(download(r, base / L"dl", &p, &zip) == OK);
        assert(install(zip, link, L"MyGameRecomp.exe") == OK);
        assert(get(port / L"MyGameRecomp.exe") == "through junction");
        cleanup(link);
        assert(!fs::exists(port / L".cvupd-cleanup"));
        RemoveDirectoryW(link.c_str());   /* the junction only, before remove_all */
    }
    fs::remove_all(base, ec);
    std::printf("updater tests: ok\n");
    return 0;
}
