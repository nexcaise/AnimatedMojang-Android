#pragma once

#include "Export.hpp"
#include "Hook.hpp"
#include "Library.hpp"
#include "Memory.hpp"
#include "Patch.hpp"
#include "Signature.hpp"
#include "VTable.hpp"
#include <cstdint>

namespace nmdk {

enum class Architecture {
    Unknown,
    ARM32,
    AArch64,
    X86,
    X86_64
};

NMDK_EXPORT Architecture architecture() noexcept;
NMDK_EXPORT const char* version() noexcept;

}
