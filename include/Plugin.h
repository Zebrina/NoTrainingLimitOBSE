#pragma once

#include <string_view>

#define PLUGIN_MODNAME "No Training Limit"
#define PLUGIN_AUTHOR "Zebrina"

using namespace std::literals;

namespace Plugin
{
    //inline constexpr auto MODNAME = PLUGIN_MODNAME;
    //inline constexpr auto AUTHOR = PLUGIN_AUTHOR;
    inline constexpr auto VERSION = plugin_version::make(1, 1, 6, 0);
    inline constexpr auto INTERNALNAME = "NoTrainingLimit"sv;
    inline constexpr auto CONFIGFILE = "OBSE\\Plugins\\NoTrainingLimit.toml"sv;
}
