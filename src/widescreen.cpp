// Widescreen (16:9) in both games: the runtime shows the picture 16:9 and draws the 2D (menus, text, movies: the EE's packets)
// in its middle 4:3 (Ps2GameInfo::widescreen); the games' 3D is made wider here.
//
// Every projection the games make goes through one function, SetPerspective(matrix slot a0, near f12, far f13, field of view
// f14, aspect f15) (File 2's 0x18F4C0, File 1's 0x18F0E0), whose body (0x18F530, 0x18F150) makes the matrix's x scale
// cot(fov / 2) / aspect and its y scale cot(fov / 2): the field of view is the vertical one. Its three callers (in the submain
// overlay: the camera's reset 0x348800 / 0x345480, its update 0x3489A0 / 0x345740, a fixed 45 degree view 0x348A60 / 0x345800)
// each load an aspect of 4/3 from gp's data (File 2's 0x2491F4, 0x2491FC, 0x249208; File 1's 0x239D38, 0x239D48, 0x239DA0).
// With the aspect 4/3 times greater (16:9's), the view is as high and a third wider (Hor+), and the matrix's x scale three
// quarters of what it was: drawn into the same 4:3 buffers and shown 16:9, the 3D keeps its shape. The VU1 programs' clipping
// works from the projection, so nothing is clipped at the wider view's edges.
#include "widescreen.h"

#include "ps2/runtime.h"

namespace Widescreen
{
void SetProjection(Ps2Cpu* c, void (*set)(Ps2Cpu*))
{
    // 16:9 over 4:3
    constexpr float Widening = 4.0f / 3.0f;
    if (ps2_widescreen() != 0)
    {
        c->f[15].f *= Widening;
    }
    set(c);
}
}
