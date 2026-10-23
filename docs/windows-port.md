# Windows: what a port would take (assessed 2026-10-23)

Method: every source file of the runtime (`ps2-native-runtime/runtime/src`, `hw`) and of this repository's `src/` was syntax-checked with
`x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only` against Homebrew's SDL3 headers (nothing was linked or run: there is no Windows machine here).

## Already portable
- The game layer (`src/*.cpp`: the menus, controls, partner AI, mods, widescreen ...) compiles clean except `lobbytrace.cpp` (BSD sockets).
- The recompiled game code is plain C (`build/generated`), and the runtime has both a Metal and an OpenGL presenter/device
  (`presentgl.cpp`, `gldevice.cpp`), so the graphics don't have to be written again. `cpu.cpp` already switches host stacks on x86-64 as well as arm64.

## What fails, and the fix each needs
| file | problem | fix |
|---|---|---|
| `memory.cpp` | the EE's 4 GB address space is an `mmap` reservation with a SIGSEGV handler that names the guest address; `execinfo.h` | `VirtualAlloc(MEM_RESERVE)` plus `MapViewOfFile`/`VirtualAlloc` for the mirrors, a vectored exception handler; drop the backtrace |
| `cpu.cpp` | `mmap`/`mprotect` host stacks; `CallOnHostStack`'s x86-64 asm uses the System V registers (rdi); `_longjmp` across a switched stack | `VirtualAlloc` stacks with a guard page; rcx for the Windows x64 ABI; check that `_longjmp` doesn't try to unwind the foreign stack (use `RtlRestoreContext` or fibers) |
| `log.cpp` | mach thread time (`mach_thread_self`, `mach_timebase_info`) for the per-thread CPU counters | `GetThreadTimes` / `QueryThreadCycleTime` |
| `control.cpp` | `mkfifo` for `--control` (a test tool, not needed to play) | a named pipe, or leave it out on Windows |
| `iop/avetcp*.cpp`, `src/lobbytrace.cpp` | BSD sockets (`arpa/inet.h`) | winsock2 shim (`WSAStartup`, `closesocket`, `ioctlsocket`); this is the online play |
| `kernel.cpp`, `iop/mc2.cpp`, `iop/mcserv.cpp`, `iop/servers.cpp` | `localtime_r`, `gmtime_r`, `tm_gmtoff` | `localtime_s`/`gmtime_s`; the offset from `_get_timezone` |
| `ipu/mpeg.cpp` | `M_PI` | `_USE_MATH_DEFINES` or a constant |
| `hw/ee/vu1translate.cpp` | `slots = {}` on an aggregate with gcc | `slots = Slots{}` (clang takes it as is) |
| CMake | `project(... OBJCXX)`, `-framework Metal`, `metaldevice.mm`, `presentmetal.mm` | build those only on Apple; find SDL3 for MSVC/MinGW |
| the apps | `make_app.py` and the launcher are macOS-only (`.app`, `ditto`, ad hoc signing) | a plain folder or installer and a Windows launcher |

`overlay.cpp` also showed an error in the check, only because the `PS2_FONTS_DIR` definition comes from CMake.

## Verdict
Not hard to *compile* (about a dozen files), but the two hard parts are `memory.cpp` (the 4 GB address trick and its fault handler: the whole speed of the
runtime rests on a guest load being one host load) and `cpu.cpp` (stack switching and `longjmp` under the Windows x64 ABI). Each is a day or two of careful work, and none of it
can be tested without a Windows machine. The recompiled game code needs no change. A Windows build would also need the OpenGL device checked against the games' draw
paths (Metal is the one that has been used), and a Windows signing/packaging story.

If the aim is only "play it on Windows", the existing Windows recompilation of the same games by another author may be the shorter road; what this repository adds on top is the English patch, the PC controls, the
partner AI and the editor tools, which are portable (the Python tools and the game layer).
