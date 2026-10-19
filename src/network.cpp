// The network configuration on the Mac. The game looks for "Your Network Configuration" on a memory card (the BWNETCNF
// directory a network setup tool writes) before it asks netcnf for the configurations in it. On the Mac the network is
// the Mac's, which the runtime's netcnf presents as the one configuration, so no card holds one: the card's check is
// replaced by "there", and the list, its entry and its settings come from the runtime (iop/netcnf.cpp there).
#include "network.h"

#include "ps2/recomp.h"

// netwk's check of the chosen card for the BWNETCNF directory (0x5C84F0, called in state 4 of the Network Play
// setup's step, 0x5C0410): -2 while libmc2 is asked, -1 when the directory is there (the setup goes on to read the configurations), any
// other value an error of the card's
extern "C" void netwk_card_has_netcnf(Ps2Cpu* c) { c->r[2].sd[0] = -1; }
