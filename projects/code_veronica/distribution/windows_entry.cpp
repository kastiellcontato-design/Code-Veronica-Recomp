// Distribution entry point: Windows GUI subsystem, no console allocation.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <cwctype>
#include <io.h>
#include <share.h>
#include "../cv_version.h"
#include "../cv_support_log.h"
extern int main(int,char**);

// CV_LAUNCHER_AUTOPLAY is a normal regional handoff, consumed once by the launcher.
static void clear_developer_environment() {
    const wchar_t* names[]={L"CV_NO_LAUNCHER",L"CV_FPS_DIAGNOSTIC",
        L"CV_FPS_TEST_60",L"CV_FPS_UNLOCKED",L"CV_FPS_BENCHMARK_RAW",L"CV_RENDER_INTERPOLATION",
        L"CV_POSE_COMPUTE",L"CV_POSE_COMPUTE_TEST",L"CV_POSE_CPU",L"CV_POSE_CPU_BLEND",
        L"CV_FSR_FRAME_GENERATION",L"CV_FSR_DIAGNOSTIC",L"CV_WORK_PROFILE",L"CV_WORK_PROFILE_STACKS",
        L"CV_MESH_DISCOVERY",L"CV_SPU_DESCRIPTOR_TEST",L"CV_SPU_NATIVE_DESCRIPTORS",L"CV_SPU_NATIVE_DRAW",
        L"CV_LIGHTER_PROBE",L"CV_AUTOCAP",L"CV_VIDEO_CAPTURE",L"CV_GPU_STATS",L"CV_COMPOSITE_PROBE",
        L"CV_POSE_STRETCH_PROBE",L"CV_FRAME_PROFILE",L"CV_FRAME_DETAIL",L"CV_PRODUCER_PROFILE",
        L"CV_POSE_PROFILE",L"CV_POSE_SEARCH_PROFILE",L"CV_FIFO_PROFILE",L"CV_FIFO_NOTIFY",
        L"CV30_OPTIMIZATIONS",L"RSX_CAP",L"RSX_CAP_DIR",L"D3D12_DBG",L"D3D12_GBV",
        L"PS3_SAMPLE",L"PS3_SAMPLE_SPU",L"PS3_SAMPLE_MAIN",L"PS3_SAMPLE_THREAD",L"PS3_TIMELINE",
        L"PS3_POLLTOP",L"PS3_SPU_STAT",L"PS3_FLIPTRACE",L"PS3_SHOT_BURST",L"SPU_FAULT_DIR",
        L"SPU_MFC_STAGE_FAST",L"PERF",L"PERF_THREADS",L"HITCHDBG",L"FRAME_BUDGET",L"SRT_LOG"};
    for(auto name:names)_wputenv_s(name,L"");
    // Remove developer probes without iterating a mutating CRT environment.
    wchar_t* block=GetEnvironmentStringsW();std::vector<std::wstring> probes;
    if(block){for(const wchar_t* p=block;*p;p+=wcslen(p)+1){std::wstring name(p,wcscspn(p,L"="));
        if(name.empty())continue;
        std::wstring upper=name;for(auto& ch:upper)ch=(wchar_t)towupper(ch);
        if(upper.rfind(L"FP_",0)==0||upper.rfind(L"TEX_",0)==0||upper.rfind(L"SPURS_EF_",0)==0||upper.rfind(L"SPURS_TASK_",0)==0)
            probes.push_back(name);
    }FreeEnvironmentStringsW(block);}
    for(const auto& name:probes)_wputenv_s(name.c_str(),L"");
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    clear_developer_environment();
    wchar_t executable[32768];
    if(!GetModuleFileNameW(nullptr,executable,32768))return 1;
    const auto directory=std::filesystem::path(executable).parent_path();
    FILE* stream=nullptr;
    // Regional handoff starts the child before the launcher exits. Both may
    // write this log briefly; exclusive freopen_s would reject the child.
    // Initialize GUI CRT descriptors, then use a shared append-only stream.
    const auto log_path=directory/L"run-log.txt";
    std::error_code log_ec;
    if(std::filesystem::file_size(log_path,log_ec)>8*1024*1024 && !log_ec) {
        const auto previous=directory/L"run-log.previous.txt";
        MoveFileExW(log_path.c_str(),previous.c_str(),MOVEFILE_REPLACE_EXISTING);
    }
    FILE* log=_wfsopen(log_path.c_str(),L"a",_SH_DENYNO);
    const bool ready=log &&
        _wfreopen_s(&stream,L"NUL",L"w",stdout)==0 &&
        _wfreopen_s(&stream,L"NUL",L"w",stderr)==0 &&
        _dup2(_fileno(log),_fileno(stdout))==0 &&
        _dup2(_fileno(log),_fileno(stderr))==0;
    if(log)fclose(log);
    if(!ready){
        const std::wstring message=L"Nao foi possivel abrir o arquivo de log. Confira a permissao de escrita nesta pasta:\n"
            +directory.wstring()+L"\n\nCould not open the log file. Check write access to this folder.";
        MessageBoxW(nullptr,message.c_str(),L"Code Veronica",MB_OK|MB_ICONERROR);
        return 1;
    }
    // Buffered informational output avoids flushing every startup/progress line.
    // Error output stays immediately visible in the session log.
    setvbuf(stdout,nullptr,_IOFBF,65536);setvbuf(stderr,nullptr,_IONBF,0);
    fprintf(stderr,"[distribution] Code Veronica PC v1.1.4; GUI executable; developer captures disabled\n");
    cv_support::startup(CV_TITLE_ID);
    const int result=main(__argc,__argv);
    fflush(stdout);fflush(stderr);
    if(result)MessageBoxW(nullptr,L"O jogo nao iniciou corretamente. Consulte run-log.txt.\nThe game did not start correctly. See run-log.txt.",L"Code Veronica",MB_OK|MB_ICONERROR);
    return result;
}
