#pragma once

#include "OBSE/OBSE.h"

#include <string_view>

using namespace std::literals;

namespace Plugin
{
    inline constexpr auto MODNAME = "No Training Limit"sv;
    inline constexpr auto AUTHOR = "Zebrina"sv;
    inline constexpr auto VERSION = OBSE::Version{ 1, 1, 6, 0 };
    inline constexpr auto INTERNALNAME = "NoTrainingLimit"sv;
    inline constexpr auto CONFIGFILE = "OBSE\\Plugins\\NoTrainingLimit.toml"sv;
}
