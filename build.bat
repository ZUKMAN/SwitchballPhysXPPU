@echo off
rem SwitchballPhysXPPU translator - x86 Native Tools Command Prompt for VS (32-bit!)
setlocal
if "%PHYSX284_SDK%"=="" set "PHYSX284_SDK=C:\Program Files (x86)\NVIDIA Corporation\NVIDIA PhysX SDK\v2.8.4_win\SDKs"
if not exist "%PHYSX284_SDK%\Physics\include\NxPhysics.h" (
    echo PhysX SDK 2.8.4 headers not found in "%PHYSX284_SDK%" - set PHYSX284_SDK
    exit /b 1
)
set INC=/I"%PHYSX284_SDK%\Physics\include" /I"%PHYSX284_SDK%\Foundation\include" /I"%PHYSX284_SDK%\Cooking\include" /I"%PHYSX284_SDK%\PhysXLoader\include"
cl /nologo /O2 /W3 /MT /EHsc /DWIN32 /DNDEBUG %INC% /LD src\b2_scene.cpp src\b2_objects.cpp src\b2_convert.cpp src\b2_fluid.cpp /link /DEF:SwitchballPhysXPPU.def /OUT:PhysXLoader.dll kernel32.lib
if errorlevel 1 exit /b 1
del /q *.obj PhysXLoader.exp PhysXLoader.lib 2>nul
echo Built PhysXLoader.dll (%~dp0) - SwitchballPhysXPPU translator
endlocal
