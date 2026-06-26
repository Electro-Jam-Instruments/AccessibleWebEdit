@echo off
call "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /EHsc /nologo /std:c++17 /Fe:C:\Temp\uiaprobe_events.exe /Fo:C:\Temp\ "C:\Dev\Projects\45 - AccWebEdit\scripts\uiaprobe_events.cpp" ole32.lib oleaut32.lib uuid.lib user32.lib
echo COMPILE_EXIT=%ERRORLEVEL%
