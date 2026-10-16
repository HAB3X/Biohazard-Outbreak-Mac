// Biohazard Outbreak for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "controls.h"
#include "ps2/runtime.h"
#include "tsnd.h"

int main(int argc, char** argv)
{
    static Ps2GameInfo game = {"Biohazard Outbreak", "SLPM_654.28", 1294104};
    BiohazardControls::Apply(game);
    Tsnd::Register();
    return ps2_main(argc, argv, &game);
}
