// Capcom's module loader (MODULES/NETMOD/LOADHIGH.IRX, "Capcom_Module_Load_High"), stood in natively: its first argument is a
// module's path, loaded high in the IOP's memory and started with the rest of the arguments (its module_start, 0x0); the
// network setup loads its modules this way (the USB searcher, the AVE-TCP wrapper...). The runtime's loader does the load.
#include "network.h"

#include "ps2/iop.h"

namespace Network
{
void RegisterLoadHigh()
{
    Ps2::IopRegisterModule("Capcom_Module_Load_High", [](const std::vector<std::string>& args) {
        if (args.empty())
        {
            Ps2::Fatal("LOADHIGH started with no module to load");
        }
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (Ps2::IopLoadModule(args[0], rest) < 0)
        {
            Ps2::Fatal("LOADHIGH: %s didn't load", args[0].c_str());
        }
    });
}
}
