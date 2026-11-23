#pragma once

#include "Export.hpp"
#include "Memory.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace nmdk::library {

NMDK_EXPORT memory::Address symbol(std::string_view library, std::string_view name);
NMDK_EXPORT memory::Address base(std::string_view library);
NMDK_EXPORT std::string path(std::string_view library);

}
