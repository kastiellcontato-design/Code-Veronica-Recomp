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
rem Optional focused shader regression tests; the normal game build is unchanged.
if "%CV_FP_TESTS%"=="1" (
 cmake --build build-release --target code_veronica_fp_tests -j 6 || exit /b 1
 build-release\bin\code_veronica_fp_tests.exe || exit /b 1
)
