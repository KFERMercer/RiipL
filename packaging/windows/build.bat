@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\..\.."

REM Requirements on PATH: CMake, a C++17 toolchain (MSVC or Ninja+cl),
REM and Qt 6 with windeployqt (set CMAKE_PREFIX_PATH if Qt is not system-wide).

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || goto :fail
cmake --build build --config Release -j %NUMBER_OF_PROCESSORS% || goto :fail

set "EXE="
for /r build %%f in (RiipL.exe) do set "EXE=%%f"
if not defined EXE (
    echo RiipL.exe not found in build tree
    goto :fail
)

windeployqt --release --no-system-d3d-compiler --no-opengl-sw --no-translations "%EXE%" || goto :fail

for /f "tokens=3" %%v in ('findstr /C:"project(RiipL VERSION" CMakeLists.txt') do set "VERSION=%%v"
set "VERSION=%VERSION::=-%"
set "ZIP=RiipL-%VERSION%-win64.zip"

for %%f in ("%EXE%") do set "EXEDIR=%%~dpf"
if exist "%ZIP%" del "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%EXEDIR%*' -DestinationPath '%ZIP%'"

echo artifact: %ZIP%
exit /b 0

:fail
echo build failed
exit /b 1
