@echo off
setlocal
cd /d "%~dp0"
rem Same DXC entry point/settings as the user's run. Capture only on a DMA
rem rejection, with no frame-by-frame traces and no change to DMA validation.
set "SPU_FAULT_DIR=%~dp0codex_diag_hang"
if not exist "%SPU_FAULT_DIR%" mkdir "%SPU_FAULT_DIR%"
if not exist "%SPU_FAULT_DIR%" exit /b 1
echo Diagnostico de congelamento: captura somente se houver erro de SPU.
echo Os arquivos ficam em codex_diag_hang. Jogue normalmente.
call "%~dp0executar-code-veronica-dxc-experimental.cmd"
endlocal
