@echo off
REM Static analysis over the C++ files this branch changes, using the clang-tidy
REM that ships with Visual Studio. Run it before opening a pull request.
REM
REM   buildscripts\lint-changed.cmd            analyse files changed vs origin/master
REM   buildscripts\lint-changed.cmd <file>...  analyse specific files
REM
REM Notes on the flags, all of which are needed to get useful output here:
REM   --driver-mode=cl  the compile database records MSVC command lines, so clang
REM                     must read them as MSVC arguments rather than its own
REM   /Y-               ignore the MSVC precompiled header, which clang cannot read
REM   --header-filter   report only our own headers; the VST3 SDK and Qt produce
REM                     hundreds of warnings we neither own nor can fix
REM   vcvars64          sets INCLUDE, without which clang cannot find MSVC's
REM                     standard library and every file fails with "'array' file not found"

setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "TIDY=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
set "BUILD=build/audacity-debug"
set "FILTER=au3/libraries/au3-vst3|src/mcp|src/effects/vst"

if not exist "%TIDY%" (
    echo clang-tidy not found at "%TIDY%"
    exit /b 1
)
if not exist "%BUILD%/compile_commands.json" (
    echo No compile_commands.json in %BUILD% - configure the build first.
    exit /b 1
)

call "%VCVARS%" >nul 2>&1

if "%~1"=="" (
    for /f "delims=" %%F in ('git diff --name-only origin/master...HEAD -- "*.cpp" "*.h"') do (
        call :analyse "%%F"
    )
) else (
    for %%F in (%*) do call :analyse "%%F"
)
exit /b 0

:analyse
if not exist "%~1" exit /b 0
echo.
echo === %~1
"%TIDY%" -p "%BUILD%" --quiet ^
    --extra-arg-before=--driver-mode=cl --extra-arg=/Y- ^
    --header-filter="%FILTER%" ^
    --checks="-*,bugprone-*,concurrency-*,clang-analyzer-core*,clang-analyzer-cplusplus*,cert-err*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions" ^
    "%~1" 2>&1 | findstr /C:"warning:" /C:"error:"
exit /b 0
