@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build-release-jp\CMakeCache.txt (
 echo Compile a versao japonesa normal uma vez com build-release-jp.cmd.
 exit /b 1
)
cmake -S . -B build-release-jp -DCV_APPEARANCE=ON -DCV_MESH_ID_DISCOVERY=OFF -DCV_SPU_NATIVE_DRAW=OFF -DCV_SPU_NATIVE_DESCRIPTORS=OFF -DCV_WORK_PROFILE=OFF || exit /b 1
cmake --build build-release-jp --target MyGameRecomp -j 3
set "APPEARANCE_BUILD_RESULT=%ERRORLEVEL%"
rem Restore the normal build selection, without replacing the normal EXE.
cmake -S . -B build-release-jp -DCV_APPEARANCE=OFF
if not "%APPEARANCE_BUILD_RESULT%"=="0" exit /b %APPEARANCE_BUILD_RESULT%
echo Pronto: build-release\bin\MyGameRecomp_JP_Characters.exe
exit /b 0
