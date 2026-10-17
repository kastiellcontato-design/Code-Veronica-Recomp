@echo off
rem Optimized build of the Code Veronica recomp into build-release\bin.
rem The Debug build (build-x64) compiles the lifted game code with /Od, which
rem leaves the guest at ~98%% of frame time and makes FMV playback stutter.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build-release\CMakeCache.txt (
 cmake -S . -B build-release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" ^
  "-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" ^
  "-DCMAKE_LINKER=C:/Program Files/LLVM/bin/lld-link.exe" ^
  "-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2" ^
  "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2" ^
  -DPS3RECOMP_DIR=D:/ps3recomp ^
  -DRECOMP_DIR=D:/ps3recomp/src/recomp ^
  -DSPU_DIR=D:/ps3recomp/projects/code_veronica/spu ^
  -DPORT_ENABLE_SPU=ON ^
  -DPS3RECOMP_BUILD_TESTS=OFF ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=D:/ps3recomp/projects/code_veronica/build-release/bin || exit /b 1
)
rem The lifted PPU units are ~45 MB each; cap parallel jobs to bound memory.
cmake --build build-release -j 6 || exit /b 1
rem Optional DXC testing: use the matched compiler/validator from the Windows
rem SDK already selected by vcvars64; these are NOT required for FXC.
if "%CV_DXC_TESTS%"=="1" (
 if not exist "%WindowsSdkVerBinPath%x64\dxcompiler.dll" exit /b 1
 if not exist "%WindowsSdkVerBinPath%x64\dxil.dll" exit /b 1
 copy /y "%WindowsSdkVerBinPath%x64\dxcompiler.dll" "build-release\bin\dxcompiler.dll" >nul || exit /b 1
 copy /y "%WindowsSdkVerBinPath%x64\dxil.dll" "build-release\bin\dxil.dll" >nul || exit /b 1
 cmake --build build-release --target code_veronica_dxc_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_dxc_tests.exe || exit /b 1
)
rem Optional focused shader regression tests; the normal game build is unchanged.
if "%CV_FP_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_fp_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_fp_tests.exe || exit /b 1
)
rem Keep build-release.cmd as the single entry point when validating both
rem supported regions; the Japanese executable shares the same runtime.
if "%CV_BUILD_JP%"=="1" call "%~dp0build-release-jp.cmd" || exit /b 1

rem Save/load checks run only on request, in isolated temporary folders.
if "%CV_SAVE_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_save_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_save_tests.exe || exit /b 1
)
rem Native menu checks use synthetic input without a game or GPU.
if "%CV_MENU_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_menu_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_menu_tests.exe || exit /b 1
)
rem Optional American executable; separate lift and build, same shared runtime.
if "%CV_BUILD_US%"=="1" (
 if not exist build-release-us\CMakeCache.txt (
  cmake -S . -B build-release-us -G Ninja ^
   -DCMAKE_BUILD_TYPE=Release ^
   "-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" ^
   "-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" ^
   "-DCMAKE_LINKER=C:/Program Files/LLVM/bin/lld-link.exe" ^
   "-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2" ^
   "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2" ^
   -DPS3RECOMP_DIR=D:/ps3recomp ^
   -DRECOMP_DIR=D:/ps3recomp/src/recomp_us ^
   -DCV_REGION=US ^
   -DSPU_DIR=D:/ps3recomp/projects/code_veronica/spu ^
   -DPORT_ENABLE_SPU=ON ^
   -DPS3RECOMP_BUILD_TESTS=OFF ^
   -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=D:/ps3recomp/projects/code_veronica/build-release/bin || exit /b 1
 )
 cmake --build build-release-us -j 6 || exit /b 1
)

rem SRT memory/layout regression tests, isolated from real saves/settings.
if "%CV_SPU_FAULT_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_spu_fault_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_spu_fault_tests.exe || exit /b 1
)
if "%CV_SPU_SCHEDULE_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_spu_schedule_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_spu_schedule_tests.exe || exit /b 1
)

if "%CV_SRT_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_srt_tests -j 6 || exit /b 1
 build-release\bin\cv_srt_tests_eu.exe codex_diag_srt || exit /b 1
 if "%CV_BUILD_JP%"=="1" (
  cmake --build build-release-jp --target code_veronica_srt_tests -j 6 || exit /b 1
  build-release\bin\cv_srt_tests_jp.exe || exit /b 1
 )
 if "%CV_BUILD_US%"=="1" (
  cmake --build build-release-us --target code_veronica_srt_tests -j 6 || exit /b 1
  build-release\bin\cv_srt_tests_us.exe || exit /b 1
 )
)

rem Map test tools: isolated guest-memory and input/dispatch checks.
if "%CV_MAP_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_map_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_map_tests.exe || exit /b 1
)
