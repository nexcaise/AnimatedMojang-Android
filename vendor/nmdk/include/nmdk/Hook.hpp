#pragma once

#include "Export.hpp"
#include "Memory.hpp"
#include <cstdint>
#include <string_view>

namespace nmdk::hook {

NMDK_EXPORT int hook(memory::Address target, memory::Address detour, memory::Address* original = nullptr,
                     memory::Priority priority = memory::Priority::Normal);
NMDK_EXPORT bool unhook(memory::Address target, memory::Address detour);
NMDK_EXPORT bool unhookAll(memory::Address target);
NMDK_EXPORT bool hookPLT(std::string_view library, std::string_view symbol,
                         memory::Address replacement, memory::Address* original = nullptr);
NMDK_EXPORT bool unhookPLT(std::string_view library, std::string_view symbol);
NMDK_EXPORT bool hookGOT(std::string_view library, std::string_view symbol,
                         memory::Address replacement, memory::Address* original = nullptr);
NMDK_EXPORT bool unhookGOT(std::string_view library, std::string_view symbol);
NMDK_EXPORT bool remove(memory::Address address);

}
