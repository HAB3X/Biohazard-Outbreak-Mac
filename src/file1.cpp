// Biohazard Outbreak (File 1) for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "controls.h"
#include "ps2/runtime.h"

int main(int argc, char** argv)
{
    static Ps2GameInfo game = {"Biohazard Outbreak", "SLPM_654.28", 1294104};
    BiohazardControls::Apply(game);
    return ps2_main(argc, argv, &game);
}
