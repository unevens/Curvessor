cmake_minimum_required(VERSION 3.25)
project(Curvessor VERSION 2.1.0 LANGUAGES C CXX)

# Universal arm64+x86_64 on macOS — matches the JUCE side's default so a
# single bundle serves both Apple Silicon and Intel users. Flip to OFF
# (-DUNIVERSAL=OFF) for fast single-arch dev iteration. Cost when ON:
# ~2x build time, ~2x binary size.
#
# Unblocked by an avec patch (VCL_NAMESPACE=vcl) that moves vectorclass's
# `extend` and friends into namespace vcl::, so they no longer collide
# with the `extend = 0x40` enumerator in macOS's <MacTypes.h> that
# iPlug2's IControls.h pulls in transitively via Cocoa headers.
if(APPLE)
    option(UNIVERSAL "Build a universal arm64+x86_64 binary on macOS" ON)
    if(UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

# macOS arm64 toolchain preflight — shared with Overdraw, lives in
# unevens/iplug-helpers since the rationale is identical for both ports.
include(${CMAKE_SOURCE_DIR}/../iplug-helpers/cmake/HomebrewClang22Preflight.cmake)
iplug_helpers_require_homebrew_clang_22("Curvessor" "Source/CurvessorDsp.cpp")

# iPlug2 framework
set(IPLUG2_DIR ${CMAKE_SOURCE_DIR}/iPlug2)
include(${IPLUG2_DIR}/iPlug2.cmake)

# iPlug2 hard-codes C++17. Bump to C++20 for Curvessor's DSP code.
# This must come after the iPlug2.cmake include because that file sets
# CMAKE_CXX_STANDARD itself.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(iPlug2 REQUIRED)

add_subdirectory(iplug)
