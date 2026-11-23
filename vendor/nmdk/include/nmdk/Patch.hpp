#pragma once

#include "Export.hpp"
#include "Memory.hpp"
#include <cstddef>
#include <type_traits>

namespace nmdk::patch {

NMDK_EXPORT bool write(memory::Address address, const void* bytes, std::size_t size,
                       void* backup = nullptr);
NMDK_EXPORT bool nop(memory::Address address, std::size_t size);
// Restores the bytes that were at `address` before the most recent tracked write()/nop() there.
NMDK_EXPORT bool unpatch(memory::Address address);

template <typename T>
[[nodiscard]] bool write(memory::Address address, const T& value, T* backup = nullptr) {
    static_assert(std::is_trivially_copyable_v<T>);
    return write(address, &value, sizeof(T), backup);
}

}
