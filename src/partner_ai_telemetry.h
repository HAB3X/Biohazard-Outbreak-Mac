// What the partners do, measured: with $BH_AI_LOG naming a file, each partner's state every half second and the events that
// count (attacks, damage to enemies, heals, the player's commands) are written there, a line each, for tools/ai_report.py to
// sum up. Off (nothing read, nothing written) without it. The game's own and this module's AI are measured the same way
#pragma once

#include "partner_ai_world.h"

namespace PartnerAi
{
// Whether $BH_AI_LOG names a log (the first call opens it)
bool Measuring();
// Called once a frame for each partner, after its think, with the world as it is then
void Measure(const World& world, int partner, uint64_t frame);
}
