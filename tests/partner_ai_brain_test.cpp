// The partners' decisions (src/partner_ai_brain.h) on made-up worlds: who they fight, when they come back, what the player's
// commands do. Run by ctest (or the executable alone); a failed check prints its line and the run fails.
#include "partner_ai_brain.h"
#include "partner_ai_pad.h"

#include <cstdio>

using namespace PartnerAi;

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

constexpr uint32_t PlayerRecord = 0x476DD0;
constexpr uint32_t PartnerRecord = 0x477EB0;
constexpr uint32_t OtherPartnerRecord = 0x478F90;
constexpr int Partner = 1;
const Tunables Defaults{};

// The player at (1000, 1000) and partner 1 beside them in room 1; partner 2 further off
World Bar()
{
    World w{};
    w.currentRoom = 1;
    const uint32_t records[] = {PlayerRecord, PartnerRecord, OtherPartnerRecord};
    const float xs[] = {1000, 1200, 1300};
    for (int n = 0; n < 3; n++)
    {
        Character& c = w.characters[n];
        c.address = records[n];
        c.active = true;
        c.cpu = n != 0;
        c.id = static_cast<uint8_t>(n);
        c.position = {xs[n], 0, 1000};
        c.room = 1;
        c.hp = c.hpMax = 2000;
    }
    return w;
}

Enemy& AddEnemy(World& w, int index, float x, float z, uint32_t after)
{
    Enemy& e = w.enemies[index];
    e.active = true;
    e.position = {x, 0, z};
    e.room = 1;
    e.hp = e.hpMax = 700;
    e.target = after;
    return e;
}

Decision DecideOnce(const World& w, bool armed, Memory& memory, uint64_t frame = 100, int partner = Partner)
{
    return Decide(w, partner, Abilities{.gun = armed}, memory, frame, Defaults);
}

void FightsWhatIsOnThePlayer()
{
    World w = Bar();
    AddEnemy(w, 46, 1080, 1000, PlayerRecord);          // biting the player
    AddEnemy(w, 47, 1250, 1100, PartnerRecord);         // after the partner, closer to it
    Memory m;
    Decision d = DecideOnce(w, true, m);
    CHECK(d.intent == Intent::Fight);
    CHECK(d.target == 46);
    // bare hands too: shoving it off the player is worth it
    Memory bare;
    d = DecideOnce(w, false, bare);
    CHECK(d.intent == Intent::Fight);
    CHECK(d.target == 46);
}

void PrefersThePlayersEnemyToItsOwn()
{
    World w = Bar();
    AddEnemy(w, 10, 1500, 1000, PlayerRecord);  // after the player, 500 off
    AddEnemy(w, 11, 1400, 1000, PartnerRecord); // after the partner, 200 off it
    Memory m;
    Decision d = DecideOnce(w, true, m);
    CHECK(d.intent == Intent::Fight);
    CHECK(d.target == 10);
}

void UnarmedKeepsOutOfFightsNotOnThePlayer()
{
    World w = Bar();
    AddEnemy(w, 10, 1500, 1000, PlayerRecord);
    Memory m;
    Decision d = DecideOnce(w, false, m);
    CHECK(d.intent == Intent::Guard);
}

void ShootsWhatComesNearWithAGun()
{
    World w = Bar();
    AddEnemy(w, 10, 1800, 1000, OtherPartnerRecord); // 800 from the player, after someone else
    Memory m;
    Decision d = DecideOnce(w, true, m);
    CHECK(d.intent == Intent::Fight);
    CHECK(d.target == 10);
    w.enemies[10].position = {3000, 0, 1000};        // far off: stays by the player
    Memory far;
    CHECK(DecideOnce(w, true, far).intent == Intent::Guard);
}

void IgnoresSetAsideDeadAndOtherRooms()
{
    World w = Bar();
    AddEnemy(w, 1, 1050, 1000, PlayerRecord).setAside = true; // the bar's dead customers
    AddEnemy(w, 2, 1050, 1000, PlayerRecord).hp = 0;
    AddEnemy(w, 3, 1050, 1000, PlayerRecord).room = 4;
    Memory m;
    Decision d = DecideOnce(w, true, m);
    CHECK(d.intent == Intent::Free);
}

void FollowsBackWithSlack()
{
    World w = Bar();
    Memory m;
    w.characters[Partner].position = {1800, 0, 1000}; // 800 away: comes back
    CHECK(DecideOnce(w, true, m).intent == Intent::Follow);
    w.characters[Partner].position = {1400, 0, 1000}; // 400: keeps coming
    CHECK(DecideOnce(w, true, m).intent == Intent::Follow);
    w.characters[Partner].position = {1250, 0, 1000}; // 250: close enough
    CHECK(DecideOnce(w, true, m).intent == Intent::Free);
    w.characters[Partner].position = {1500, 0, 1000}; // 500: within the slack
    CHECK(DecideOnce(w, true, m).intent == Intent::Free);
    w.characters[Partner].room = 4;                   // another room
    CHECK(DecideOnce(w, true, m).intent == Intent::Follow);
}

void ThisWayBringsPartnersClose()
{
    World w = Bar();
    w.characters[Partner].position = {1280, 0, 1000}; // 280: free without a command
    Memory m;
    CHECK(DecideOnce(w, true, m, 100).intent == Intent::Free);
    w.characters[0].speech = static_cast<uint8_t>(Command::ThisWay);
    CHECK(DecideOnce(w, true, m, 101).intent == Intent::Follow);
    CHECK(m.order == Order::Come);
    // the order lapses
    w.characters[0].speech = 0;
    DecideOnce(w, true, m, 101 + Defaults.orderFrames);
    CHECK(m.order == Order::None);
}

void CallingByNameOrdersOnlyThatPartner()
{
    World w = Bar();
    Memory first, second;
    w.characters[0].speech = static_cast<uint8_t>(Command::CallSecond);
    DecideOnce(w, true, first, 100, 1);
    DecideOnce(w, true, second, 100, 2);
    CHECK(first.order == Order::None);
    CHECK(second.order == Order::Come);
}

void ACommandCountsOnceWhileItsLineIsSaid()
{
    World w = Bar();
    Memory m;
    w.characters[0].speech = static_cast<uint8_t>(Command::Go);
    DecideOnce(w, true, m, 100);
    const uint64_t until = m.orderUntil;
    DecideOnce(w, true, m, 200); // the same line still being said: not a new order
    CHECK(m.orderUntil == until);
}

void GoTakesOnEnemiesFurtherAway()
{
    World w = Bar();
    AddEnemy(w, 20, 2600, 1000, OtherPartnerRecord); // 1400 from the partner, after someone else
    Memory m;
    CHECK(DecideOnce(w, true, m, 100).intent != Intent::Fight);
    w.characters[0].speech = static_cast<uint8_t>(Command::Go);
    Decision d = DecideOnce(w, true, m, 101);
    CHECK(d.intent == Intent::Fight);
    CHECK(d.target == 20);
}

void KeepsItsTargetUnlessAnotherIsMuchWorse()
{
    World w = Bar();
    AddEnemy(w, 10, 1500, 1000, PlayerRecord); // after the player, 500 off
    Memory m;
    CHECK(DecideOnce(w, true, m).target == 10);
    AddEnemy(w, 11, 1450, 1000, PlayerRecord); // a little closer to the player: not worth switching
    CHECK(DecideOnce(w, true, m).target == 10);
    AddEnemy(w, 12, 1100, 1000, PlayerRecord); // on the player: switch
    CHECK(DecideOnce(w, true, m).target == 12);
}

void TakesOutACarriedGunWhenEnemiesAreAbout()
{
    World w = Bar();
    Character& me = w.characters[Partner];
    me.pack[4] = Item{.id = 82, .fires = true, .rounds = 15}; // Mark's handgun, carried
    Memory m;
    CHECK(Decide(w, Partner, AbilitiesOf(me), m, 100, Defaults).items == ItemCheck::None); // nothing about
    AddEnemy(w, 10, 3000, 1000, OtherPartnerRecord);
    CHECK(Decide(w, Partner, AbilitiesOf(me), m, 101, Defaults).items == ItemCheck::TakeOutGun);
    me.hand = me.pack[4];                                     // in hand: nothing to take out
    CHECK(Decide(w, Partner, AbilitiesOf(me), m, 102, Defaults).items != ItemCheck::TakeOutGun);
    me.hand.rounds = 0;
    me.pack[4].rounds = 0;                                    // empty: not worth it
    CHECK(Decide(w, Partner, AbilitiesOf(me), m, 103, Defaults).items != ItemCheck::TakeOutGun);
}

void LooksForARecoveryItemWhenHurt()
{
    World w = Bar();
    Character& me = w.characters[Partner];
    me.pack[0] = Item{.id = 85, .heals = true};
    Memory m;
    CHECK(Decide(w, Partner, AbilitiesOf(me), m, 1000, Defaults).items == ItemCheck::None); // well
    me.hp = 900;                                                                            // under half
    Decision d = Decide(w, Partner, AbilitiesOf(me), m, 1001, Defaults);
    CHECK(d.items == ItemCheck::Look);
    CHECK(d.urgent);
    CHECK(Decide(w, Partner, AbilitiesOf(me), m, 1002, Defaults).items == ItemCheck::None); // not again at once
    me.pack[0] = Item{};
    CHECK(Decide(w, Partner, AbilitiesOf(me), m, 2000, Defaults).items == ItemCheck::None); // nothing to use
}

void GuardingLooksAtItemsNowAndThen()
{
    World w = Bar();
    AddEnemy(w, 10, 3000, 1000, OtherPartnerRecord);
    Memory m;
    int looks = 0;
    for (uint64_t frame = 1000; frame < 1000 + 3 * Defaults.lookFrames; frame++)
    {
        Decision d = DecideOnce(w, true, m, frame);
        CHECK(d.intent == Intent::Guard);
        looks += d.items == ItemCheck::Look;
        CHECK(!d.urgent);
    }
    CHECK(looks == 3);
}

void StepsAwayFacingTheRightWay()
{
    const Vec me{0, 0, 0};
    const Vec north{0, 0, 100}; // toward +z: heading 0
    CHECK(Heading(me, north) == 0);
    CHECK(Heading(me, Vec{100, 0, 0}) == 0x4000);
    // the enemy to the north: facing it, step back; facing south (away), run; facing east, turn toward south
    CHECK(StepAway(me, 0x0000, north) == Pad::Back);
    CHECK(StepAway(me, 0x8000, north) == (Pad::Forward | Pad::Run));
    // away is 0x8000; from 0x4000 the facing must rise: Left (Right lowers it)
    CHECK(StepAway(me, 0x4000, north) == Pad::Left);
    CHECK(StepAway(me, 0xC000, north) == Pad::Right);
}

void BacksAwayWithoutAGun()
{
    World w = Bar();
    AddEnemy(w, 10, 1200, 1150, PartnerRecord); // 150 from the partner, 250 from the player
    Memory m;
    Decision bare = DecideOnce(w, false, m);
    CHECK(bare.pad != 0);
    Memory armed;
    CHECK(DecideOnce(w, true, armed).pad == 0);  // a loaded gun: it fights it
    w.characters[Partner].hp = 500;              // badly hurt: backs off even with a gun
    Memory hurt;
    CHECK(DecideOnce(w, true, hurt).pad != 0);
    w.enemies[10].position = {1050, 0, 1000};    // on the player: not backed away from
    Memory rescue;
    CHECK(DecideOnce(w, false, rescue).pad == 0);
}

void ADownedPartnerDecidesNothing()
{
    World w = Bar();
    AddEnemy(w, 46, 1080, 1000, PlayerRecord);
    w.characters[Partner].hp = 0;
    Memory m;
    CHECK(DecideOnce(w, true, m).intent == Intent::Free);
}
}

int main()
{
    FightsWhatIsOnThePlayer();
    PrefersThePlayersEnemyToItsOwn();
    UnarmedKeepsOutOfFightsNotOnThePlayer();
    ShootsWhatComesNearWithAGun();
    IgnoresSetAsideDeadAndOtherRooms();
    FollowsBackWithSlack();
    ThisWayBringsPartnersClose();
    CallingByNameOrdersOnlyThatPartner();
    ACommandCountsOnceWhileItsLineIsSaid();
    GoTakesOnEnemiesFurtherAway();
    KeepsItsTargetUnlessAnotherIsMuchWorse();
    TakesOutACarriedGunWhenEnemiesAreAbout();
    LooksForARecoveryItemWhenHurt();
    GuardingLooksAtItemsNowAndThen();
    StepsAwayFacingTheRightWay();
    BacksAwayWithoutAGun();
    ADownedPartnerDecidesNothing();
    if (g_failures != 0)
    {
        std::printf("%d checks failed\n", g_failures);
        return 1;
    }
    std::printf("partner AI brain: all checks passed\n");
    return 0;
}
