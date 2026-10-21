// Port 0's DualShock as the game reads it (game_pad.h)
//
// The frame reading adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT):
// game.c's mc_pad_read.
#include "game_pad.h"

#include "ps2/recomp.h"

#include <algorithm>
#include <cmath>

namespace GamePad
{
namespace
{
// libpad2's frames: two of 0x80 bytes, the newer by the counter at +0x7C; the pad data's length at +2, the data from +0x1C
// (buttons active low, then the right stick's x and y, the left's)
constexpr uint32_t FrameSize = 0x80, FrameCount = 0x7C, FrameLength = 0x02, FrameButtons = 0x1C;
constexpr uint32_t FrameRx = 0x1E, FrameRy = 0x1F, FrameLx = 0x20, FrameLy = 0x21;
constexpr float DeadZone = 7000.0f / 32767.0f;

float Axis(uint32_t raw, bool flip)
{
    float v = std::clamp((static_cast<float>(raw) - 128.0f) / 127.0f, -1.0f, 1.0f);
    if (std::fabs(v) < DeadZone)
    {
        v = 0;
    }
    return flip ? -v : v;
}
}

State Read(uint32_t framesPointer)
{
    State pad;
    uint32_t block = rd32(framesPointer);
    if (block < 0x100000 || block + 2 * FrameSize > 0x2000000 || (block & 3) != 0)
    {
        return pad;
    }
    uint32_t frame = block;
    if (static_cast<int32_t>(rd32(block + FrameCount)) < static_cast<int32_t>(rd32(block + FrameSize + FrameCount)))
    {
        frame = block + FrameSize;
    }
    uint8_t length = rd8(frame + FrameLength);
    if (length < 2)
    {
        return pad;
    }
    pad.valid = true;
    pad.held = ~rd16(frame + FrameButtons) & 0xFFFF;
    if (length >= 6)
    {
        pad.rx = Axis(rd8(frame + FrameRx), false);
        pad.ry = Axis(rd8(frame + FrameRy), true);
        pad.lx = Axis(rd8(frame + FrameLx), false);
        pad.ly = Axis(rd8(frame + FrameLy), true);
    }
    return pad;
}
}
