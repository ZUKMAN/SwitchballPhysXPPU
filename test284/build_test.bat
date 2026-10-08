@echo off
rem x86 Native Tools Command Prompt. PHYSX284_SDK = folder with Physics, Foundation, Cooking, PhysXLoader
setlocal
if "%PHYSX284_SDK%"=="" set "PHYSX284_SDK=C:\Program Files (x86)\NVIDIA Corporation\NVIDIA PhysX SDK\v2.8.4_win\SDKs"
if not exist "%PHYSX284_SDK%\Physics\include\NxPhysics.h" (
    echo PhysX SDK 2.8.4 headers not found in "%PHYSX284_SDK%" - set PHYSX284_SDK
    exit /b 1
)
set INC=/I"%PHYSX284_SDK%\Physics\include" /I"%PHYSX284_SDK%\Foundation\include" /I"%PHYSX284_SDK%\Cooking\include" /I"%PHYSX284_SDK%\PhysXLoader\include"
cl /nologo /O2 /W3 /MT /EHsc /DWIN32 /DNDEBUG %INC% test284.cpp /link /OUT:test284.exe kernel32.lib
if errorlevel 1 exit /b 1
del /q *.obj 2>nul
echo Built test284.exe
endlocal
