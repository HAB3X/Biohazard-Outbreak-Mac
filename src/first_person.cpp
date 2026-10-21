// The first-person camera (first_person.h).
//
// Adapted from REO-FirstPerson 2.6.8 by RaccoonRecomp (MIT): first_person.c's frame and pad-map steps with their constants and
// measures, on this disc's addresses (src/modern_camera_game.cpp); its host calls made the runtime's.
#include "first_person.h"

#include "ps2/recomp.h"
#include "ps2/runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace FirstPerson
{
namespace
{
using ModernCamera::Game;
// The game's record fields (set by each entry point)
const ModernCamera::Fields* F = nullptr;

struct V3
{
    float x = 0, y = 0, z = 0;
};
V3 v3(float x, float y, float z) { return {x, y, z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }
bool Sane(float v) { return std::isfinite(v) && v > -1e6f && v < 1e6f; }
bool Sane3(V3 a) { return Sane(a.x) && Sane(a.y) && Sane(a.z); }
V3 Unit(V3 a, V3 fallback)
{
    float n = std::sqrt(Dot(a, a));
    return n > 1e-5f && Sane(n) ? a * (1.0f / n) : fallback;
}
float Rdf(uint32_t a) { return std::bit_cast<float>(rd32(a)); }
void Wrf(uint32_t a, float v) { wr32(a, std::bit_cast<uint32_t>(v)); }
V3 Rdv(uint32_t a) { return v3(Rdf(a), Rdf(a + 4), Rdf(a + 8)); }
float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The skeleton (the same for all eight): a node starts with its world matrix (rows X, Y, Z at +0, +0x10, +0x20, the
// translation at +0x30, +0x3C = 1.0); +0xC8 its parent
constexpr uint32_t NodeHead = 21, NodeEye = 23, NodeWrist = 19, NodeHand = 28;

// Where the camera sits in each character's head (measured, head-bone space): the eyes' height above the head joint, 8 units
// in front of them, and the head bone's pitch at rest (the view is turned by it, so standing still looks level)
struct Eyes
{
    float up, forward, restCos, restSin;
};
constexpr Eyes EyeSpots[8] = {
    {7.5f, 16.0f, 0.98481f, -0.17365f}, // Kevin
    {7.5f, 16.0f, 0.99337f, -0.11494f}, // Mark
    {7.5f, 16.0f, 0.99488f, -0.10106f}, // Jim
    {7.5f, 16.0f, 0.97815f, -0.20791f}, // George
    {6.7f, 16.0f, 0.99604f, -0.08889f}, // David
    {7.0f, 16.0f, 0.97815f, -0.20791f}, // Alyssa
    {6.5f, 16.0f, 0.99317f, -0.11667f}, // Yoko
    {7.0f, 16.0f, 0.99098f, -0.13399f}, // Cindy
};

// The spot just behind each measured weapon's rear sight (the weapon node's axes): Kevin's 45 Auto and Mark's handgun, by the
// item record they are in the bar
struct Sight
{
    uint32_t item;
    float x, y, z;
};
constexpr Sight Sights[] = {{0x51, 0.0f, -1.7f, 9.0f}, {0x52, 2.0f, -1.7f, 9.0f}};

// Each view's offsets (game units, the head's levelled axes), look up or down (degrees) and field of view: RaccoonRecomp's
// numbers
struct Tune
{
    float fwd, up, right, look, fov;
};
constexpr Tune NormalView = {0.0f, 0.0f, -4.0f, 0.0f, 63.0f};
constexpr Tune AimView = {2.5f, 3.5f, -1.0f, 0.0f, 61.0f};

constexpr float LookDistance = 500.0f;
constexpr float SteadyCos = 0.9063078f;  // the barrel within 25 degrees of her aim: the sights are usable
constexpr float SettledCos = 0.9993908f; // turned less than 2 degrees since the last frame: held still
constexpr float BlendVblanks = 12.0f;    // normal <-> aiming view and her aim <-> the sight line take 0.2 s
constexpr float TakenDistance2 = 25.0f * 25.0f;
constexpr float PredictGrow = 6.0f, PredictGrowAxis = 0.1f, PredictJump = 40.0f, CarryMax = 11.0f;
constexpr float EyeEase = 0.5f, EyeEaseMin = 1.5f, EyeEaseMax = 6.0f, HeadClear = 14.0f, EaseReach = 20.0f;
constexpr int32_t GameTurn = 2731, TurnUnseen = 364, TurnStep = 5461, TurnAside = 1456;
constexpr float TurnSwing = 10.0f, YawPerRadian = 10430.378f;
constexpr uint8_t KindQuickTurn = 0x15;
constexpr float QuickBackCos = 0.9396926f;
constexpr float ResumeDistance2 = 30.0f * 30.0f;
constexpr int RoomLoading = 1, RoomPlaced = 2;
constexpr float StickTurn = 140.0f, StickTilt = 90.0f, StickUpMax = 30.0f, StickDownMax = 60.0f;
constexpr float VblankSeconds = 1.0f / 59.94f, YawUnits = 182.04445f;
constexpr int32_t AimCarry = 1092, AutoAimMax = 8192;
constexpr uint8_t ActionAim = 0x0B, ActionStand = 0x0B, KindBackFirst = 0x09, KindBackLast = 0x0B;
// A push of the left stick within this of the view's forward, or any with the dash, sets her off that way
constexpr int32_t PadFront = 0x4800;
constexpr uint32_t PadR3 = 0x2000;   // R3 in the system pad's new presses (an ad-lib line)
constexpr int32_t PadStickMove = 16;
constexpr uint32_t PadLeftStickMagnitude = 0x18, PadLeftStickDirection = 0x1A; // from the mapped record's held word
constexpr float GameYawStep = 16.0f, GamePi = 3.1415927f, GameUnits = 182.04445f;
constexpr uint32_t ActionDash = 0x10;

// A value carried on into the frame that'll be drawn: now plus its change of the last frame (a change that grew by more than
// `grow` only at the size of the one before; one longer than PredictJump not at all)
struct Track
{
    bool have = false;
    V3 last, delta;

    V3 Next(V3 now, float grow)
    {
        if (!have)
        {
            have = true;
            last = now;
            delta = {};
            return now;
        }
        V3 d = now - last;
        float n1 = std::sqrt(Dot(d, d)), n0 = std::sqrt(Dot(delta, delta));
        last = now;
        delta = d;
        if (n1 > PredictJump)
        {
            delta = {};
            return now;
        }
        if (n1 > n0 + grow)
        {
            d = d * (n0 / n1);
        }
        return now + d;
    }
};

struct State
{
    bool driving = false;
    bool yielded = false;
    V3 yieldPos;
    bool haveHide = false;
    float hideOriginal = 0;
    bool haveLast = false;
    V3 lastEye, lastAt;
    uint32_t taken = 0;
    bool haveVbl = false;
    uint32_t lastVbl = 0;
    uint32_t model = 0;
    uint32_t room = 0;
    int newRoom = 0;
    float aimBlend = 0, sightBlend = 0;
    bool haveSight = false;
    V3 lineLocal;
    bool holding = false;
    bool havePrev = false;
    V3 prevBarrel;
    uint32_t prevAim = 0;
    bool crossOn = false;
    Track trY, trZ, trT, trF;
    bool easing = false;
    V3 ease;
    bool quickHold = false;
    V3 quickSpot;
    bool haveOut = false;
    V3 lastOut, lastPos, lastR;
    uint32_t outH = 0;
    V3 feetStep;
    int32_t turnNext = 0;
    bool planWas = false;
    bool plan = false;
    uint32_t planH = 0;
    int32_t planMost = 0;
    int32_t padMag = 0;
    uint32_t padDir = 0;
    bool padDash = false;
    bool padCentred = false;
    bool haveView = false;
    float viewYaw = 0, viewPitch = 0;
    uint32_t lastH = 0;
    int32_t aimTurn = 0, aimApplied = 0;
    bool wasRaised = false;
    float aimYaw = 0, aimPitch = 0;
};
State s;

float YawWrap(float a)
{
    a = std::fmod(a, 65536.0f);
    return a < 0 ? a + 65536.0f : a;
}
int32_t YawDiff(uint32_t a, uint32_t b) { return static_cast<int16_t>(static_cast<uint16_t>((a - b) & 0xFFFF)); }
void YawSinCos(float yaw, float* sn, float* cs)
{
    double r = YawWrap(yaw) * (6.283185307179586 / 65536.0);
    *sn = static_cast<float>(std::sin(r));
    *cs = static_cast<float>(std::cos(r));
}
// Turned about the vertical by a yaw in the game's units (+ to her left, as her heading turns)
V3 TurnTo(V3 d, float yaw)
{
    float sn, cs;
    YawSinCos(yaw, &sn, &cs);
    return v3(d.x * cs + d.z * sn, d.y, d.z * cs - d.x * sn);
}
uint32_t TurnToward(uint32_t from, uint32_t to, int32_t step)
{
    int32_t d = std::clamp(YawDiff(to, from), -step, step);
    return static_cast<uint32_t>(static_cast<int32_t>(from) + d) & 0xFFFF;
}
// A direction tilted up by the angle whose cosine and sine are c and s
V3 Tilt(V3 d, float c, float s)
{
    float h = std::sqrt(d.x * d.x + d.z * d.z);
    if (!(h > 1e-4f))
    {
        return d;
    }
    float nc = std::max(0.05f, h * c - d.y * s), ns = d.y * c + h * s;
    return Unit(v3(d.x / h * nc, ns, d.z / h * nc), d);
}
V3 LookUp(V3 d, float degrees)
{
    float a = degrees * 0.017453293f;
    return degrees == 0 ? d : Tilt(d, std::cos(a), std::sin(a));
}
// The camera's yaw as the walking input takes it from the camera block's look-at minus its eye: steps of 16 (0..4095)
int32_t YawStep(float dx, float dz)
{
    if (dx == 0)
    {
        dx = 1e-4f; // (as the game does)
    }
    double deg = std::atan2(static_cast<double>(dx), static_cast<double>(dz)) * 180.0 / static_cast<double>(GamePi);
    if (deg < 0)
    {
        deg += 360.0;
    }
    return static_cast<int32_t>(deg * static_cast<double>(GameUnits) / GameYawStep) & 0xFFF;
}
V3 FromLocal(V3 origin, V3 fwd, V3 right, V3 l) { return origin + fwd * l.x + right * l.y + v3(0, l.z, 0); }
V3 ToLocal(V3 d, V3 fwd, V3 right) { return v3(Dot(d, fwd), Dot(d, right), d.y); }

bool NodeOk(uint32_t p) { return p >= 0x100000 && p < 0x2000000 && (p & 15) == 0 && rd32(p + 0x3C) == 0x3F800000; }
uint32_t NodeTable(uint32_t rec)
{
    uint32_t model = rd32(rec + F->Model);
    if (model < 0x100000 || model >= 0x2000000 || (model & 3) != 0)
    {
        return 0;
    }
    uint32_t table = rd32(model);
    return table >= 0x100000 && table < 0x2000000 && (table & 3) == 0 ? table : 0;
}
// The head bone, checked: valid matrices, her eye point a grandchild of it
uint32_t HeadNode(uint32_t table)
{
    uint32_t head = rd32(table + 4 * NodeHead), eye = rd32(table + 4 * NodeEye);
    if (!NodeOk(head) || !NodeOk(eye))
    {
        return 0;
    }
    uint32_t parent = rd32(eye + 0xC8);
    return NodeOk(parent) && rd32(parent + 0xC8) == head ? head : 0;
}
// The right hand's weapon node, checked: a child of the right wrist
uint32_t HandNode(uint32_t table)
{
    uint32_t hand = rd32(table + 4 * NodeHand), wrist = rd32(table + 4 * NodeWrist);
    return NodeOk(hand) && NodeOk(wrist) && rd32(hand + 0xC8) == wrist ? hand : 0;
}
const Sight* SightOf(uint32_t item)
{
    for (const Sight& sight : Sights)
    {
        if (sight.item == item)
        {
            return &sight;
        }
    }
    return nullptr;
}

// The walking input reads the stick by the camera (not as the d-pad's directions) in this action
bool StickByCamera(uint32_t rec)
{
    uint8_t act = rd8(rec + F->Action);
    return !(act == 0x1A || act == 0x27 || act == 0x31 || act == 0x80 || act == 0xB0 ||
             (act == 0x41 && rd8(rec + F->PadMode) == 1));
}

// She's free to turn and walk: standing, walking, running, turning or stepping back
bool FreeToTurn(uint32_t rec)
{
    uint8_t kind = rd8(rec + F->AimState), act = rd8(rec + F->ActionPhase), pad = rd8(rec + F->Action);
    return rd8(rec + F->Stance) == 1 && kind <= 0x0B && (act == 0x0B || act == 0x0A || act == 0x01 || act == 0x02) &&
           pad <= 3;
}

// Where the mapper sets her heading while she's free to turn: with the stick pushed in front of the view (or the dash), the
// way her walking input walks her (steer: the game walks her the way she faces); further back without the dash, the view's
// yaw while she stands or steps back; the stick at rest, the view's yaw while she stands. False: her heading is the game's
bool PadWant(uint32_t rec, int32_t mag, uint32_t dir, bool dash, uint32_t cam16, uint32_t* want, bool* steer)
{
    uint8_t kind = rd8(rec + F->AimState);
    bool stand = rd8(rec + F->ActionPhase) == ActionStand;
    int32_t rel = static_cast<int16_t>(dir);
    *want = static_cast<uint32_t>(static_cast<int32_t>(s.viewYaw + 0.5f)) & 0xFFFF;
    *steer = false;
    if (mag >= PadStickMove && StickByCamera(rec))
    {
        if ((rel <= PadFront && rel >= -PadFront) || dash)
        {
            *want = (cam16 + (dir & 0xFFF0)) & 0xFFFF;
            *steer = true;
            return true;
        }
        return stand || (kind >= KindBackFirst && kind <= KindBackLast);
    }
    return stand;
}

// The dash's button in the player's control type, as the mapped record's held word has it (square 0x100, cross 0x10,
// circle 0x20; 0 none)
uint32_t DashButton(const Game& game)
{
    static const uint32_t Buttons[3] = {0x100, 0x10, 0x20};
    uint8_t type = rd8(game.layout().controlType);
    if (type >= 8)
    {
        return 0;
    }
    uint32_t row = game.layout().buttonMap + 12u * type;
    for (int c = 0; c < 3; c++)
    {
        if (rd32(row + 4u * c) == ActionDash)
        {
            return Buttons[c];
        }
    }
    return 0;
}

void Crosshair(bool on)
{
    if (on == s.crossOn)
    {
        return;
    }
    s.crossOn = on;
    // A small white dot in the middle while she aims
    Ps2Crosshair c{};
    c.visible = on ? 1 : 0;
    c.style = 0;
    c.size = 2;
    c.rgb = 0xFFFFFF;
    c.opacity = on ? 1.0f : 0.0f;
    ps2_set_crosshair(&c);
}

void RestoreHide(const Game& game)
{
    if (s.haveHide)
    {
        Wrf(game.layout().nearHide + 4, s.hideOriginal);
        s.haveHide = false;
    }
}

void ResetView()
{
    s.trY = s.trZ = s.trT = s.trF = Track{};
    s.haveLast = false;
    s.taken = 0;
    s.aimBlend = s.sightBlend = 0;
    s.haveSight = s.holding = s.havePrev = s.haveOut = false;
    s.plan = s.planWas = false;
    s.padMag = 0;
    s.haveView = false;
    s.aimTurn = s.aimApplied = 0;
    s.wasRaised = false;
}
}

void HandBack(Game& game, bool restoreCamera)
{
    F = &game.layout().field;
    if (s.driving && restoreCamera)
    {
        game.RestoreCutCamera();
    }
    RestoreHide(game);
    game.RestoreArtwork();
    Crosshair(false);
    s.padCentred = false;
    s.driving = false;
    s.yielded = false;
    ResetView();
}

bool CentreRightStick() { return s.padCentred; }

bool Frame(Game& game, const Look& lookInput)
{
    const ModernCamera::Layout& l = game.layout();
    F = &l.field;
    const uint32_t rec = game.PlayerRecord();
    uint32_t vbl = rd32(l.frames);
    if (!s.haveVbl)
    {
        s.haveVbl = true;
        s.lastVbl = vbl;
    }
    float dt = Clamp(static_cast<float>(vbl - s.lastVbl), 1.0f, 8.0f);
    s.lastVbl = vbl;
    float step = dt / BlendVblanks;

    // A level: her record in use with a position, a skeleton with a head
    V3 pos = Rdv(rec + F->Position);
    uint32_t table = 0, head = 0;
    if (rd8(rec + F->Live) != 0 && Sane3(pos) && (pos.x != 0 || pos.z != 0))
    {
        table = NodeTable(rec);
        head = table != 0 ? HeadNode(table) : 0;
    }
    uint32_t id = rd32(rec + F->CharacterId) & 0xFF;
    if (head == 0 || id >= 8)
    {
        if (s.driving || s.yielded)
        {
            HandBack(game, false);
        }
        return false;
    }
    if (rd32(rec + F->Model) != s.model)
    {
        // Another character (a door keeps the model): from her eyes again
        s.model = rd32(rec + F->Model);
        ResetView();
        s.yielded = false;
    }
    uint8_t stance = rd8(rec + F->Stance);
    bool raised = stance == 2; // (a hit, stance 3, is no aim: the view stays)

    // The game took the camera for a scripted shot: it's the game's until she moves on her own or raises her weapon
    if (s.yielded)
    {
        V3 d = pos - s.yieldPos;
        if (d.x * d.x + d.z * d.z < ResumeDistance2 && stance != 2)
        {
            return false;
        }
        s.yielded = false;
    }
    if (s.driving && s.haveLast)
    {
        V3 e = Rdv(l.eye), a = Rdv(l.target);
        V3 de = e - s.lastEye, da = a - s.lastAt;
        s.taken = (Dot(de, de) > TakenDistance2 || Dot(da, da) > TakenDistance2) ? s.taken + 1 : 0;
        if (s.taken >= 2)
        {
            HandBack(game, false);
            s.yielded = true;
            s.yieldPos = pos;
            return false;
        }
    }
    if (!s.driving)
    {
        s.driving = true;
        ResetView();
    }
    s.padCentred = true;
    game.KeepArtwork();

    // Her frame: feet, facing (level), right
    float fx = Rdf(rec + F->ForwardX), fz = Rdf(rec + F->ForwardZ), n = std::sqrt(fx * fx + fz * fz);
    if (!(n > 1e-4f) || !Sane(n))
    {
        fx = 0;
        fz = 1;
    }
    else
    {
        fx /= n;
        fz /= n;
    }
    V3 fwdNow = v3(fx, 0, fz), rightNow = v3(-fz, 0, fx);

    // Her facing and head as they'll be drawn (the game draws the next frame after moving her once more)
    V3 hyNow = Rdv(head + 0x10), hzNow = Rdv(head + 0x20), htNow = Rdv(head + 0x30);
    bool jumped = false;
    if (s.trT.have)
    {
        V3 j = htNow - s.trT.last;
        if (Dot(j, j) > PredictJump * PredictJump)
        {
            jumped = true;
            s.trY = s.trZ = s.trT = s.trF = Track{};
        }
    }
    V3 fwd = s.trF.Next(fwdNow, PredictGrowAxis);
    fwd = Unit(v3(fwd.x, 0, fwd.z), fwdNow);
    V3 right = v3(-fwd.z, 0, fwd.x);
    V3 hy = s.trY.Next(hyNow, PredictGrowAxis), hz = s.trZ.Next(hzNow, PredictGrowAxis), ht = s.trT.Next(htNow, PredictGrow);

    // The eye spot: the head bone's matrix applied to her fixed point; the view along the head, levelled at rest
    const Eyes& eyes = EyeSpots[id];
    float rc = eyes.restCos, rs = eyes.restSin;
    V3 eyeSpot = ht + hy * eyes.up + hz * eyes.forward;
    V3 shift = eyeSpot - (htNow + hyNow * eyes.up + hzNow * eyes.forward);
    V3 headDir = Tilt(Unit(hz, fwd), rc, -rs);
    V3 hyU = Unit(hy, v3(0, 1, 0)), hzU = Unit(hz, fwd);
    V3 fH = Unit(hzU * rc - hyU * rs, headDir), uH = Unit(hyU * rc + hzU * rs, v3(0, 1, 0)), rH = Unit(Cross(fH, uH), right);
    const Tune& tn = NormalView;
    const Tune& ta = AimView;
    V3 aimPos = eyeSpot + fH * ta.fwd + uH * ta.up + rH * ta.right;

    // Where you look: from her heading when the camera comes into her eyes and where a door places her; then only the look
    // (the right stick, the mouse) turns it; while she aims the view is her aim's
    float sec = dt * VblankSeconds;
    uint32_t h16 = rd16(rec + F->Heading);
    float turn = -lookInput.stickX * StickTurn * sec * YawUnits - lookInput.mouseYawDegrees * YawUnits;
    bool viewWas = s.haveView;
    int32_t gameTurn = YawDiff(h16, s.lastH) - s.aimApplied;
    s.aimApplied = 0;
    uint32_t room = rd32(l.roomNow);
    if (room != s.room)
    {
        s.room = room;
        s.newRoom = RoomLoading;
    }
    if (s.newRoom != 0 && (jumped || (s.newRoom == RoomLoading && (raised || FreeToTurn(rec)))))
    {
        s.haveView = false;
        s.newRoom = RoomPlaced;
    }
    if (s.newRoom == RoomPlaced && (raised || FreeToTurn(rec)))
    {
        s.newRoom = 0;
    }
    if (!s.haveView)
    {
        s.haveView = true;
        s.viewYaw = static_cast<float>(h16);
        s.viewPitch = 0;
    }
    else if (raised)
    {
        if (s.wasRaised)
        {
            s.viewYaw = static_cast<float>(h16);
        }
    }
    else if (s.wasRaised)
    {
        s.viewYaw = s.aimYaw;
        s.viewPitch = Clamp(s.aimPitch - tn.look, -StickDownMax, StickUpMax);
    }
    s.viewYaw = YawWrap(s.viewYaw + turn);
    if (raised)
    {
        s.aimTurn = YawDiff(static_cast<uint32_t>(static_cast<int32_t>(s.viewYaw + 0.5f)), h16);
    }
    else
    {
        s.aimTurn = 0;
        s.viewPitch = Clamp(s.viewPitch + lookInput.stickY * StickTilt * sec + lookInput.mousePitchDegrees, -StickDownMax,
                            StickUpMax);
    }
    s.lastH = h16;
    s.wasRaised = raised;

    // Her aim: her facing as it'll be drawn, tilted with the gun's elevation
    bool aimReady = stance == 2 && rd8(rec + F->AimState) == 1 && rd8(rec + F->ActionPhase) == ActionAim;
    float elev = Clamp(static_cast<float>(static_cast<int16_t>(rd16(rec + F->GunElevation))) * (6.2831853f / 65536.0f),
                       -1.2f, 1.2f);
    int32_t aimCarry = 0;
    if (stance == 2 && rd8(rec + F->AimState) == 0 && rd8(rec + F->ActionPhase) == ActionAim &&
        rd16(rec + F->AutoAimLeft) != 0)
    {
        aimCarry = static_cast<int16_t>(rd16(rec + F->AutoAimStep));
        aimCarry = aimCarry > AutoAimMax || aimCarry < -AutoAimMax ? 0 : aimCarry;
    }
    else if (viewWas && gameTurn <= AimCarry && gameTurn >= -AimCarry)
    {
        aimCarry = gameTurn;
    }
    float aimYaw = raised ? static_cast<float>(h16) + static_cast<float>(s.aimTurn) + static_cast<float>(aimCarry) : s.viewYaw;
    float asn, acs;
    YawSinCos(aimYaw, &asn, &acs);
    V3 aimFwd = v3(asn, 0, acs), aimRight = v3(-acs, 0, asn);
    V3 aimDir = aimFwd * std::cos(elev) + v3(0, std::sin(elev), 0);
    V3 aimDirNow = fwdNow * std::cos(elev) + v3(0, std::sin(elev), 0);
    s.aimBlend = Clamp(s.aimBlend + (raised ? step : -step), 0, 1);

    // The sight line: through the weapon's rear-sight spot along her aim; the aiming view stepped square onto it
    uint32_t item = rd8(rec + F->Weapon);
    const Sight* sight = SightOf(item);
    uint32_t hand = table != 0 ? HandNode(table) : 0;
    bool ride = false;
    if (sight != nullptr && hand != 0 && aimReady)
    {
        V3 nx = Unit(Rdv(hand), fwdNow), ny = Unit(Rdv(hand + 0x10), rightNow), nz = Unit(Rdv(hand + 0x20), v3(0, 1, 0));
        V3 rear = Rdv(hand + 0x30) + nx * sight->x + ny * sight->y + nz * sight->z;
        V3 barrel = nx * -1.0f, bl = ToLocal(barrel, fwdNow, rightNow);
        uint32_t aim16 = rd16(rec + F->GunElevation);
        bool settled = s.havePrev && Dot(bl, s.prevBarrel) > SettledCos;
        bool aimMoving = s.havePrev && aim16 != s.prevAim;
        s.prevBarrel = bl;
        s.prevAim = aim16;
        s.havePrev = true;
        if (Sane3(rear) && Dot(barrel, aimDirNow) > SteadyCos && (settled || aimMoving))
        {
            V3 off = rear - (aimPos - shift);
            s.lineLocal = ToLocal(off - aimDirNow * Dot(off, aimDirNow), fwdNow, rightNow);
            s.haveSight = true;
            s.holding = false;
            ride = true;
        }
    }
    else
    {
        s.havePrev = false;
    }
    if (s.haveSight && raised && !ride && !s.holding)
    {
        s.holding = true; // a shot's recoil or a gesture: the camera keeps its step onto the line
    }
    s.sightBlend = Clamp(s.sightBlend + (s.haveSight && raised ? step : -step), 0, 1);
    if (!raised && s.sightBlend <= 0)
    {
        s.haveSight = false;
        s.holding = false;
    }
    if (s.haveSight)
    {
        aimPos = aimPos + FromLocal(V3{}, aimFwd, aimRight, s.lineLocal) * s.sightBlend;
    }
    V3 aimView = LookUp(aimDir, ta.look);

    // The mapper's turn of her next frame (toward the view as she stands, the way the stick walks her)
    s.plan = false;
    if (!raised && s.haveView && FreeToTurn(rec))
    {
        float rh = std::sqrt((htNow.x - pos.x) * (htNow.x - pos.x) + (htNow.z - pos.z) * (htNow.z - pos.z));
        int32_t most = TurnStep;
        if (rh * static_cast<float>(TurnStep) > TurnSwing * YawPerRadian)
        {
            most = static_cast<int32_t>(TurnSwing * YawPerRadian / rh); // (her head far ahead of her feet: bent forward)
        }
        float vs, vc;
        YawSinCos(s.viewYaw, &vs, &vc);
        uint32_t want;
        bool steer;
        if (PadWant(rec, s.padMag, s.padDir, s.padDash, static_cast<uint32_t>(YawStep(vs, vc)) * 16u, &want, &steer))
        {
            s.plan = true;
            s.planH = TurnToward(h16, want, most);
            s.planMost = most;
        }
    }

    // The normal view: where you look, from her eyes at her head as it'll be drawn
    V3 normalPos, normalDir;
    {
        V3 hyN = Unit(hyNow, v3(0, 1, 0)), hzN = Unit(hzNow, fwdNow);
        V3 fN = Unit(hzN * rc - hyN * rs, fwdNow), uN = Unit(hyN * rc + hzN * rs, v3(0, 1, 0)), rN = Unit(Cross(fN, uN), rightNow);
        V3 spot = htNow + hyNow * eyes.up + hzNow * eyes.forward;
        spot = spot + fN * tn.fwd + uN * tn.up + rN * tn.right;
        V3 rel = spot - htNow;
        V3 hf = Unit(v3(fN.x, 0, fN.z), fwdNow), hr = v3(-hf.z, 0, hf.x);
        float vs, vc;
        YawSinCos(s.viewYaw, &vs, &vc);
        V3 vf = v3(vs, 0, vc), vr = v3(-vc, 0, vs);
        V3 r = htNow - pos;
        int32_t turnNow = YawDiff(h16, s.outH), turnNext = 0;
        uint8_t kind = rd8(rec + F->AimState);
        V3 carry;
        if (s.haveOut)
        {
            V3 d = pos - s.lastPos;
            if (Dot(d, d) > PredictJump * PredictJump)
            {
                s.haveOut = false;
            }
        }
        if (!s.haveOut)
        {
            s.easing = false;
            s.quickHold = false;
            s.feetStep = {};
        }
        else
        {
            V3 d = pos - s.lastPos;
            float n1 = std::sqrt(Dot(d, d)), n0 = std::sqrt(Dot(s.feetStep, s.feetStep));
            bool unseen = turnNow - s.turnNext > TurnUnseen || s.turnNext - turnNow > TurnUnseen;
            if (n1 <= PredictJump && Dot(d, s.feetStep) > 0)
            {
                carry = n1 > n0 + PredictGrow ? d * (n0 / n1) : d;
                float nc = std::sqrt(Dot(carry, carry));
                if (nc > CarryMax)
                {
                    carry = carry * (CarryMax / nc);
                }
            }
            s.feetStep = n1 <= PredictJump ? d : V3{};
            if (s.easing)
            {
                float ne = std::sqrt(Dot(s.ease, s.ease));
                float st = Clamp(ne * EyeEase, EyeEaseMin, EyeEaseMax) * dt * 0.5f;
                if (!(ne > st))
                {
                    s.easing = false;
                    s.ease = {};
                }
                else
                {
                    s.ease = s.ease * ((ne - st) / ne);
                }
            }
            V3 sr = TurnTo(s.lastR, static_cast<float>(turnNow)) - TurnTo(s.lastR, static_cast<float>(s.turnNext));
            float ns = std::sqrt(Dot(sr, sr));
            if (!s.quickHold && unseen && ns > 0.5f)
            {
                V3 sw = sr * Clamp(Dot(r - TurnTo(s.lastR, static_cast<float>(s.turnNext)), sr) / (ns * ns), 0, 1);
                s.ease = s.easing ? s.ease - sw : sw * -1.0f;
                s.easing = true;
                float ne = std::sqrt(Dot(s.ease, s.ease));
                if (ne > EaseReach)
                {
                    s.ease = s.ease * (EaseReach / ne);
                }
            }
            if (s.plan)
            {
                turnNext = YawDiff(s.planH, h16);
            }
            else if (!s.planWas && turnNow <= GameTurn && turnNow >= -GameTurn)
            {
                turnNext = turnNow; // (the game's own steady turn, carried on)
            }
        }
        s.lastR = r;
        s.outH = h16;
        s.turnNext = turnNext;
        s.planWas = s.plan;
        V3 jp = pos + TurnTo(carry, static_cast<float>(turnNext)) + TurnTo(r, static_cast<float>(turnNext));
        V3 headSpot = jp + vf * Dot(rel, hf) + vr * Dot(rel, hr) + v3(0, rel.y, 0);
        // The game's quick turn: the camera keeps its spot by her feet through it
        if (stance == 1 && s.haveOut &&
            (kind == KindQuickTurn ||
             (s.quickHold && kind == 0 && Dot(fwdNow, vf) < QuickBackCos &&
              Dot((pos + s.quickSpot) - headSpot, (pos + s.quickSpot) - headSpot) > EyeEaseMax * EyeEaseMax &&
              Dot((pos + s.quickSpot) - jp, vf) >= HeadClear)))
        {
            if (!s.quickHold)
            {
                s.quickHold = true;
                s.quickSpot = s.lastOut - s.lastPos;
            }
            normalPos = pos + s.quickSpot;
        }
        else
        {
            if (s.quickHold)
            {
                s.quickHold = false;
                s.ease = (pos + s.quickSpot) - headSpot;
                s.easing = true;
            }
            if (s.easing)
            {
                float lo = std::min(0.0f, HeadClear - Dot(headSpot - jp, vf)), e = Dot(s.ease, vf);
                if (e < lo)
                {
                    s.ease = s.ease + vf * (lo - e);
                }
                headSpot = headSpot + s.ease;
            }
            normalPos = headSpot;
        }
        s.haveOut = true;
        s.lastOut = normalPos;
        s.lastPos = pos;
        float pitch = Clamp(s.viewPitch + tn.look, -85.0f, 85.0f) * 0.017453293f;
        float sy = std::sin(pitch), cy = std::cos(pitch), sp, cp;
        YawSinCos(s.viewYaw + (raised ? static_cast<float>(aimCarry) : 0.0f), &sp, &cp);
        normalDir = v3(cy * sp, sy, cy * cp);
    }

    // The view: normal <-> aiming in 0.2 s
    if (!raised)
    {
        aimView = normalDir;
    }
    V3 camPos = Lerp(normalPos, aimPos, s.aimBlend);
    V3 camDir = Unit(Lerp(normalDir, aimView, s.aimBlend), normalDir);
    float fov = tn.fov + (ta.fov - tn.fov) * s.aimBlend;
    if (!Sane3(camPos) || !Sane3(camDir))
    {
        return true; // (never a broken camera: the last one stays)
    }
    if (raised)
    {
        s.aimYaw = YawWrap(static_cast<float>(std::atan2(static_cast<double>(camDir.x), static_cast<double>(camDir.z)) *
                                              (32768.0 / 3.141592653589793)));
        s.aimPitch = static_cast<float>(std::atan2(static_cast<double>(camDir.y),
                                                   std::sqrt(static_cast<double>(camDir.x * camDir.x + camDir.z * camDir.z))) *
                                        (180.0 / 3.141592653589793));
    }
    game.WriteCamera({camPos.x, camPos.y, camPos.z},
                     {camPos.x + camDir.x * LookDistance, camPos.y + camDir.y * LookDistance, camPos.z + camDir.z * LookDistance});
    s.lastEye = camPos;
    s.lastAt = camPos + camDir * LookDistance;
    s.haveLast = true;
    game.SetFov(fov);

    // The characters right in front of the camera stay drawn (her own arms and gun, a zombie up close)
    if (!s.haveHide)
    {
        s.hideOriginal = Rdf(l.nearHide + 4);
        s.haveHide = true;
    }
    Wrf(l.nearHide + 4, 0.0f);
    Crosshair(stance == 2);
    return true;
}

void PadMap(Game& game)
{
    const ModernCamera::Layout& l = game.layout();
    F = &l.field;
    const uint32_t rec = game.PlayerRecord();
    const uint32_t held = l.padRecords + 8; // offline the local player's mapped record, 0
    // R3 is an ad-lib line too while the right stick looks
    if (s.padCentred && (rd32(l.systemPadPressed) & PadR3) != 0)
    {
        wr32(l.systemPadPressed, rd32(l.systemPadPressed) & ~PadR3);
    }
    if (!s.driving || !s.haveView)
    {
        s.plan = false;
        return;
    }
    uint32_t h = rd16(rec + F->Heading);
    uint8_t stance = rd8(rec + F->Stance);
    int32_t mag = static_cast<int16_t>(rd16(held + PadLeftStickMagnitude));
    uint32_t dir = rd16(held + PadLeftStickDirection);
    bool dash = (rd16(held) & DashButton(game)) != 0;
    s.padMag = mag;
    s.padDir = dir;
    s.padDash = dash;
    if (stance == 2 || stance == 3)
    {
        if (s.aimTurn != 0)
        {
            wr16(rec + F->Heading, static_cast<uint16_t>(h + static_cast<uint32_t>(s.aimTurn)));
        }
        s.aimApplied = s.aimTurn;
        s.aimTurn = 0;
    }
    else if (FreeToTurn(rec))
    {
        uint32_t cam16 = static_cast<uint32_t>(YawStep(Rdf(l.target) - Rdf(l.eye), Rdf(l.target + 8) - Rdf(l.eye + 8))) * 16u;
        uint32_t want;
        bool steer;
        bool live = PadWant(rec, mag, dir, dash, cam16, &want, &steer);
        uint32_t base = s.plan ? s.planH : h;
        uint32_t to = TurnToward(base, live ? TurnToward(h, want, s.plan ? s.planMost : TurnStep) : base, TurnAside);
        if (steer)
        {
            // The game walks her the way she faces: her heading, in steps of 16, as the stick's direction
            to = (to + 8u) & 0xFFF0;
            if (((cam16 + (dir & 0xFFF0)) & 0xFFFF) != to)
            {
                wr16(held + PadLeftStickDirection, static_cast<uint16_t>((to - cam16) & 0xFFFF));
            }
        }
        if ((live || s.plan) && to != h)
        {
            wr16(rec + F->Heading, static_cast<uint16_t>(to));
        }
    }
    s.plan = false;
}
}
