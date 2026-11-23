#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define NMDK_EXPORT [[maybe_unused]] __attribute__((visibility("default")))
#else
#define NMDK_EXPORT [[maybe_unused]]
#endif
