@echo off
REM Static analysis over the C++ files this branch changes, using the clang-tidy
REM that ships with Visual Studio. Run it before opening a pull request.
REM
REM   buildscripts\lint-changed.cmd            analyse files changed vs the base commit
REM   buildscripts\lint-changed.cmd <file>...  analyse specific files
REM
REM Set LINT_BASE to override the base commit the diff is taken against.
REM
REM Notes on the flags, all of which are needed to get useful output here:
REM   --driver-mode=cl  the compile database records MSVC command lines, so clang
REM                     must read them as MSVC arguments rather than its own
REM   /Y-               ignore the MSVC precompiled header, which clang cannot read
REM   --header-filter   report only our own headers; the VST3 SDK and Qt produce
REM                     hundreds of warnings we neither own nor can fix
REM   vcvars64          sets INCLUDE, without which clang cannot find MSVC's
REM                     standard library and every file fails with "'array' file not found"

setlocal enabledelayedexpansion

REM Locate Visual Studio rather than assuming an edition or version. Set VSINSTALL,
REM VCVARS or TIDY in the environment to override any of it.
if not defined VSINSTALL (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -property installationPath 2^>nul`) do set "VSINSTALL=%%I"
    )
)
if not defined VCVARS set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not defined TIDY set "TIDY=%VSINSTALL%\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
if not defined BUILD set "BUILD=build/audacity-debug"
set "FILTER=au3/libraries/au3-vst3|src/mcp|src/effects/vst"

if not exist "%TIDY%" (
    echo clang-tidy not found at "%TIDY%"
    echo It ships with Visual Studio - install the "C++ Clang tools for Windows"
    echo component, or set TIDY to a clang-tidy.exe yourself.
    exit /b 1
)
if not exist "%VCVARS%" (
    echo vcvars64.bat not found at "%VCVARS%" - set VSINSTALL or VCVARS.
    exit /b 1
)
if not exist "%BUILD%/compile_commands.json" (
    echo No compile_commands.json in %BUILD% - configure the build first.
    exit /b 1
)

call "%VCVARS%" >nul 2>&1

if not "%~1"=="" (
    set COUNT=0
    for %%F in (%*) do call :analyse "%%F"
    echo.
    echo Analysed !COUNT! file^(s^).
    exit /b 0
)

REM Resolve a base commit. This must never fall through silently: a base that
REM does not resolve produces an empty file list, and the script would then
REM report success having analysed nothing at all.
set "BASE="
for %%R in ("%LINT_BASE%" upstream/master origin/master upstream/main origin/main) do (
    if not defined BASE (
        for /f "delims=" %%H in ('git rev-parse --verify --quiet "%%~R^{commit}" 2^>nul') do set "BASE=%%H"
    )
)

REM Fall back to the fork point of a shallow clone, where no upstream branch ref
REM exists to diff against but the boundary commit is exactly what we branched from.
if not defined BASE (
    for /f "delims=" %%G in ('git rev-parse --git-dir') do set "GITDIR=%%G"
    if exist "!GITDIR!\shallow" (
        for /f "delims=" %%C in ('type "!GITDIR!\shallow"') do (
            git merge-base --is-ancestor %%C HEAD >nul 2>&1
            if not errorlevel 1 set "BASE=%%C"
        )
    )
)

if not defined BASE (
    echo Could not resolve a base commit to diff against.
    echo Tried: LINT_BASE, upstream/master, origin/master, upstream/main, origin/main,
    echo and the fork point of a shallow clone. None of them resolved.
    echo.
    echo Set one explicitly, or pass files directly:
    echo   set LINT_BASE=^<commit^> ^&^& buildscripts\lint-changed.cmd
    echo   buildscripts\lint-changed.cmd path\to\file.cpp
    exit /b 1
)

echo Base: %BASE%
set COUNT=0
for /f "delims=" %%F in ('git diff --name-only %BASE%..HEAD -- "*.cpp" "*.h"') do (
    call :analyse "%%F"
)
echo.
if !COUNT!==0 (
    echo No C++ files changed since %BASE% - nothing to analyse.
) else (
    echo Analysed !COUNT! file^(s^).
)
exit /b 0

:analyse
if not exist "%~1" exit /b 0
set /a COUNT+=1
echo.
echo === %~1
"%TIDY%" -p "%BUILD%" --quiet ^
    --extra-arg-before=--driver-mode=cl --extra-arg=/Y- ^
    --header-filter="%FILTER%" ^
    --checks="-*,bugprone-*,concurrency-*,clang-analyzer-core*,clang-analyzer-cplusplus*,cert-err*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions" ^
    "%~1" 2>&1 | findstr /C:"warning:" /C:"error:"
exit /b 0
