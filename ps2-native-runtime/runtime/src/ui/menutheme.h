// The PC menus' look, chosen by the game (Ps2GameInfo::menuTheme): every colour, box and text style the view draws with
// (menuview.cpp), the backdrop and the fonts. "crash" (the default) is the Crash Twinsanity native port's options screen;
// "outbreak" follows Biohazard Outbreak's own pause and status screens (menutheme.cpp)
#pragma once

#include "overlay.h"

namespace Ps2::Ui
{
enum class MenuBackdrop : u32
{
    // The blue gradient with the ring target and the Twinsanity spiral (OverlayFrame::Backdrop)
    Spiral,
    // The game's picture under a dark wash, as a game's own pause screen over its play
    GameDimmed,
};

struct MenuTheme
{
    MenuBackdrop backdrop = MenuBackdrop::Spiral;
    // The backdrop's wash for GameDimmed, and the pause menu's: the dim over the whole picture and the wash from the left
    Rgba32 dim{};
    Rgba32 washTop{};
    Rgba32 washBottom{};
    // The fonts' files (OverlayFont's order; nullptr: the bundled one), read from the Mac's own fonts
    const char* fontFiles[static_cast<u32>(OverlayFont::Count)] = {};

    // The title (its style and place), the game's name under it, and whether the title is drawn in capitals as given
    TextStyle title{};
    f32 titleX = 0, titleTop = 0;
    TextStyle subtitle{};
    f32 subtitleX = 0, subtitleTop = 0;

    // The tabs (and the pause menu's buttons) down the left: idle and selected boxes, their text's colours, the selected
    // text's outline
    BoxStyle tab{};
    BoxStyle tabSelected{};
    OverlayFont tabFont = OverlayFont::Display;
    Rgba32 tabInk{};
    Rgba32 tabInkSelected{};
    f32 tabOutlineSelected = 0;
    Rgba32 tabOutlineColour{};

    // The panel of rows (and the question box's)
    BoxStyle panel{};
    // The selected row's bar (a binding row's: the cells carry the selection), its corners
    Rgba32 bar{};
    Rgba32 barBinding{};
    f32 barRadius = 0;
    Rgba32 separator{};
    Rgba32 flash{};
    // A group's heading, the controls table's header and its backing, "Nothing here yet"
    Rgba32 heading{};
    Rgba32 headerBacking{};
    // A row: its label, the value's pill, its text and its chevrons, idle and selected
    Rgba32 label{}, labelSelected{};
    Rgba32 pill{}, pillSelected{};
    Rgba32 pillText{}, pillTextSelected{};
    Rgba32 chevron{}, chevronSelected{};
    // A binding's cells, idle and selected
    Rgba32 cell{}, cellSelected{};
    Rgba32 cellText{}, cellTextSelected{};
    // A slider's track, fill and knob
    Rgba32 track{};
    Rgba32 trackFill{}, trackFillBottom{};
    BoxStyle knob{};
    Rgba32 scrollbar{};
    // The question box's text, its detail, its buttons (the chosen one with a lip)
    Rgba32 questionInk{};
    Rgba32 questionDetail{};
    BoxStyle button{};
    BoxStyle buttonChosen{};
    Rgba32 buttonText{}, buttonTextChosen{};
    // The highlighted row's description under the panel
    TextStyle description{};
    // The keycaps of the bar of prompts, their ink, the bar and its words
    BoxStyle keycap{};
    Rgba32 keyInk{};
    Rgba32 footer{};
    TextStyle footerWord{};
};

// The game's theme (Ps2GameInfo::menuTheme; an unknown name is logged and the default used)
const MenuTheme& Theme();
}
