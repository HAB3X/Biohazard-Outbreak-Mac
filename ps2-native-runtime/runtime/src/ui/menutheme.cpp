// The PC menus' themes (menutheme.h)
#include "menutheme.h"

#include "../runtime.h"
#include "ps2/runtime.h"

#include <cstring>

namespace Ps2
{
extern const Ps2GameInfo* g_game;
}

namespace Ps2::Ui
{
namespace
{
TextStyle Style(OverlayFont font, f32 size, Rgba32 colour, f32 spacing = 0.0f)
{
    TextStyle style;
    style.font = font;
    style.size = size;
    style.colour = colour;
    style.letterSpacing = spacing;
    return style;
}

BoxStyle Box(Rgba32 fill, f32 radius)
{
    BoxStyle box;
    box.fill = fill;
    box.radius = radius;
    return box;
}

// The Crash Twinsanity native port's options screen (its ui/settingsview.cpp: the approved design's values)
MenuTheme Crash()
{
    constexpr Rgba32 Orange = Hex(0xff9a1f), DeepOrange = Hex(0xff8a00), Lip = Hex(0xffb43a), Brown = Hex(0x3a1200);
    constexpr Rgba32 Ink = Hex(0x173a6b), PillBlue = Hex(0x1b6fd0), PillFill = Hex(0xeaf2ff), White = Hex(0xffffff);
    constexpr Rgba32 KeyInk = Hex(0x1b2340), Muted = Hex(0x6b86a8);
    MenuTheme t;
    t.backdrop = MenuBackdrop::Spiral;
    t.dim = Hex(0x04122a, 0.55f);
    t.washTop = Hex(0x0a3b7a, 0.55f);
    t.washBottom = Hex(0x071f45, 0.55f);

    t.title = Style(OverlayFont::Display, 66.0f, Orange, 1.0f);
    t.title.outline = 2.0f;
    t.title.outlineColour = Brown;
    t.title.dropY = 4.0f;
    t.title.dropColour = Brown;
    t.titleX = 64.0f;
    t.titleTop = 40.0f;
    t.subtitle = Style(OverlayFont::Body, 18.0f, Hex(0xbcd9ff), 3.0f);
    t.subtitle.dropY = 1.0f;
    t.subtitle.dropColour = Hex(0x000000, 0.35f);
    t.subtitleX = 68.0f;
    t.subtitleTop = 118.0f;

    t.tab = Box(Hex(0xffffff, 0.08f), 16.0f);
    t.tab.border = 2.0f;
    t.tab.borderColour = Hex(0xffffff, 0.15f);
    t.tabSelected = Box(White, 16.0f);
    t.tabSelected.border = 2.0f;
    t.tabSelected.borderColour = White;
    t.tabSelected.lip = 6.0f;
    t.tabSelected.lipColour = Lip;
    t.tabFont = OverlayFont::Display;
    t.tabInk = Hex(0xdbe9ff);
    t.tabInkSelected = DeepOrange;
    t.tabOutlineSelected = 1.2f;
    t.tabOutlineColour = Hex(0x4a1800);

    t.panel = Box(White, 22.0f);
    t.panel.lip = 10.0f;
    t.panel.lipColour = Lip;
    t.panel.shadowOffset = 26.0f;
    t.panel.shadowBlur = 50.0f;
    t.panel.shadowColour = Hex(0x000000, 0.4f);
    t.bar = Orange;
    t.barBinding = Hex(0xfff0dc);
    t.barRadius = 12.0f;
    t.separator = Hex(0xeef3fa);
    t.flash = Lip;
    t.heading = Muted;
    t.headerBacking = White;
    t.label = Ink;
    t.labelSelected = White;
    t.pill = PillFill;
    t.pillSelected = Hex(0xffffff, 0.25f);
    t.pillText = PillBlue;
    t.pillTextSelected = White;
    t.chevron = Orange;
    t.chevronSelected = White;
    t.cell = PillFill;
    t.cellSelected = Orange;
    t.cellText = PillBlue;
    t.cellTextSelected = White;
    t.track = Hex(0xdbe7f7);
    t.trackFill = Hex(0xffc04d);
    t.trackFillBottom = DeepOrange;
    t.knob = Box(Hex(0xffb347), 13.0f);
    t.knob.fillBottom = Hex(0xe8650a);
    t.knob.border = 2.0f;
    t.knob.borderColour = Brown;
    t.knob.lip = 2.0f;
    t.knob.lipColour = Hex(0x000000, 0.25f);
    t.scrollbar = Orange;
    t.questionInk = Ink;
    t.questionDetail = Muted;
    t.button = Box(PillFill, 20.0f);
    t.buttonChosen = Box(Orange, 20.0f);
    t.buttonChosen.lip = 3.0f;
    t.buttonChosen.lipColour = Hex(0xd86f00);
    t.buttonText = PillBlue;
    t.buttonTextChosen = White;
    t.description = Style(OverlayFont::Body, 19.0f, Hex(0xd6e8ff));
    t.description.dropY = 1.0f;
    t.description.dropColour = Hex(0x000000, 0.3f);
    t.keycap = Box(Hex(0xfdfdfd), 8.0f);
    t.keycap.fillBottom = Hex(0xcfd6e3);
    t.keycap.border = 2.0f;
    t.keycap.borderColour = KeyInk;
    t.keycap.lip = 4.0f;
    t.keycap.lipColour = KeyInk;
    t.keyInk = KeyInk;
    t.footer = Hex(0x000000, 0.45f);
    t.footerWord = Style(OverlayFont::Body, 19.0f, White);
    t.footerWord.dropY = 2.0f;
    t.footerWord.dropColour = Hex(0x000000);
    return t;
}

// Biohazard Outbreak's own menus (its pause and status screens): the game's picture dimmed behind, dark navy panels in a
// lavender-blue frame, the tabs as the status screen's ITEM / SPECIAL / FILE (the chosen one orange-brown in a light frame),
// the chosen row's text green as the pause menu's EXIT, condensed capitals (DIN Condensed, as the game's headings' cut) with
// a blue outline as its PAUSE
MenuTheme Outbreak()
{
    constexpr Rgba32 Frame = Hex(0x7f88e6), Lavender = Hex(0x9aa4e8), Navy = Hex(0x141a44), Text = Hex(0xe8e8f4);
    constexpr Rgba32 Green = Hex(0x58ff7a), Orange = Hex(0xd88a2a), OrangeDeep = Hex(0x7a3a0c), Cream = Hex(0xfff4e0);
    MenuTheme t = Crash();
    t.backdrop = MenuBackdrop::GameDimmed;
    t.dim = Hex(0x02040e, 0.62f);
    // No band down the left: the game's own menus dim the picture evenly
    t.washTop = Hex(0x000000, 0.0f);
    t.washBottom = Hex(0x000000, 0.0f);
    t.fontFiles[static_cast<u32>(OverlayFont::Display)] = "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf";
    t.fontFiles[static_cast<u32>(OverlayFont::Body)] = "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf";
    t.fontFiles[static_cast<u32>(OverlayFont::BodyBold)] = "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf";

    t.title = Style(OverlayFont::Display, 72.0f, Hex(0xf4f4ff), 2.0f);
    t.title.outline = 3.0f;
    t.title.outlineColour = Hex(0x1a2a8a);
    t.title.dropY = 3.0f;
    t.title.dropColour = Hex(0x000000, 0.6f);
    t.titleTop = 44.0f;
    t.subtitle = Style(OverlayFont::Body, 17.0f, Lavender, 3.0f);
    t.subtitle.dropY = 1.0f;
    t.subtitle.dropColour = Hex(0x000000, 0.6f);

    t.tab = Box(Hex(0x0b1030, 0.88f), 6.0f);
    t.tab.fillBottom = Hex(0x05081c, 0.88f);
    t.tab.border = 2.0f;
    t.tab.borderColour = Hex(0x6f78d8);
    t.tabSelected = Box(Orange, 6.0f);
    t.tabSelected.fillBottom = OrangeDeep;
    t.tabSelected.border = 2.0f;
    t.tabSelected.borderColour = Cream;
    t.tabInk = Hex(0xe6e8ff);
    t.tabInkSelected = Hex(0xffffff);
    t.tabOutlineSelected = 1.5f;
    t.tabOutlineColour = Hex(0x3a1a00);

    t.panel = Box(Hex(0x080c26, 0.92f), 14.0f);
    t.panel.fillBottom = Hex(0x04061a, 0.92f);
    t.panel.border = 3.0f;
    t.panel.borderColour = Frame;
    t.panel.shadowOffset = 10.0f;
    t.panel.shadowBlur = 30.0f;
    t.panel.shadowColour = Hex(0x000000, 0.5f);
    t.bar = Hex(0x1d2a78, 0.9f);
    t.barBinding = Hex(0x141c50);
    t.barRadius = 6.0f;
    t.separator = Hex(0x2a3270, 0.8f);
    t.flash = Hex(0x3a8a4a);
    t.heading = Lavender;
    t.headerBacking = Hex(0x080c26);
    t.label = Hex(0xd8dae8);
    t.labelSelected = Green;
    t.pill = Navy;
    t.pillSelected = Hex(0x0e3a20);
    t.pillText = Text;
    t.pillTextSelected = Hex(0x8affa0);
    t.chevron = Lavender;
    t.chevronSelected = Green;
    t.cell = Navy;
    t.cellSelected = Orange;
    t.cellText = Text;
    t.cellTextSelected = Hex(0xffffff);
    t.track = Hex(0x1a2050);
    t.trackFill = Hex(0x6f78d8);
    t.trackFillBottom = Hex(0x3a44a8);
    t.knob = Box(Text, 11.0f);
    t.knob.fillBottom = Hex(0x9aa0c8);
    t.knob.border = 2.0f;
    t.knob.borderColour = Hex(0x1a2a8a);
    t.scrollbar = Frame;
    t.questionInk = Hex(0xf0f0ff);
    t.questionDetail = Lavender;
    t.button = Box(Navy, 6.0f);
    t.button.border = 2.0f;
    t.button.borderColour = Hex(0x6f78d8);
    t.buttonChosen = t.tabSelected;
    t.buttonText = Text;
    t.buttonTextChosen = Hex(0xffffff);
    t.description = Style(OverlayFont::Body, 19.0f, Hex(0xf0f0ff));
    t.description.dropY = 2.0f;
    t.description.dropColour = Hex(0x000000, 0.8f);
    t.keycap = Box(Navy, 5.0f);
    t.keycap.fillBottom = Hex(0x0a0e2c);
    t.keycap.border = 2.0f;
    t.keycap.borderColour = Lavender;
    t.keycap.lip = 2.0f;
    t.keycap.lipColour = Hex(0x000000, 0.6f);
    t.keyInk = Text;
    t.footer = Hex(0x000000, 0.6f);
    t.footerWord = Style(OverlayFont::Body, 19.0f, Text);
    t.footerWord.dropY = 2.0f;
    t.footerWord.dropColour = Hex(0x000000);
    return t;
}
}

const MenuTheme& Theme()
{
    static const MenuTheme theme = [] {
        const char* name = g_game != nullptr ? g_game->menuTheme : nullptr;
        if (name == nullptr || std::strcmp(name, "crash") == 0)
        {
            return Crash();
        }
        if (std::strcmp(name, "outbreak") == 0)
        {
            return Outbreak();
        }
        Log("menus: no theme \"%s\" (crash, outbreak): the crash one", name);
        return Crash();
    }();
    return theme;
}
}
