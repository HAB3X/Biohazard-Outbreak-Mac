// The lobby's and game server's messages in the log (src/lobbytrace.cpp), so a run's log shows what was said
#pragma once

#include "ps2/recomp.h"

namespace LobbyTrace
{
// A connection opened (its handle, the remote port in network order), its data sent or received, and closed
void Opened(s32 handle, u16 remotePort);
void Sent(s32 handle, const u8* data, u32 size);
void Received(s32 handle, const u8* data, u32 size);
void Closed(s32 handle);
}
