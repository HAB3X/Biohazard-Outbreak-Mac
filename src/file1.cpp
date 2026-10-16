// Biohazard Outbreak (File 1) for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "ps2/runtime.h"
#include "tsnd.h"

int main(int argc, char** argv)
{
    static const Ps2GameInfo game = {"Biohazard Outbreak", "SLPM_654.28", 1294104};
    Tsnd::Register();
    return ps2_main(argc, argv, &game);
}
