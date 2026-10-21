// The games' menus for the pointer (src/pointer.h): where each one keeps what it shows, per game, and the finders that read it.
// Each area has its own file: menus_title.cpp (the title menu and its submenus), menus_play.cpp (the selects and the game's own
// menus over play), menus_net.cpp (Network Play's setup screens), menus_lobby.cpp (the screens after login) and
// menus_keyboard.cpp (the software keyboard). docs/controls.md has what each was found from.
#pragma once

#include "pointer.h"

namespace Menus
{
enum class Disc
{
    File1,
    File2,
};

// Which game's addresses the finders read (set once, before the game starts)
void Use(Disc disc);
Disc Current();

// Where the title is (menus_title.cpp): running (its overlay loaded, its state made), and at PRESS START or its attract movies
bool TitleRunning();
bool AtPressStart();
// Network Play's overlay is loaded (menus_net.cpp)
bool NetworkRunning();

// The lobby's cursor engines (menus_lobby.cpp), each run through this: the cursor it moves is kept for the frame (a lobby
// menu is pointed at while the game moves its cursor), then the engine (`original`) runs. A byte's or a 16-bit word's engine
// (the cursor's address, its last value, how it moves), or a list's (the window's first row, the row in it, the list's
// length, the window's)
enum class LobbyEngine
{
    Byte,
    Half,
    List,
};
void LobbyCursor(Ps2Cpu* c, LobbyEngine engine, void (*original)(Ps2Cpu*));

// Every menu's finder, the topmost (a dialog over a list) first
const Pointer::Finder* Finders(int& count);

// The areas' finders (each file's own, in its order)
extern const Pointer::Finder SaveFinders[];
extern const int SaveFinderCount;
extern const Pointer::Finder KeyboardFinders[];
extern const int KeyboardFinderCount;
extern const Pointer::Finder SelectFinders[];
extern const int SelectFinderCount;
extern const Pointer::Finder PlayFinders[];
extern const int PlayFinderCount;
extern const Pointer::Finder LobbyFinders[];
extern const int LobbyFinderCount;
extern const Pointer::Finder NetFinders[];
extern const int NetFinderCount;
extern const Pointer::Finder CollectionFinders[];
extern const int CollectionFinderCount;
extern const Pointer::Finder TitleFinders[];
extern const int TitleFinderCount;
}
