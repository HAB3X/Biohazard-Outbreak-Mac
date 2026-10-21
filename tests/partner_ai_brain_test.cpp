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
    // bare hands too: shoving it off the player is worth it (with nothing else at the partner's elbow)
    w.enemies[47].position = {1600, 0, 1300};
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
    // the enemy to the north: facing it, the quick turn; facing south (away), run; facing east, turn toward south
    CHECK(StepAway(me, 0x0000, north).held == (Pad::Back | Pad::Run)); // the quick turn
    CHECK(StepAway(me, 0x0000, north).pressed == Pad::Run);
    CHECK(StepAway(me, 0x8000, north).held == (Pad::Forward | Pad::Run));
    // away is 0x8000; from 0x4000 the facing must rise: Left (Right lowers it)
    CHECK(StepAway(me, 0x4000, north).held == Pad::Left);
    CHECK(StepAway(me, 0xC000, north).held == Pad::Right);
}

void BacksAwayWithoutAGun()
{
    World w = Bar();
    AddEnemy(w, 10, 1200, 1150, PartnerRecord); // 150 from the partner, 250 from the player
    Memory m;
    Decision bare = DecideOnce(w, false, m);
    CHECK(bare.pad != 0);
    Memory armed;
    CHECK(DecideOnce(w, true, armed).pad == Pad::Ready); // a loaded gun: it fights it (readied, aiming), no backing off
    w.characters[Partner].hp = 500;              // badly hurt: backs off even with a gun
    Memory hurt;
    CHECK(DecideOnce(w, true, hurt).pad != 0);
    w.enemies[10].position = {1050, 0, 1000};    // on the player: not backed away from
    Memory rescue;
    CHECK(DecideOnce(w, false, rescue).pad == 0);
}

void KeepsBackingAwayUntilClear()
{
    World w = Bar();
    AddEnemy(w, 10, 1200, 1150, PartnerRecord); // 150 from the partner
    Memory m;
    CHECK(DecideOnce(w, false, m, 100).pad != 0);
    w.enemies[10].position = {1200, 0, 1400};    // 400: past the space, within the slack: still going
    CHECK(DecideOnce(w, false, m, 101).pad != 0);
    w.enemies[10].position = {1200, 0, 1500};    // 500: clear
    CHECK(DecideOnce(w, false, m, 102).pad == 0);
}

void GivesUpBackingIntoAWall()
{
    World w = Bar();
    AddEnemy(w, 10, 1200, 1150, PartnerRecord); // 150 from the partner, which can't move (a wall behind it)
    Memory m;
    int evading = 0;
    for (uint64_t frame = 100; frame < 100 + 60; frame++)
    {
        evading += DecideOnce(w, false, m, frame).pad != 0;
    }
    CHECK(evading == static_cast<int>(Defaults.evadeCheckFrames)); // then it stops for a while
    CHECK(DecideOnce(w, false, m, 100 + 30 + Defaults.evadeBlockedFrames).pad != 0); // and tries again after
}

void BareHandsOnlyWhenNothingElseIsClose()
{
    World w = Bar();
    AddEnemy(w, 46, 1080, 1000, PlayerRecord);   // on the player
    AddEnemy(w, 47, 1250, 1150, PartnerRecord);  // and another at the partner's elbow
    Memory m;
    Decision d = DecideOnce(w, false, m);
    CHECK(d.intent != Intent::Fight);
    CHECK(d.pad != 0);
    w.enemies[47].position = {1200, 0, 1320};    // 320 off: not at its elbow, the rescue goes on (it stood idle before)
    Memory rescue;
    Decision r = DecideOnce(w, false, rescue);
    CHECK(r.intent == Intent::Fight);
    CHECK(r.target == 46);
    w.characters[Partner].hp = 900;              // under half: it doesn't wrestle zombies bare-handed
    Memory hurt;
    CHECK(DecideOnce(w, false, hurt).intent != Intent::Fight);
}

void AimsThenFires()
{
    Character me{};
    me.position = {0, 0, 0};
    me.facing = 0x4000;                       // facing +x
    const Vec north{0, 0, 500};               // the enemy toward +z: heading 0, the facing must fall: Right
    Buttons b = Aim(me, north, 10, Defaults);
    CHECK(b.held == Pad::Right);              // turning with the weapon down
    CHECK(b.pressed == 0);
    me.facing = 0x0100;                       // on target, not yet in the aiming stance
    b = Aim(me, north, 10, Defaults);
    CHECK(b.held == Pad::Ready);
    me.motion = 0x80;                         // aiming: fires on the cadence's frames only
    CHECK(Aim(me, north, 10, Defaults).pressed == Pad::Attack);
    CHECK(Aim(me, north, 11, Defaults).pressed == 0);
}

void ShootsItsTargetItself()
{
    World w = Bar();
    AddEnemy(w, 10, 1400, 1000, PlayerRecord); // 400 from the player: urgent
    Memory m;
    Decision d = DecideOnce(w, true, m);
    CHECK(d.intent == Intent::Fight);
    CHECK(d.pad == Pad::Left);                // facing +z, the enemy toward +x: turning to it, the weapon down
    w.enemies[10].position = {1700, 0, 1000};  // 700 from the player: the game's own attack routine
    Memory notUrgent;
    Decision later = DecideOnce(w, true, notUrgent);
    CHECK(later.intent == Intent::Fight);
    CHECK(later.pad == 0);
    w.enemies[10].position = {3000, 0, 1000}; // out of range: the game's own attack routine
    AddEnemy(w, 11, 1800, 1000, OtherPartnerRecord);
    Memory far;
    w.enemies[11].position = {2700, 0, 1000};
    Decision d2 = Decide(w, Partner, Abilities{.gun = true}, far, 100, Defaults);
    CHECK(d2.pad == 0);
}

void StopsShootingWhatItCantHit()
{
    World w = Bar();
    Character& me = w.characters[Partner];
    AddEnemy(w, 10, 1200, 1350, PlayerRecord); // straight ahead (+z) of the partner, 400 off the player
    me.motion = 0x80;
    Memory m;
    int presses = 0;
    for (uint64_t frame = 0; frame < 200; frame++)
    {
        Decision d = Decide(w, Partner, Abilities{.gun = true}, m, frame, Defaults);
        presses += d.pressed != 0;
    }
    CHECK(presses == static_cast<int>(Defaults.missedShots) + 1); // then it holds fire
    // hits keep it going
    Memory hitting;
    presses = 0;
    for (uint64_t frame = 0; frame < 200; frame++)
    {
        Decision d = Decide(w, Partner, Abilities{.gun = true}, hitting, frame, Defaults);
        if (d.pressed != 0)
        {
            presses++;
            w.enemies[10].hp -= 10;
        }
    }
    CHECK(presses == 20);
}

// The owner's rule: the partners leave the scenarios' puzzles and progress to the player. What a partner may pick up for
// itself, by the item classes of File 1's bar (the class table dumped: each class's flags, its item number)
void TakesOnlyConsumables()
{
    CHECK(Consumable(0x102041));  // Mark's handgun (0x191)
    CHECK(Consumable(0x2481001)); // a pipe
    CHECK(Consumable(0x100002));  // rounds (0xFA-0x106)
    CHECK(Consumable(0x100400));  // a box of handgun rounds (0xC8, 0xD2)
    CHECK(Consumable(0x40004));   // a green herb (0x12C)
    CHECK(!Consumable(0x40));     // the characters' own items, files (0x192-0x1A1)
    CHECK(!Consumable(0x8));      // event items (0x1C3)
    CHECK(!Consumable(0x8000008));
    CHECK(!Consumable(0x8040200)); // key items (0x2774-0x2776)
    CHECK(!Consumable(0x10));     // 0x27A6
    CHECK(!Consumable(0));        // no class
}

// Nothing the director asks of the game is a puzzle or event action: its intents are following, guarding and fighting, and
// its item checks are the gun carried and the item check's look (whose pick-ups are filtered, TakesOnlyConsumables)
void DecidesNoPuzzleActions()
{
    World w = Bar();
    Memory m;
    for (uint64_t frame = 0; frame < 2000; frame++)
    {
        if (frame == 500)
        {
            AddEnemy(w, 10, 1500, 1000, PlayerRecord);
        }
        if (frame == 1500)
        {
            w.characters[0].speech = static_cast<uint8_t>(Command::HelpMe);
        }
        const Decision d = DecideOnce(w, true, m, frame);
        const bool known = d.intent == Intent::Free || d.intent == Intent::Follow || d.intent == Intent::Guard ||
                           d.intent == Intent::Fight;
        CHECK(known);
        CHECK(d.items == ItemCheck::None || d.items == ItemCheck::TakeOutGun || d.items == ItemCheck::Look);
        CHECK((d.pressed & ~Pad::Attack) == 0);                 // the only press is the attack
        CHECK(d.pressed == 0 || (d.pad & Pad::Ready) != 0);      // and only with the weapon readied: never the action
    }
}

void RetreatsToTheSafestNodeNearThePlayer()
{
    World w = Bar();
    // the room's route nodes: by the player, by the enemy, a far corner off the leash, and one clear of the enemy
    const Vec nodes[] = {{1000, 0, 1100}, {1300, 0, 1300}, {3000, 0, 3000}, {500, 0, 600}};
    for (const Vec& n : nodes)
    {
        w.nodes[static_cast<size_t>(w.nodeCount++)] = n;
    }
    AddEnemy(w, 10, 1300, 1250, PartnerRecord); // 270 from the partner (at 1300, 1000), not on the player
    w.characters[Partner].position = {1300, 0, 1000};
    Memory m;
    Decision d = DecideOnce(w, false, m, 100);
    CHECK(d.retreat);
    CHECK(d.newRoute);
    CHECK(d.pad == 0);                             // the game's route moves it, not the pad
    CHECK(d.retreatTo.x == 500 && d.retreatTo.z == 600);
    Decision again = DecideOnce(w, false, m, 101);
    CHECK(again.retreat && !again.newRoute);       // the same choice, no new route
    Memory armed;
    CHECK(!DecideOnce(w, true, armed, 100).retreat); // a loaded gun: it fights
    w.enemies[10].position = {1300, 0, 1600};      // 600 off: no need
    Memory calm;
    CHECK(!DecideOnce(w, false, calm, 100).retreat);
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
    KeepsBackingAwayUntilClear();
    GivesUpBackingIntoAWall();
    BareHandsOnlyWhenNothingElseIsClose();
    AimsThenFires();
    ShootsItsTargetItself();
    StopsShootingWhatItCantHit();
    TakesOnlyConsumables();
    RetreatsToTheSafestNodeNearThePlayer();
    DecidesNoPuzzleActions();
    ADownedPartnerDecidesNothing();
    if (g_failures != 0)
    {
        std::printf("%d checks failed\n", g_failures);
        return 1;
    }
    std::printf("partner AI brain: all checks passed\n");
    return 0;
}
