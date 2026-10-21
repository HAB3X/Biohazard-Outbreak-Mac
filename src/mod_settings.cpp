// The mods' rows in the PC menus (mod_settings.h)
#include "mod_settings.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace ModSettings
{
namespace
{
// The runtime's settings take plain functions: one pair per row, made for every index up to this
constexpr int Capacity = 192;

std::array<Row, Capacity> g_rows;
std::array<std::atomic<int>, Capacity> g_choices;
int g_count = 0;

template <int I>
int Get()
{
    return g_choices[I].load();
}

template <int I>
void Set(int choice)
{
    g_choices[I].store(choice);
}

template <int... I>
constexpr std::array<int (*)(void), Capacity> Getters(std::integer_sequence<int, I...>)
{
    return {&Get<I>...};
}

template <int... I>
constexpr std::array<void (*)(int), Capacity> Setters(std::integer_sequence<int, I...>)
{
    return {&Set<I>...};
}

const std::array<int (*)(void), Capacity> g_getters = Getters(std::make_integer_sequence<int, Capacity>{});
const std::array<void (*)(int), Capacity> g_setters = Setters(std::make_integer_sequence<int, Capacity>{});
}

int Add(const Row& row)
{
    if (g_count >= Capacity)
    {
        std::fprintf(stderr, "mod settings: more than %d rows (\"%s\")\n", Capacity, row.name);
        std::abort();
    }
    g_rows[g_count] = row;
    g_choices[g_count].store(row.initial);
    return g_count++;
}

int Choice(int index) { return g_choices[index].load(); }

std::vector<Ps2GameSetting> Settings(const Ps2GameSetting* others, int otherCount)
{
    std::vector<Ps2GameSetting> settings(others, others + otherCount);
    for (int i = 0; i < g_count; i++)
    {
        const Row& r = g_rows[i];
        settings.push_back({r.tab, r.name, r.choices, g_getters[i], g_setters[i], r.group, r.description});
    }
    return settings;
}
}
