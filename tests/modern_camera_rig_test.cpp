// The Modern camera's rig (src/modern_camera_rig.h): the shot's geometry, the follow heading and the walls, against values
// worked out by hand. Run by ctest (or the executable alone); a failed check prints its line and the run fails.
#include "modern_camera_rig.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace ModernCamera;

namespace
{
int g_failures = 0;

#define CHECK(condition)                                                                                                       \
    do                                                                                                                         \
    {                                                                                                                          \
        if (!(condition))                                                                                                      \
        {                                                                                                                      \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                        \
            g_failures++;                                                                                                      \
        }                                                                                                                      \
    } while (0)

bool Near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

void AnglesWrapIntoAHalfTurnEachWay()
{
    CHECK(Near(AngleWrap(0), 0));
    CHECK(Near(AngleWrap(190), -170));
    CHECK(Near(AngleWrap(-190), 170));
    CHECK(Near(AngleWrap(720 + 45), 45));
    CHECK(Near(AngleWrap(180), -180));
}

void SmoothingIsTheSameAtAnyFrameRate()
{
    // Two 60ths of a second at k equal one 30th
    float a60 = SmoothAlpha(0.25f, 1.0f / 60);
    float twice = 1 - (1 - a60) * (1 - a60);
    CHECK(Near(SmoothAlpha(0.25f, 1.0f / 30), twice, 1e-4f));
    CHECK(SmoothAlpha(0, 0.1f) == 0);
    CHECK(SmoothAlpha(1, 0.1f) == 1);
}

void TheEyeSitsBehindAndBesideHer()
{
    // Facing +z (yaw 0 seats the eye along +z: the yaw is the eye's way from her), level, 300 back, 50 over the right shoulder
    Rig rig;
    rig.distance = 300;
    rig.shoulder = 50;
    rig.height = 130;
    Vec3 eye, look;
    EyeAndLook(1000, 0, 2000, 0, 0, rig, &eye, &look);
    CHECK(Near(eye.x, 1050) && Near(eye.y, 130) && Near(eye.z, 2300));
    CHECK(Near(look.x, 1050) && Near(look.y, 130) && Near(look.z, 2000));
    // Pitched up 30 degrees: the eye rises by distance * sin and comes in by distance * (1 - cos)
    EyeAndLook(0, 0, 0, 0, 30, rig, &eye, &look);
    CHECK(Near(eye.y, 130 + 150) && Near(eye.z, 300 * std::cos(30 * DegToRad)));
}

void TheStickWalksRelativeToTheView()
{
    float heading, gap;
    // Pushed up: the view's way; right: 90 degrees clockwise of it (the game's headings turn the other way)
    CHECK(ModernMovement(0, 1, 40, 0, 0.2f, &heading, &gap) && Near(heading, 40));
    CHECK(ModernMovement(1, 0, 40, 0, 0.2f, &heading, &gap) && Near(heading, -50));
    CHECK(!ModernMovement(0.1f, 0.1f, 40, 0, 0.2f, &heading, &gap));
}

void TheBoomStopsInFrontOfAWall()
{
    // A wall across x = 100 from z -500 to 500, 0 to 300 high; the head at the origin, the eye at x 400
    const float corners[9] = {100, 0, -500, 100, 300, 500, 100, 0, 500};
    Wall wall;
    CHECK(WallFromRecord(1, corners, 0, &wall));
    std::vector<Wall> walls{wall};
    float ex = 400, ez = 0, ey = 100;
    float kept = ClampEyeToWalls(walls, &ex, &ez, &ey, 0, 0, 20, 0, true, 100, true, 125);
    // The wall at a quarter of the boom, less the margin (20 of 400)
    CHECK(Near(kept, 0.25f - 20.0f / 400.0f, 1e-3f));
    CHECK(Near(ex, 400 * kept, 0.1f));
    // Never closer than 0.15 of the boom (a margin of 55 would leave 0.1125)
    float ex3 = 400, ez3 = 0, ey3 = 100;
    CHECK(Near(ClampEyeToWalls(walls, &ex3, &ez3, &ey3, 0, 0, 55, 0, true, 100, true, 125), 0.15f, 1e-4f));
    // A floor (its normal up) isn't a wall, nor is a low step
    CHECK(!WallFromRecord(1, corners, 0.9f, &wall));
    const float step[9] = {100, 0, -500, 100, 10, 500, 100, 0, 500};
    CHECK(!WallFromRecord(1, step, 0, &wall));
    // A wall whose top is below the look point and the block height doesn't stop it
    float ex2 = 400, ez2 = 0, ey2 = 100;
    CHECK(ClampEyeToWalls(walls, &ex2, &ez2, &ey2, 0, 0, 55, 0, true, 100, true, 400) == 1.0f);
}

void TheBoomStopsUnderTheFloorAbove()
{
    // A slab at height 100 over the square x, z in [-200, 200], as two triangles (the floor above a stairwell)
    const float a[9] = {-200, 100, -200, 200, 100, -200, 200, 100, 200};
    const float b[9] = {-200, 100, -200, 200, 100, 200, -200, 100, 200};
    std::vector<Slab> slabs(2);
    CHECK(SlabFromRecord(a, -1.0f, &slabs[0]));
    CHECK(SlabFromRecord(b, -1.0f, &slabs[1]));
    // A wall's record isn't a slab
    Slab unused;
    CHECK(!SlabFromRecord(a, 0.1f, &unused));
    // From her head below it up to an eye above it: kept to the slab, 10 short (the boom is 100 long, straight up)
    CHECK(Near(BoomClearOfSlabs(slabs, {0, 50, 0}, {0, 150, 0}, 10), 0.4f));
    // Coming down through it the same
    CHECK(Near(BoomClearOfSlabs(slabs, {0, 150, 0}, {0, 50, 0}, 10), 0.4f));
    // A boom that stays under it, or passes beside it, is whole
    CHECK(Near(BoomClearOfSlabs(slabs, {0, 50, 0}, {0, 90, -150}, 10), 1.0f));
    CHECK(Near(BoomClearOfSlabs(slabs, {300, 50, 0}, {300, 150, 0}, 10), 1.0f));
}

void FollowingTakesTheWaySheWalks()
{
    // Walking along +x (heading 90) facing that way: the heading followed settles on 90; stopped, it holds
    FollowHeading f;
    float base = 0;
    bool moving = false;
    for (int i = 0; i < 120; i++)
    {
        moving = f.Update(10.0f * static_cast<float>(i), 0, 90, 1.0f / 60, true, false, &base);
    }
    CHECK(moving && Near(base, 90, 1.0f));
    for (int i = 0; i < 60; i++)
    {
        moving = f.Update(1190, 0, 90, 1.0f / 60, false, false, &base);
    }
    CHECK(!moving && Near(base, 90, 1.0f));
    // Walking backwards (facing -x while going +x): a backpedal, sat behind her facing, not her way
    FollowHeading b;
    for (int i = 0; i < 120; i++)
    {
        b.Update(10.0f * static_cast<float>(i), 0, -90, 1.0f / 60, true, false, &base);
    }
    CHECK(Near(std::fabs(AngleWrap(base + 90)), 0, 1.0f));
}
}

int main()
{
    AnglesWrapIntoAHalfTurnEachWay();
    SmoothingIsTheSameAtAnyFrameRate();
    TheEyeSitsBehindAndBesideHer();
    TheStickWalksRelativeToTheView();
    TheBoomStopsInFrontOfAWall();
    FollowingTakesTheWaySheWalks();
    TheBoomStopsUnderTheFloorAbove();
    if (g_failures != 0)
    {
        std::printf("%d checks failed\n", g_failures);
        return 1;
    }
    std::printf("modern camera rig: all checks passed\n");
    return 0;
}
