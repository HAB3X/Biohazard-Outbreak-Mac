#include "partner_ai_telemetry.h"

#include "partner_ai_pad.h"

#include <cstdio>
#include <cstdlib>

namespace PartnerAi
{
namespace
{
constexpr uint64_t SampleFrames = 15; // a state line each half second (the game runs at 30 frames a second)

struct Remembered
{
    bool known = false;
    int hp = 0;
};

FILE* g_log = nullptr;
bool g_opened = false;
uint64_t g_lastEnemyFrame = ~0ull;
std::array<Remembered, CharacterCount> g_characters;
std::array<Remembered, EnemyCount> g_enemies;
uint8_t g_lastSpeech = 0;

FILE* Log()
{
    if (!g_opened)
    {
        g_opened = true;
        if (const char* path = std::getenv("BH_AI_LOG"))
        {
            g_log = std::fopen(path, "w");
            if (g_log == nullptr)
            {
                std::fprintf(stderr, "partner AI: BH_AI_LOG %s can't be written\n", path);
            }
            else
            {
                std::setvbuf(g_log, nullptr, _IOLBF, 0);
            }
        }
    }
    return g_log;
}

double Seconds(uint64_t frame) { return static_cast<double>(frame) / 30.0; }

// The enemies the player's room holds, alive: their count, hit points and the nearest's distance to `from`
struct RoomEnemies
{
    int count = 0, hp = 0;
    float nearest = -1;
};

RoomEnemies EnemiesIn(const World& world, int16_t room, const Vec& from)
{
    RoomEnemies r;
    for (const Enemy& e : world.enemies)
    {
        if (!e.alive() || e.room != room)
        {
            continue;
        }
        r.count++;
        r.hp += e.hp;
        float d = Distance(e.position, from);
        if (r.nearest < 0 || d < r.nearest)
        {
            r.nearest = d;
        }
    }
    return r;
}

// Once a frame: damage to the enemies, the player's heals and commands
void FrameEvents(FILE* log, const World& world, uint64_t frame)
{
    double t = Seconds(frame);
    for (int n = 0; n < EnemyCount; n++)
    {
        const Enemy& e = world.enemies[n];
        Remembered& r = g_enemies[n];
        if (r.known && e.active && e.hp < r.hp)
        {
            std::fprintf(log, "E t=%.2f enemy=%d room=%d damage=%d hp=%d/%d\n", t, n, e.room, r.hp - e.hp, e.hp, e.hpMax);
        }
        // An enemy being set up has its hit points above its most for a frame (File 1's bar: 800 then 750): not damage
        r.known = e.active && e.hp <= e.hpMax;
        r.hp = e.hp;
    }
    const Character& player = world.player();
    if (player.speech != g_lastSpeech && player.speech != 0)
    {
        std::fprintf(log, "E t=%.2f command=%d\n", t, player.speech);
    }
    g_lastSpeech = player.speech;
}

void CharacterEvents(FILE* log, const Character& c, int index, uint64_t frame)
{
    Remembered& r = g_characters[index];
    if (r.known && c.hp > r.hp)
    {
        std::fprintf(log, "E t=%.2f c=%d heal from=%d to=%d max=%d\n", Seconds(frame), index, r.hp, c.hp, c.hpMax);
    }
    if (r.known && c.hp < r.hp)
    {
        std::fprintf(log, "E t=%.2f c=%d hurt from=%d to=%d max=%d\n", Seconds(frame), index, r.hp, c.hp, c.hpMax);
    }
    r.known = c.active;
    r.hp = c.hp;
}
}

bool Measuring() { return Log() != nullptr; }

void Measure(const World& world, int partner, uint64_t frame)
{
    FILE* log = Log();
    if (frame != g_lastEnemyFrame)
    {
        g_lastEnemyFrame = frame;
        FrameEvents(log, world, frame);
        CharacterEvents(log, world.player(), 0, frame);
    }
    const Character& p = world.characters[partner];
    CharacterEvents(log, p, partner, frame);
    if ((p.padPressed & Pad::Attack) != 0 && (p.padHeld & Pad::Ready) != 0)
    {
        std::fprintf(log, "E t=%.2f c=%d attack target=%d\n", Seconds(frame), partner, p.target);
    }
    if (frame % SampleFrames != 0)
    {
        return;
    }
    const Character& player = world.player();
    RoomEnemies near = EnemiesIn(world, player.room, player.position);
    RoomEnemies mine = EnemiesIn(world, p.room, p.position);
    std::fprintf(log,
                 "S t=%.2f c=%d room=%d proom=%d d=%.0f hp=%d/%d m=%d plan=%d move=%d act=%d tgt=%d pad=%x x=%.0f z=%.0f "
                 "php=%d/%d pm=%d en=%d ehp=%d pnear=%.0f mynear=%.0f\n",
                 Seconds(frame), partner, p.room, player.room, Distance(p.position, player.position), p.hp, p.hpMax, p.motion,
                 p.plan, p.move, p.action, p.target, p.padHeld, p.position.x, p.position.z, player.hp, player.hpMax,
                 player.motion, near.count, near.hp, near.nearest, mine.nearest);
}
}
