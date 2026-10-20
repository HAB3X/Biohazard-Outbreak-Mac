// The tables a game's recompiled code gives the runtime, empty: for the runtime's tests, which run without a game
#include "ps2/recomp.h"

#include "ee/vu1jit.h"

// The runtime's tables a game would give: none
extern "C" {
const Ps2FunctionEntry ps2_function_table[1] = {};
const u32 ps2_function_count = 0;
const Ps2OverlayFunction ps2_overlay_function_table[1] = {};
const u32 ps2_overlay_function_count = 0;
const Ps2Overlay ps2_overlays[1] = {};
const u32 ps2_overlay_count = 0;
}

// VU1's translated programs a game would give: none
namespace Ee::Vu1Jit
{
const Signature g_Signatures[1] = {};
const u32 g_SignatureCount = 0;
const Program g_Programs[1] = {};
const u32 g_ProgramCount = 0;
const Signature g_SignaturesVu0[1] = {};
const u32 g_SignatureCountVu0 = 0;
const Program g_ProgramsVu0[1] = {};
const u32 g_ProgramCountVu0 = 0;
}
