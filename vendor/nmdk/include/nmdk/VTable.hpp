#pragma once

#include "Export.hpp"
#include "Memory.hpp"
#include <cstddef>
#include <string_view>

namespace nmdk::vtable {

NMDK_EXPORT memory::Address resolveFunction(std::string_view abiSymbol, std::size_t index,
                                           std::string_view library);
NMDK_EXPORT void** findVtable(std::string_view library, std::string_view abiSymbol);

}
