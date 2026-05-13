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

# macOS arm64 toolchain preflight (carried over from the JUCE build).
#
# Apple Clang (Xcode 17, CLT 21) and pre-22 upstream Clang have a NEON
# codegen bug that triggers a frontend bus error in Source/CurvessorDsp.cpp
# (the heavy SIMD spline TU, which the iPlug2 build pulls in once the DSP
# port lands). The fix shipped in upstream LLVM 22, so we require Homebrew
# Clang >= 22 here. Verified working: Homebrew clang 22.1.4.
if(APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
              OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
       OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
           AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22))
        set(_curvessor_brew_clang "")
        foreach(_p /opt/homebrew/opt/llvm/bin/clang++
                   /usr/local/opt/llvm/bin/clang++)
            if(EXISTS "${_p}")
                set(_curvessor_brew_clang "${_p}")
                break()
            endif()
        endforeach()
        if(_curvessor_brew_clang)
            string(REPLACE "++" "" _curvessor_brew_cc "${_curvessor_brew_clang}")
            message(FATAL_ERROR
                "Curvessor on macOS arm64 requires Homebrew Clang >= 22 "
                "(detected: ${CMAKE_CXX_COMPILER_ID} "
                "${CMAKE_CXX_COMPILER_VERSION}). Apple Clang and pre-22 "
                "upstream Clang hit a NEON codegen bus error in "
                "Source/CurvessorDsp.cpp. You already have a suitable "
                "compiler installed; reconfigure with:\n"
                "  rm -rf ${CMAKE_BINARY_DIR}\n"
                "  cmake -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR} \\\n"
                "    -DCMAKE_C_COMPILER=${_curvessor_brew_cc} \\\n"
                "    -DCMAKE_CXX_COMPILER=${_curvessor_brew_clang}")
        else()
            message(FATAL_ERROR
                "Curvessor on macOS arm64 requires Homebrew Clang >= 22 "
                "(detected: ${CMAKE_CXX_COMPILER_ID} "
                "${CMAKE_CXX_COMPILER_VERSION}). Install with:\n"
                "  brew install llvm\n"
                "and reconfigure with:\n"
                "  rm -rf ${CMAKE_BINARY_DIR}\n"
                "  cmake -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR} \\\n"
                "    -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \\\n"
                "    -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++")
        endif()
    endif()
endif()

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
