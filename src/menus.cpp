#include "menus.h"

#include <vector>

namespace Menus
{
namespace
{
Disc g_disc = Disc::File2;
std::vector<Pointer::Finder> g_finders;
}

void Use(Disc disc)
{
    g_disc = disc;
    g_finders.clear();
    // The save data's screens come over every other menu (the options' save/load, the collection's data convert...)
    g_finders.insert(g_finders.end(), SaveFinders, SaveFinders + SaveFinderCount);
    g_finders.insert(g_finders.end(), PlayFinders, PlayFinders + PlayFinderCount);
    g_finders.insert(g_finders.end(), SelectFinders, SelectFinders + SelectFinderCount);
    g_finders.insert(g_finders.end(), NetFinders, NetFinders + NetFinderCount);
    g_finders.insert(g_finders.end(), TitleFinders, TitleFinders + TitleFinderCount);
}

Disc Current() { return g_disc; }

const Pointer::Finder* Finders(int& count)
{
    count = static_cast<int>(g_finders.size());
    return g_finders.data();
}
}
