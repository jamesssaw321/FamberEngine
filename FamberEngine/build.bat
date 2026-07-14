@echo off
REM FamberEngine build (MinGW g++).
setlocal
set GXX=C:\mingw64\bin\g++.exe
"%GXX%" -std=c++17 -O2 -DWIN32_LEAN_AND_MEAN -static -static-libgcc -static-libstdc++ src\game\main.cpp src\platform\gl.cpp -o FamberEngine.exe -lopengl32 -lgdi32 -luser32 -lwinmm -lws2_32
if %ERRORLEVEL%==0 (echo BUILD OK: FamberEngine.exe) else (echo BUILD FAILED)
endlocal
