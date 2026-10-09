@echo off
rem recorder25 - x86 Native Tools Command Prompt for VS (32-bit). No PhysX SDK needed.
cl /nologo /O2 /W3 /MT recorder25.c /LD /link /DEF:recorder25.def /OUT:PhysXLoader.dll kernel32.lib
if errorlevel 1 exit /b 1
del /q *.obj PhysXLoader.exp PhysXLoader.lib 2>nul
echo Built PhysXLoader.dll (recorder25 - original PhysX 2.5 recorder)
