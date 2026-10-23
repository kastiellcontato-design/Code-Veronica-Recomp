@echo off
rem Optimized build of the Japanese version (NPJB00135) into
rem build-release\bin\CodeVeronicaPort_JP.exe, beside the European one.
rem Needs the JP lift in src\recomp_jp (see cv_version.h for how it is made);
rem the European build is build-release.cmd.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build-release-jp\CMakeCache.txt (
 cmake -S . -B build-release-jp -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" ^
  "-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" ^
  "-DCMAKE_LINKER=C:/Program Files/LLVM/bin/lld-link.exe" ^
  "-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2" ^
  "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2" ^
  -DPS3RECOMP_DIR=D:/ps3recomp ^
  -DRECOMP_DIR=D:/ps3recomp/src/recomp_jp ^
  -DCV_REGION=JP ^
  -DSPU_DIR=D:/ps3recomp/projects/code_veronica/spu ^
  -DPORT_ENABLE_SPU=ON ^
  -DPS3RECOMP_BUILD_TESTS=OFF ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=D:/ps3recomp/projects/code_veronica/build-release/bin || exit /b 1
)
rem The lifted PPU units are ~45 MB each; cap parallel jobs to bound memory.
cmake --build build-release-jp -j 6 || exit /b 1
