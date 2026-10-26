@echo off
rem Build Lexi.exe - uses MSVC (cl) if available, otherwise MinGW-w64 (g++).
rem For MSVC: run this from "x64 Native Tools Command Prompt for VS".

where cl >nul 2>nul
if %errorlevel%==0 (
    echo Building with MSVC...
    cl /nologo /std:c++17 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE lexi_win32.cpp /Fe:Lexi.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib
    goto done
)

where g++ >nul 2>nul
if %errorlevel%==0 (
    echo Building with MinGW-w64...
    g++ -std=c++17 -O2 -mwindows -static -o Lexi.exe lexi_win32.cpp -luser32 -lgdi32 -lcomdlg32
    goto done
)

echo No C++ compiler found. Install Visual Studio (Desktop development with C++) or MSYS2 MinGW-w64.
exit /b 1

:done
if exist Lexi.exe (
    echo Done: Lexi.exe
) else (
    echo Build failed.
)
