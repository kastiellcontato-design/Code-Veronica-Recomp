// Small GUI entry point. Regional processes keep their LiveSplit-compatible names.
#include <windows.h>
#include <filesystem>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,32768);
    if(!length||length>=32768)return 1;
    const auto directory=std::filesystem::path(path).parent_path()/L"bin";
    const auto executable=directory/L"CodeVeronicaPort.exe";
    std::wstring command=L"\""+executable.wstring()+L"\"";
    if(arguments&&*arguments){command+=L" ";command+=arguments;}
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,
                       nullptr,directory.c_str(),&startup,&process)) {
        MessageBoxW(nullptr,
            L"Nao foi possivel abrir o launcher. Extraia o ZIP completo e mantenha a pasta bin junto deste executavel.\n\n"
            L"Could not open the launcher. Extract the full ZIP and keep the bin folder beside this executable.",
            L"Code Veronica",MB_OK|MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
