@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "%~dp0"
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /I third_party src\main.cpp src\engine.cpp src\desktop.cpp /Fe:..\Valhalla.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib kernel32.lib dwmapi.lib gdiplus.lib comctl32.lib winhttp.lib uxtheme.lib shlwapi.lib ole32.lib oleaut32.lib shell32.lib UIAutomationCore.lib
exit /b %ERRORLEVEL%
