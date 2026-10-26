// File 2's enemies in File 1 (transplant.h).
//
// File 1's enemy creation gives an enemy table[map[kind]] (the game overlay's u16 map at 0x6D7040, its 30 routines at
// 0x6D7110), the routine its frames call with the enemy in a0; a kind File 1 has no AI for maps to routine 0, the stand-in
// at 0x601ED0. The carried kinds' code and the File 2 data it uses are one block for memory at transplant_base (free in every
// File 1 dump taken), made by tools/bh_transplant.py: the block is put there the first time a carried enemy runs (and again
// should anything clear it), since its code reads its own data there and the runtime finds its functions by their words in
// memory. Without File 2's disc at build time no kind is carried and the stand-in is the game's.
#include "transplant.h"

#include <cstdint>
#include <cstring>

#if TRANSPLANT
extern "C" const uint32_t transplant_base;
extern "C" const uint32_t transplant_size;
extern "C" const uint8_t transplant_blob[];
extern "C" const uint32_t transplant_count;
extern "C" const uint32_t transplant_routines[][2];     // {kind, its routine in the block}
#endif

namespace Transplant
{
namespace
{
constexpr uint32_t EnemyKind = 3;        // the enemy's kind byte (File 1's and File 2's enemy tables alike)

#if TRANSPLANT
uint32_t RoutineOf(uint8_t kind)
{
    for (uint32_t i = 0; i < transplant_count; i++)
        if (transplant_routines[i][0] == kind)
            return transplant_routines[i][1];
    return 0;
}

void EnsureLoaded()
{
    // (the block's first words are its first function's: they stand for the whole, whose data the carried code writes as
    // it runs, so it is put in place once and not refreshed)
    if (std::memcmp(ps2_mem + transplant_base, transplant_blob, 16) != 0)
        std::memcpy(ps2_mem + transplant_base, transplant_blob, transplant_size);
}
#endif
}

void StandIn(Ps2Cpu* c, Ps2Function original)
{
#if TRANSPLANT
    if (uint32_t routine = RoutineOf(rd8(c->r[4].uw[0] + EnemyKind)))
    {
        EnsureLoaded();
        ps2_call(c, routine);
        return;
    }
#endif
    original(c);
}
}
