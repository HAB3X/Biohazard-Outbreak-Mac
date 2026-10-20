#include "partner_ai_brain.h"

#include "partner_ai_pad.h"

#include <algorithm>
#include <limits>

namespace PartnerAi
{
namespace
{
constexpr float Far = std::numeric_limits<float>::max();

// A new line from the player (the speech code changing to a command) gives this partner an order for a while
void TakeCommand(const World& world, int partner, Memory& memory, uint64_t frame, const Tunables& tunables)
{
    const uint8_t speech = world.player().speech;
    if (speech != memory.lastSpeech && speech != 0)
    {
        const uint8_t id = world.characters[partner].id;
        Order order = Order::None;
        switch (static_cast<Command>(speech))
        {
        case Command::Go:
            order = Order::Go;
            break;
        case Command::ThisWay:
            order = Order::Come;
            break;
        case Command::HelpMe:
            order = Order::Help;
            break;
        case Command::CallFirst:
            order = id == 1 ? Order::Come : Order::None;
            break;
        case Command::CallSecond:
            order = id == 2 ? Order::Come : Order::None;
            break;
        case Command::Thanks:
            break;
        }
        if (order != Order::None)
        {
            memory.order = order;
            memory.orderUntil = frame + tunables.orderFrames;
        }
    }
    memory.lastSpeech = speech;
    if (memory.order != Order::None && frame >= memory.orderUntil)
    {
        memory.order = Order::None;
    }
}

bool EnemiesAbout(const World& world, const Character& me)
{
    for (const Enemy& enemy : world.enemies)
    {
        if (enemy.alive() && enemy.room == me.room)
        {
            return true;
        }
    }
    return false;
}

struct Threat
{
    int index = -1;
    float score = 0;
    bool onPlayer = false;
};

// How much an enemy is worth fighting for this partner now (0: not at all). On the player first, then after the player,
// then after the partner; told "Go!", anything near
float ThreatScore(const Enemy& enemy, const Character& me, const Character& player, Order order, const Tunables& t, bool& onPlayer)
{
    const float toPlayer = player.room == me.room ? Distance(enemy.position, player.position) : Far;
    const float toMe = Distance(enemy.position, me.position);
    const float reach = order == Order::Help ? t.threatToPlayer * 1.5f : t.threatToPlayer;
    onPlayer = toPlayer < t.closeQuarters;
    if (onPlayer)
    {
        return 4000 - toPlayer;
    }
    if (enemy.target == player.address && toPlayer < reach)
    {
        return 3000 - toPlayer;
    }
    if (enemy.target == me.address && toMe < t.threatToPartner)
    {
        return 2000 - toMe;
    }
    if (order == Order::Come)
    {
        return 0; // coming to the player: only what's on the player or on the partner
    }
    if (toPlayer < reach * 0.5f)
    {
        return 1000 - toPlayer;
    }
    if (toPlayer < t.watchToPlayer || toMe < t.watchToPartner)
    {
        return 600 - std::min(toPlayer, toMe) / 2;
    }
    if (order == Order::Go && toMe < t.goThreat)
    {
        return 500 - toMe / 8;
    }
    return 0;
}

// The enemy to fight: the worst threat, or the one being fought while it's nearly as bad (switching restarts the attack)
Threat PickThreat(const World& world, int partner, const Memory& memory, const Tunables& tunables)
{
    const Character& me = world.characters[partner];
    const Character& player = world.player();
    Threat worst, current;
    for (int n = 0; n < EnemyCount; n++)
    {
        const Enemy& enemy = world.enemies[n];
        if (!enemy.alive() || enemy.room != me.room)
        {
            continue;
        }
        bool onPlayer = false;
        const float score = ThreatScore(enemy, me, player, memory.order, tunables, onPlayer);
        if (score > worst.score)
        {
            worst = {n, score, onPlayer};
        }
        if (n == memory.target && score > 0)
        {
            current = {n, score, onPlayer};
        }
    }
    const bool keep = current.index >= 0 && current.score >= worst.score * tunables.keepTarget &&
                      (current.onPlayer || !worst.onPlayer);
    return keep ? current : worst;
}

// The nearest enemy within the partner's personal space that isn't on the player (one on the player is the partner's to
// shove off, not to back away from)
const Enemy* TooClose(const World& world, const Character& me, float space, const Tunables& tunables)
{
    const Character& player = world.player();
    const Enemy* nearest = nullptr;
    float best = space;
    for (const Enemy& enemy : world.enemies)
    {
        if (!enemy.alive() || enemy.room != me.room)
        {
            continue;
        }
        const float d = Distance(enemy.position, me.position);
        const bool onPlayer = player.room == me.room && Distance(enemy.position, player.position) < tunables.closeQuarters;
        if (d < best && !onPlayer)
        {
            best = d;
            nearest = &enemy;
        }
    }
    return nearest;
}

// What the partner should be doing: fight (a gun, or bare hands against what's on the player), come back to the player,
// guard them while enemies are about, or leave it to the game
Decision Intend(const World& world, int partner, const Abilities& abilities, Memory& memory, const Tunables& tunables)
{
    const Character& me = world.characters[partner];
    const Character& player = world.player();
    const Threat threat = PickThreat(world, partner, memory, tunables);
    // bare hands only against what's on the player, and only with no other enemy at the partner's elbow
    const bool bareHanded = threat.onPlayer && TooClose(world, me, tunables.personalSpace * tunables.evadeSlack, tunables) == nullptr;
    if (threat.index >= 0 && (abilities.gun || bareHanded))
    {
        memory.target = threat.index;
        return {Intent::Fight, threat.index, threat.onPlayer ? "on the player" : "threat"};
    }
    memory.target = -1;
    const bool come = memory.order == Order::Come || memory.order == Order::Help;
    const float far = come ? tunables.comeFar : tunables.followFar;
    const float near = come ? tunables.comeNear : tunables.followNear;
    const float distance = Distance(me.position, player.position);
    if (me.room != player.room || distance > far)
    {
        memory.following = true;
    }
    else if (distance < near)
    {
        memory.following = false;
    }
    if (memory.following)
    {
        return {Intent::Follow, -1, me.room != player.room ? "other room" : "far"};
    }
    if (EnemiesAbout(world, me))
    {
        return {Intent::Guard, -1, "enemies about"};
    }
    return {Intent::Free, -1, "near"};
}

// The item check worth asking for now: the gun carried when enemies are about and none is in hand; a recovery item when hurt
// (urgent: a fight may be cut short for it); while guarding, a look now and then (the game looks on arriving somewhere,
// which a guarding partner doesn't)
void AskForItems(Decision& d, const Character& me, const Abilities& abilities, bool enemiesAbout, Memory& memory, uint64_t frame,
                 const Tunables& tunables)
{
    const bool due = frame >= memory.lastLook + tunables.lookFrames;
    if (enemiesAbout && !abilities.gun && abilities.spareGun && frame >= memory.lastLook + tunables.retryFrames)
    {
        d.items = ItemCheck::TakeOutGun;
    }
    else if (abilities.recovery && me.hp < me.hpMax * tunables.hurt && due)
    {
        d.items = ItemCheck::Look;
        d.urgent = true;
    }
    else if (d.intent == Intent::Guard && due)
    {
        d.items = ItemCheck::Look;
    }
    if (d.items != ItemCheck::None)
    {
        memory.lastLook = frame;
    }
}
}

// The motion the game's fire routine waits for before it fires (File 1's 0x830D30: the motion byte at 0x80)
constexpr uint8_t MotionAiming = 0x80;

Buttons Aim(const Character& me, const Vec& target, uint64_t frame, const Tunables& tunables)
{
    // Turning is done with the weapon down: with it readied, left and right move the game's own aim from enemy to enemy
    // instead of turning (measured: the facing jumped between two headings and never reached the one wanted)
    Buttons b;
    const uint16_t wanted = Heading(me.position, target);
    const int16_t off = static_cast<int16_t>(static_cast<uint16_t>(wanted - me.facing));
    const int16_t tolerance = static_cast<int16_t>(tunables.aimTolerance);
    if (off > tolerance || off < -tolerance)
    {
        b.held = static_cast<uint16_t>(me.facing - wanted) <= 0x8000 ? Pad::Right : Pad::Left;
        return b;
    }
    b.held = Pad::Ready;
    if (me.motion == MotionAiming && frame % tunables.fireFrames == 0)
    {
        b.held |= Pad::Attack;
        b.pressed |= Pad::Attack;
    }
    return b;
}

uint32_t StepAway(const Vec& position, uint16_t facing, const Vec& from)
{
    const uint16_t away = Heading(from, position);
    const int16_t off = static_cast<int16_t>(static_cast<uint16_t>(away - facing));
    constexpr int16_t Ahead = 0x2000;  // within 45 degrees of away: run
    constexpr int16_t Behind = 0x6000; // within 45 degrees of toward it: step back, still facing it
    if (off >= -Ahead && off <= Ahead)
    {
        return Pad::Forward | Pad::Run;
    }
    if (off >= Behind || off <= -Behind)
    {
        return Pad::Back;
    }
    return static_cast<uint16_t>(facing - away) <= 0x8000 ? Pad::Right : Pad::Left;
}

namespace
{
// The partner's own shots at its target, kept honest: shots that don't hurt it (another enemy in the line, a wall, the
// game's aim gone elsewhere) are counted, and after missFrames' worth the shooting is left to the game's own attack routine
// for a while rather than the clip emptied into nothing
void Shoot(Decision& d, const Character& me, const Enemy& target, Memory& memory, uint64_t frame, const Tunables& tunables)
{
    if (d.target != memory.shotTarget || target.hp < memory.shotTargetHp)
    {
        memory.shotTarget = d.target;
        memory.shotsMissed = 0;
    }
    memory.shotTargetHp = target.hp;
    const Buttons aim = Aim(me, target.position, frame, tunables);
    d.pad = aim.held;
    d.pressed = aim.pressed;
    if (aim.pressed != 0 && ++memory.shotsMissed > tunables.missedShots)
    {
        memory.holdFireUntil = frame + tunables.holdFireFrames;
        memory.shotsMissed = 0;
    }
}
}

Abilities AbilitiesOf(const Character& character)
{
    Abilities a;
    a.gun = character.hand.loadedGun();
    a.melee = character.hand.weapon && !character.hand.fires;
    for (const Item& item : character.pack)
    {
        a.spareGun = a.spareGun || (item.loadedGun() && item.id != character.hand.id);
        a.recovery = a.recovery || item.heals;
    }
    return a;
}

Decision Decide(const World& world, int partner, const Abilities& abilities, Memory& memory, uint64_t frame,
                const Tunables& tunables)
{
    TakeCommand(world, partner, memory, frame, tunables);
    const Character& me = world.characters[partner];
    if (!me.active || me.hp <= 0 || !world.player().active)
    {
        return {Intent::Free, -1, "down"};
    }
    Decision d = Intend(world, partner, abilities, memory, tunables);
    AskForItems(d, me, abilities, EnemiesAbout(world, me), memory, frame, tunables);
    const bool canFight = abilities.gun && me.hp >= me.hpMax * tunables.badlyHurt;
    // stepping away goes on until the enemy is farther than the space with some slack, so it doesn't stop and start
    const float space = tunables.personalSpace * (memory.evading ? tunables.evadeSlack : 1.0f);
    const Character& player = world.player();
    const bool rescuing = d.intent == Intent::Fight &&
                          Distance(world.enemies[d.target].position, player.position) < tunables.closeQuarters;
    const Enemy* close = canFight || rescuing || frame < memory.evadeBlockedUntil ? nullptr : TooClose(world, me, space, tunables);
    if (close != nullptr && !memory.evading)
    {
        memory.evadeFrom = me.position;
        memory.evadeSince = frame;
    }
    memory.evading = close != nullptr;
    if (close != nullptr && frame >= memory.evadeSince + tunables.evadeCheckFrames)
    {
        // backing into a wall or a corner: stop trying for a while (the game's own moves get it out)
        if (Distance(me.position, memory.evadeFrom) < tunables.evadeMinProgress)
        {
            memory.evadeBlockedUntil = frame + tunables.evadeBlockedFrames;
            memory.evading = false;
            close = nullptr;
        }
        memory.evadeFrom = me.position;
        memory.evadeSince = frame;
    }
    if (close != nullptr)
    {
        d.pad = StepAway(me.position, me.facing, close->position);
        d.why = "too close";
    }
    else if (d.intent == Intent::Fight && abilities.gun && frame >= memory.holdFireUntil &&
             Distance(world.enemies[d.target].position, me.position) < tunables.shootRange)
    {
        Shoot(d, me, world.enemies[d.target], memory, frame, tunables);
    }
    return d;
}
}
