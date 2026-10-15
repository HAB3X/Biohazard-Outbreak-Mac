// Biohazard Outbreak File 2 for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "ps2/runtime.h"

int main(int argc, char** argv)
{
    static const Ps2GameInfo game = {"Biohazard Outbreak File 2", "SLPM_656.92", 1356096};
    return ps2_main(argc, argv, &game);
}
