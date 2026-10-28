@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "%~dp0"
cl /nologo /O2 /c /W0 /D_CRT_SECURE_NO_WARNINGS third_party\puff\puff.c || exit /b 1
set "WINRT=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt"
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /I third_party /I "%WINRT%" src\main.cpp src\engine.cpp src\desktop.cpp src\knowledge.cpp src\sessions.cpp src\ocr.cpp puff.obj /Fe:..\Valhalla.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib kernel32.lib dwmapi.lib gdiplus.lib comctl32.lib winhttp.lib uxtheme.lib shlwapi.lib ole32.lib oleaut32.lib shell32.lib comdlg32.lib uuid.lib UIAutomationCore.lib windowsapp.lib || exit /b 1
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /I third_party src\kb_test.cpp src\knowledge.cpp puff.obj /Fe:kb_test.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib || exit /b 1
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /I third_party /I "%WINRT%" src\agent_test.cpp src\sessions.cpp src\desktop.cpp src\ocr.cpp /Fe:agent_test.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib gdi32.lib ole32.lib oleaut32.lib uuid.lib UIAutomationCore.lib windowsapp.lib
exit /b %ERRORLEVEL%
