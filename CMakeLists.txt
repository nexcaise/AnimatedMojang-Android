cmake_minimum_required(VERSION 3.22)
project(AnimatedMojang VERSION 2.3.1 LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "Animated Mojang is an Android arm64-v8a native mod. Configure with the Android NDK toolchain.")
endif()
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Only arm64-v8a is supported by the supplied NMDK binary.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NMDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/nmdk" CACHE PATH "NMDK distribution root")
set(NMDK_INCLUDE_DIR "${NMDK_ROOT}/include")
set(NMDK_LIBRARY "${NMDK_ROOT}/libs/${ANDROID_ABI}/libnmdk.so")
if(NOT EXISTS "${NMDK_INCLUDE_DIR}/nmdk/NMDK.hpp" OR NOT EXISTS "${NMDK_LIBRARY}")
    message(FATAL_ERROR "NMDK headers or ${NMDK_LIBRARY} are missing.")
endif()

find_library(ANDROID_LOG_LIBRARY log REQUIRED)
find_library(ANDROID_DL_LIBRARY dl REQUIRED)
find_library(ANDROID_EGL_LIBRARY EGL REQUIRED)
find_library(ANDROID_GLES_LIBRARY GLESv3 REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE MOD_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(AnimatedMojang SHARED ${MOD_SOURCES})

include(CheckIPOSupported)
check_ipo_supported(RESULT AM_IPO_OK OUTPUT AM_IPO_MSG)
if(AM_IPO_OK)
    set_property(TARGET AnimatedMojang PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

target_include_directories(AnimatedMojang PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${NMDK_INCLUDE_DIR}"
)

target_compile_options(AnimatedMojang PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-DNDEBUG>
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Wno-unused-parameter
)

target_link_options(AnimatedMojang PRIVATE
    -Wl,--gc-sections
    -Wl,--as-needed
    $<$<CONFIG:Release>:-Wl,--strip-all>
    -Wl,-z,relro
    -Wl,-z,now
    "-Wl,-rpath,\$ORIGIN"
)

target_link_libraries(AnimatedMojang PRIVATE
    "${NMDK_LIBRARY}"
    "${ANDROID_LOG_LIBRARY}"
    "${ANDROID_DL_LIBRARY}"
    "${ANDROID_GLES_LIBRARY}"
    "${ANDROID_EGL_LIBRARY}"
    Threads::Threads
)

set_target_properties(AnimatedMojang PROPERTIES
    OUTPUT_NAME AnimatedMojang
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
