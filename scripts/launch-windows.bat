@echo off
setlocal
cd /d "%~dp0.."
where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake is required. Install CMake and Visual Studio 2022 with C++ Desktop Development.
  pause
  exit /b 1
)
cmake -S . -B build
if errorlevel 1 (pause & exit /b 1)
cmake --build build --config Release
if errorlevel 1 (pause & exit /b 1)
if exist "build\Release\astra-studio.exe" (
    "build\Release\astra-studio.exe"
) else (
    "build\astra-studio.exe"
)
pause
