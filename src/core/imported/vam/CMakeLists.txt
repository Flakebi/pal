
##
 #######################################################################################################################
 #
 #  Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All Rights Reserved.
 #
 #  Permission is hereby granted, free of charge, to any person obtaining a copy
 #  of this software and associated documentation files (the "Software"), to deal
 #  in the Software without restriction, including without limitation the rights
 #  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 #  copies of the Software, and to permit persons to whom the Software is
 #  furnished to do so, subject to the following conditions:
 #
 #  The above copyright notice and this permission notice shall be included in all
 #  copies or substantial portions of the Software.
 #
 #  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 #  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 #  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 #  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 #  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 #  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 #  SOFTWARE.
 #
 #######################################################################################################################

cmake_minimum_required(VERSION 3.1)

project(VAM VERSION 1.2.0 LANGUAGES CXX)

add_library(vam STATIC "")

target_include_directories(vam PUBLIC  inc
                               PRIVATE src/core)

target_sources(vam PRIVATE inc/vambasictypes.h
                           inc/vamtypes.h
                           inc/vaminterface.h)

target_sources(vam PRIVATE src/vaminterface.cpp)

target_sources(vam PRIVATE src/core/vammacros.h
                           src/core/vamlink.h
                           src/core/vamtree.h
                           src/core/vamcommon.h
                           src/core/vamobject.h
                           src/core/vamobject.cpp
                           src/core/vamrange.h
                           src/core/vamrange.cpp
                           src/core/vamsuballoc.h
                           src/core/vamsuballoc.cpp
                           src/core/vamsectionalloc.h
                           src/core/vamsectionalloc.cpp
                           src/core/vamptb.h
                           src/core/vamptb.cpp
                           src/core/vamdevice.h
                           src/core/vamdevice.cpp)

#if ADDR_CLOSED_SOURCE
if (CMAKE_SYSTEM_NAME MATCHES Windows)
    # CMake-TODO: Why vam_basedrv.h is not part of VAM interface?
    target_include_directories(vam PRIVATE
                                      ${GLOBAL_ROOT_SRC_DIR}/drivers/inc/shared)
endif()

# CMake-TODO: What these macros are used for?
#             Should these be part of VAM interface?
# - BUILDING_CMMQSLIB
# - BUILDING_CMM
# - _BASETSD_H_
#endif // ADDR_CLOSED_SOURCE

target_compile_definitions(vam PRIVATE $<$<CONFIG:Debug>:VAM_DEBUG>)

set_target_properties(vam PROPERTIES CXX_STANDARD              11
                                     CXX_STANDARD_REQUIRED     ON
                                     CXX_EXTENSIONS            OFF
                                     POSITION_INDEPENDENT_CODE ON)

# W/A: Replace this code by using CMake module CheckIPOSupported, available since CMake 3.9.
if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU") AND (NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.8")))

    # [GCC] Options That Control Optimization
    #   https://gcc.gnu.org/onlinedocs/gcc-8.1.0/gcc/Optimize-Options.html
    target_compile_options(vam PRIVATE $<$<CONFIG:Release>:-flto>)

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

    # [Clang] Command Line Argument Reference
    #    https://clang.llvm.org/docs/ClangCommandLineReference.html
    #
    # [Clang] Thin LTO
    #    https://clang.llvm.org/docs/ThinLTO.html
    target_compile_options(vam PRIVATE -flto=thin)

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")

    # [MSVC] Whole Program Optimization
    #    https://msdn.microsoft.com/en-us/library/0zza0de8.aspx
    target_compile_options(vam PRIVATE /GL)

    # [MSVC] Link-time Code Generation
    #    https://msdn.microsoft.com/en-us/library/xbf3tbeh.aspx
    set_target_properties(vam PROPERTIES STATIC_LIBRARY_FLAGS /LTCG)

else()
    message(WARNING "IPO was not enabled!")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")

    # [GCC] Exceptions
    #   https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_exceptions.html
    #
    # [GCC] Options Controlling C++ Dialect
    #   https://gcc.gnu.org/onlinedocs/gcc-8.1.0/gcc/C_002b_002b-Dialect-Options.html
    #
    # [GCC] Options That Control Optimization
    #   https://gcc.gnu.org/onlinedocs/gcc-8.1.0/gcc/Optimize-Options.html
    target_compile_options(vam PRIVATE
        -fno-exceptions  # Disable exception handling support.
        -fno-rtti        # Disable run-time type information support.
        -fcheck-new      # Check if pointer returned by operator new is non-null.
        -fno-math-errno) # Single instruction math operations do not set ERRNO.

    # [GCC] Options to Request or Suppress Warnings
    #   https://gcc.gnu.org/onlinedocs/gcc-8.1.0/gcc/Warning-Options.html
    target_compile_options(vam PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wno-unused
        -Wno-unused-parameter
        -Wno-ignored-qualifiers
        -Wno-missing-field-initializers)

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")

    # [MSVC] Code Analysis
    #    https://msdn.microsoft.com/en-us/library/ms173498.aspx
    #
    # [MSVC] Exception Handling Model
    #    https://msdn.microsoft.com/en-us/library/1deeycx5.aspx
    #
    # [MSVC] Enable Run-Time Type Information
    #    https://msdn.microsoft.com/en-us/library/we6hfdy0.aspx
    #
    # [MSVC] Buffer Security Check
    #    https://msdn.microsoft.com/en-us/library/8dbf701c.aspx
    #
    # [MSVC] Whole Program Optimization
    #    https://msdn.microsoft.com/en-us/library/0zza0de8.aspx
    target_compile_options(vam PRIVATE
        $<$<CONFIG:Debug>:/analyze>
              # Turns on static analysis for debug build.
        /EHsc # Catches only C++ exceptions and assumes
              # functions declared as extern "C" never throw a C++ exception.
        /GR-  # Disables run-time type information.
        /GS-) # Disables detection of buffer overruns.

    # [MSVC] Warning Level
    #    https://msdn.microsoft.com/en-us/library/thxezb7y.aspx
    #
    # [MSVC] Compiler Warnings
    #    https://msdn.microsoft.com/en-us/library/ysb0wexw.aspx
    #    https://msdn.microsoft.com/en-us/library/t460hcc3.aspx
    #    https://msdn.microsoft.com/en-us/library/cfahxw6k.aspx
    target_compile_options(vam PRIVATE
        /W4      # Enable warning level 4.
        /WX      # Treat warnings as errors.
        /wd4100  # unreferenced formal parameter
        /wd4127  # conditional expression is constant
        /wd4189  # local variable is initialized but not referenced
        /wd4201  # nonstandard extension used : nameless struct/union
        /wd4701  # potentially uninitialized local variable
        /wd4703) # potentially uninitialized local pointer variable

else()
    message(FATAL_ERROR "Compiler ${CMAKE_CXX_COMPILER_ID} is not supported!")
endif()
