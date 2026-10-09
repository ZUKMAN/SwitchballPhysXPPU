SwitchballPhysXPPU - PhysX 2.5.0 interface of Switchball on PhysX SDK 2.8.4
=================================================================

The translator implements the PhysX 2.5 interface the game uses on top of the
PhysX 2.8.4 SDK. Hardware mode (default) emulates a PhysX PPU: the game enables
its hardware levels and fluids, which run on the 2.8.4 software solver.

Build (x86 Native Tools Command Prompt for VS, in this folder):
    build.bat

Supported compilers (32-bit / x86 only - Switchball and PhysX 2.8.4 are 32-bit):
  - MSVC 2019 (cl 19.2x, toolset v142)  - tested (cl 19.29.30151, Build Tools 2019)
  - MSVC 2015 (cl 19.00, v140), 2017 (cl 19.1x, v141), 2022 (cl 19.3x/19.4x, v143)
      - expected to work, not tested
  - Older MSVC (2010-2013) - not supported/tested
  - x64 builds and MinGW/GCC - not supported (the PhysX 2.8.4 headers and the
    32-bit game ABI, __fastcall/__thiscall wrappers, require 32-bit MSVC)
  The PhysX 2.8.4 SDK headers are taken from PHYSX284_SDK, default:
  C:\Program Files (x86)\NVIDIA Corporation\NVIDIA PhysX SDK\v2.8.4_win\SDKs

Install into the game folder:
    PhysXLoader.dll   (built)
    SwitchballPhysXPPU.ini
    PhysX284\         PhysXCore.dll, PhysXCooking.dll, PhysXDevice.dll,
                      cudart32_30_9.dll, physxcudart_20.dll
  (the game's original PhysXLoader.dll is not used)

Settings: SwitchballPhysXPPU.ini (HardwareMode, cloth tuning, 2-4 rope crates, diagnostics).
Log: SwitchballPhysXPPU.log. "UNIMPLEMENTED <class> slot N called from X" = a 2.5 method the
game called that the translator does not implement yet.

test284\ - stand-alone test that PhysX 2.8.4 runs on this machine.

tools\recorder25\ - records moving bodies on the ORIGINAL PhysX 2.5 runtime, in the same
format as TrackMoving=1, to compare the bridge with the original (box pushing etc.):
    build_recorder25.bat (no SDK needed) -> PhysXLoader.dll
    game folder: rename the game's original PhysXLoader.dll to PhysXLoader_orig.dll,
    copy the built PhysXLoader.dll in. Output: recorder25.log. Software levels only.
