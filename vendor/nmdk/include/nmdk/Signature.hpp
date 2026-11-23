#pragma once

#include "Export.hpp"
#include "Memory.hpp"
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nmdk::signature {

NMDK_EXPORT memory::Address resolveSignature(std::string_view library, std::string_view pattern,
                                              memory::Scan scan = memory::Scan::Executable);

// Maps every pattern to its address; 0 for a pattern that was not found.
NMDK_EXPORT std::unordered_map<std::string, memory::Address>
resolveSignatures(std::string_view library, std::span<const std::string_view> patterns,
                  memory::Scan scan = memory::Scan::Executable);

// Tries the patterns in order and returns the first match; 0 when none of them is found.
NMDK_EXPORT memory::Address
resolveSignatureWithFallbacks(std::string_view library, std::span<const std::string_view> patterns,
                              memory::Scan scan = memory::Scan::Executable);

inline std::unordered_map<std::string, memory::Address>
resolveSignatures(std::string_view library, std::initializer_list<std::string_view> patterns,
                  memory::Scan scan = memory::Scan::Executable) {
    return resolveSignatures(library, std::span<const std::string_view>(patterns.begin(), patterns.size()), scan);
}

inline memory::Address
resolveSignatureWithFallbacks(std::string_view library, std::initializer_list<std::string_view> patterns,
                              memory::Scan scan = memory::Scan::Executable) {
    return resolveSignatureWithFallbacks(library, std::span<const std::string_view>(patterns.begin(), patterns.size()), scan);
}

}
