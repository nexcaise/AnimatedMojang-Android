#pragma once

#include <cstdint>

namespace nmdk::memory {

using Address = std::uintptr_t;

enum class Priority : int {
    Highest = 0,
    High = 100,
    Normal = 200,
    Low = 300,
    Lowest = 400
};

enum class Scan : std::uint32_t {
    Executable = 1u << 0,
    ReadOnly = 1u << 1,
    Writable = 1u << 2,
    AllReadable = (1u << 0) | (1u << 1) | (1u << 2)
};

}
