# vim: syntax=cmake
if(NOT CMAKE_BUILD_TYPE)
    # default to Release build for GCC builds
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()
message(STATUS "cmake version ${CMAKE_VERSION}")
if(POLICY CMP0025)
    cmake_policy(SET CMP0025 OLD) # report Apple's Clang as just Clang
endif()
if(POLICY CMP0042)
    cmake_policy(SET CMP0042 NEW) # MACOSX_RPATH
endif()
if(POLICY CMP0054)
    cmake_policy(SET CMP0054 OLD) # Only interpret if() arguments as variables or keywords when unquoted
endif()

project (x265)
cmake_minimum_required (VERSION 2.8.8) # OBJECT libraries require 2.8.8
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckCXXCompilerFlag)

option(FPROFILE_GENERATE "Compile executable to generate usage data" OFF)
option(FPROFILE_USE "Compile executable using generated usage data" OFF)
option(NATIVE_BUILD "Target the build CPU" OFF)
option(STATIC_LINK_CRT "Statically link C runtime for release builds" OFF)
mark_as_advanced(FPROFILE_USE FPROFILE_GENERATE NATIVE_BUILD)

# X265_BUILD must be incremented each time the public API is changed
set(X265_BUILD 63)
configure_file("${PROJECT_SOURCE_DIR}/x265.def.in"
               "${PROJECT_BINARY_DIR}/x265.def")
configure_file("${PROJECT_SOURCE_DIR}/x265_config.h.in"
               "${PROJECT_BINARY_DIR}/x265_config.h")

SET(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" "${CMAKE_MODULE_PATH}")

# System architecture detection
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SYSPROC)
set(X86_ALIASES x86 i386 i686 x86_64 amd64)
list(FIND X86_ALIASES "${SYSPROC}" X86MATCH)
if("${SYSPROC}" STREQUAL "" OR X86MATCH GREATER "-1")
    message(STATUS "Detected x86 target processor")
    set(X86 1)
    add_definitions(-DX265_ARCH_X86=1)
    if("${CMAKE_SIZEOF_VOID_P}" MATCHES 8)
        set(X64 1)
        add_definitions(-DX86_64=1)
    endif()
elseif(${SYSPROC} STREQUAL "armv6l")
    message(STATUS "Detected ARM target processor")
    set(ARM 1)
    add_definitions(-DX265_ARCH_ARM=1 -DHAVE_ARMV6=1)
else()
    message(STATUS "CMAKE_SYSTEM_PROCESSOR value `${CMAKE_SYSTEM_PROCESSOR}` is unknown")
    message(STATUS "Please add this value near ${CMAKE_CURRENT_LIST_FILE}:${CMAKE_CURRENT_LIST_LINE}")
endif()

if(UNIX)
    list(APPEND PLATFORM_LIBS pthread)
    find_library(LIBRT rt)
    if(LIBRT)
        list(APPEND PLATFORM_LIBS rt)
    endif()
    find_library(LIBDL dl)
    if(LIBDL)
        list(APPEND PLATFORM_LIBS dl)
    endif()
    find_package(Numa)
    if(NUMA_FOUND)
        link_directories(${NUMA_LIBRARY_DIR})
        list(APPEND CMAKE_REQUIRED_LIBRARIES numa)
        check_symbol_exists(numa_node_of_cpu numa.h NUMA_V2)
        if(NUMA_V2)
            add_definitions(-DHAVE_LIBNUMA)
            message(STATUS "libnuma found, building with support for NUMA nodes")
            list(APPEND PLATFORM_LIBS numa)
            include_directories(${NUMA_INCLUDE_DIR})
        endif()
    endif()
    mark_as_advanced(LIBRT NUMA_FOUND)
endif(UNIX)

if(X64 AND NOT WIN32)
    option(ENABLE_PIC "Enable Position Independent Code" ON)
else()
    option(ENABLE_PIC "Enable Position Independent Code" OFF)
endif(X64 AND NOT WIN32)

# Compiler detection
if(CMAKE_GENERATOR STREQUAL "Xcode")
  set(XCODE 1)
endif()
if(APPLE)
  add_definitions(-DMACOS)
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    set(CLANG 1)
endif()
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Intel")
    set(INTEL_CXX 1)
endif()
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
    set(GCC 1)
endif()

if(INTEL_CXX AND WIN32)
    # treat icl roughly like MSVC
    set(MSVC 1)
endif()
if(MSVC)
    if(STATIC_LINK_CRT)
        set(CompilerFlags CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE)
        foreach(CompilerFlag ${CompilerFlags})
            string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
        endforeach()
    endif(STATIC_LINK_CRT)
    add_definitions(/W4)  # Full warnings
    add_definitions(/Ob2) # always inline
    add_definitions(/MP)  # multithreaded build

    # disable Microsofts suggestions for proprietary secure APIs
    add_definitions(/D_CRT_SECURE_NO_WARNINGS)

    check_include_files(stdint.h HAVE_STDINT_H)
    if(NOT HAVE_STDINT_H)
        include_directories(compat/msvc)
    endif()
endif(MSVC)

check_include_files(inttypes.h HAVE_INT_TYPES_H)
if(HAVE_INT_TYPES_H)
    add_definitions(-DHAVE_INT_TYPES_H=1)
endif()

if(INTEL_CXX AND UNIX)
    set(GCC 1) # treat icpc roughly like gcc
elseif(CLANG)
    set(GCC 1) # treat clang roughly like gcc
elseif(CMAKE_COMPILER_IS_GNUCXX)
    set(GCC 1)
endif()
if(GCC)
    add_definitions(-Wall -Wextra -Wshadow)
    add_definitions(-D__STDC_LIMIT_MACROS=1)
    if(ENABLE_PIC)
         add_definitions(-fPIC)
    endif(ENABLE_PIC)
    if(NATIVE_BUILD)
        if(INTEL_CXX)
            add_definitions(-xhost)
        else()
            add_definitions(-march=native)
        endif()
    elseif(X86 AND NOT X64)
        add_definitions(-march=i686)
    endif()
    if(ARM)
        add_definitions(-march=armv6 -mfloat-abi=hard -mfpu=vfp)
    endif()
    if(FPROFILE_GENERATE)
        if(INTEL_CXX)
            add_definitions(-prof-gen -prof-dir="${CMAKE_CURRENT_BINARY_DIR}")
            list(APPEND LINKER_OPTIONS "-prof-gen")
        else()
            check_cxx_compiler_flag(-fprofile-generate CC_HAS_PROFILE_GENERATE)
            if(CC_HAS_PROFILE_GENERATE)
                add_definitions(-fprofile-generate)
                list(APPEND LINKER_OPTIONS "-fprofile-generate")
            endif(CC_HAS_PROFILE_GENERATE)
        endif(INTEL_CXX)
    endif(FPROFILE_GENERATE)
    if(FPROFILE_USE)
        if(INTEL_CXX)
            add_definitions(-prof-use -prof-dir="${CMAKE_CURRENT_BINARY_DIR}")
            list(APPEND LINKER_OPTIONS "-prof-use")
        else()
            check_cxx_compiler_flag(-fprofile-use CC_HAS_PROFILE_USE)
            check_cxx_compiler_flag(-fprofile-correction CC_HAS_PROFILE_CORRECTION)
            check_cxx_compiler_flag(-Wno-error=coverage-mismatch CC_HAS_COVMISMATCH)
            if(CC_HAS_PROFILE_USE)
                add_definitions(-fprofile-use)
                list(APPEND LINKER_OPTIONS "-fprofile-use")
            endif(CC_HAS_PROFILE_USE)
            if(CC_HAS_PROFILE_CORRECTION)
                # auto-correct corrupted counters (happens a lot with x265)
                add_definitions(-fprofile-correction)
            endif(CC_HAS_PROFILE_CORRECTION)
            if(CC_HAS_COVMISMATCH)
                # ignore coverage mismatches (also happens a lot)
                add_definitions(-Wno-error=coverage-mismatch)
            endif(CC_HAS_COVMISMATCH)
        endif(INTEL_CXX)
    endif(FPROFILE_USE)
    if(STATIC_LINK_CRT)
        add_definitions(-static)
        list(APPEND LINKER_OPTIONS "-static")
    endif(STATIC_LINK_CRT)
    check_cxx_compiler_flag(-Wno-strict-overflow CC_HAS_NO_STRICT_OVERFLOW)
    check_cxx_compiler_flag(-Wno-narrowing CC_HAS_NO_NARROWING) 
    check_cxx_compiler_flag(-Wno-array-bounds CC_HAS_NO_ARRAY_BOUNDS) 
    if (CC_HAS_NO_ARRAY_BOUNDS)
        add_definitions(-Wno-array-bounds) # these are unhelpful
    endif()
    check_cxx_compiler_flag(-ffast-math CC_HAS_FAST_MATH) 
    if (CC_HAS_FAST_MATH)
        add_definitions(-ffast-math)
    endif()
    check_cxx_compiler_flag(-mstackrealign CC_HAS_STACK_REALIGN) 
    if (CC_HAS_STACK_REALIGN)
        add_definitions(-mstackrealign)
    endif()
    # Disable exceptions. Reduce executable size, increase compability.
    check_cxx_compiler_flag(-fno-exceptions CC_HAS_FNO_EXCEPTIONS_FLAG)
    if(CC_HAS_FNO_EXCEPTIONS_FLAG)
        add_definitions(-fno-exceptions)
    endif()
    set(FSANITIZE "" CACHE STRING "-fsanitize options for GCC/clang")
    if(FSANITIZE)
        add_definitions(-fsanitize=${FSANITIZE})
        # clang and gcc need the sanitize options to be passed at link
        # time so the appropriate ASAN/TSAN runtime libraries can be
        # linked.
        list(APPEND LINKER_OPTIONS "-fsanitize=${FSANITIZE}")
    endif()
    option(ENABLE_AGGRESSIVE_CHECKS "Enable stack protection and -ftrapv" OFF)
    if(ENABLE_AGGRESSIVE_CHECKS)
        # use with care, -ftrapv can cause testbench SIGILL exceptions
        # since it is testing corner cases of signed integer math
        add_definitions(-DUSING_FTRAPV=1)
        check_cxx_compiler_flag(-fsanitize=undefined-trap CC_HAS_CATCH_UNDEFINED) # clang
        check_cxx_compiler_flag(-ftrapv CC_HAS_FTRAPV)                            # gcc
        check_cxx_compiler_flag(-fstack-protector-all CC_HAS_STACK_PROTECT)       # gcc
        if(CC_HAS_FTRAPV)
            add_definitions(-ftrapv)
        endif()
        if(CC_HAS_CATCH_UNDEFINED)
            add_definitions(-fsanitize=undefined-trap -fsanitize-undefined-trap-on-error)
        endif()
        if(CC_HAS_STACK_PROTECT)
            add_definitions(-fstack-protector-all)
            if(MINGW)
                list(APPEND PLATFORM_LIBS ssp)
            endif()
        endif()
    endif(ENABLE_AGGRESSIVE_CHECKS)
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE CC_VERSION)
endif(GCC)

find_package(Yasm)
if(YASM_FOUND AND X86)
    if (YASM_VERSION_STRING VERSION_LESS "1.2.0")
        message(STATUS "Yasm version ${YASM_VERSION_STRING} is too old. 1.2.0 or later required")
        option(ENABLE_ASSEMBLY "Enable use of assembly coded primitives" OFF)
    else()
        message(STATUS "Found Yasm ${YASM_VERSION_STRING} to build assembly primitives")
        option(ENABLE_ASSEMBLY "Enable use of assembly coded primitives" ON)
    endif()
endif()

option(CHECKED_BUILD "Enable run-time sanity checks (debugging)" OFF)
if(CHECKED_BUILD)
    add_definitions(-DCHECKED_BUILD=1)
endif()

# Build options
set(LIB_INSTALL_DIR lib CACHE STRING "Install location of libraries")
set(BIN_INSTALL_DIR bin CACHE STRING "Install location of executables")
set(EXTRA_LIB "" CACHE STRING "Extra libraries to link against")
set(EXTRA_LINK_FLAGS "" CACHE STRING "Extra link flags")
mark_as_advanced(EXTRA_LIB EXTRA_LINK_FLAGS)

if(X64)
    # NOTE: We only officially support high-bit-depth compiles of x265
    # on 64bit architectures. Main10 plus large resolution plus slow
    # preset plus 32bit address space usually means malloc failure.  You
    # can disable this if(X64) check if you desparately need a 32bit
    # build with 10bit/12bit support, but this violates the "shrink wrap
    # license" so to speak.  If it breaks you get to keep both halves.
    # You will need to disable assembly manually.
    option(HIGH_BIT_DEPTH "Store pixel samples as 16bit values (Main10/Main12)" OFF)
endif(X64)
if(HIGH_BIT_DEPTH)
    option(MAIN12 "Support Main12 instead of Main10" OFF)
    if(MAIN12)
        add_definitions(-DHIGH_BIT_DEPTH=1 -DX265_DEPTH=12)
    else()
        add_definitions(-DHIGH_BIT_DEPTH=1 -DX265_DEPTH=10)
    endif()
else(HIGH_BIT_DEPTH)
    add_definitions(-DHIGH_BIT_DEPTH=0 -DX265_DEPTH=8)
endif(HIGH_BIT_DEPTH)

# this option can only be used when linking multiple libx265 libraries
# together, and some alternate API access method is implemented.
option(EXPORT_C_API "Implement public C programming interface" ON)
mark_as_advanced(EXPORT_C_API)
if(EXPORT_C_API)
    set(X265_NS x265)
    add_definitions(-DEXPORT_C_API=1)
elseif(HIGH_BIT_DEPTH)
    if(MAIN12)
        set(X265_NS x265_12bit)
    else()
        set(X265_NS x265_10bit)
    endif()
    add_definitions(-DEXPORT_C_API=0)
else()
    set(X265_NS x265_8bit)
    add_definitions(-DEXPORT_C_API=0)
endif()
add_definitions(-DX265_NS=${X265_NS})

option(WARNINGS_AS_ERRORS "Stop compiles on first warning" OFF)
if(WARNINGS_AS_ERRORS)
    if(GCC)
        add_definitions(-Werror)
    elseif(MSVC)
        add_definitions(/WX)
    endif()
endif(WARNINGS_AS_ERRORS)

if(WIN32)
    # Visual leak detector
    find_package(VLD QUIET)
    if(VLD_FOUND)
        add_definitions(-DHAVE_VLD)
        include_directories(${VLD_INCLUDE_DIRS})
        list(APPEND PLATFORM_LIBS ${VLD_LIBRARIES})
        link_directories(${VLD_LIBRARY_DIRS})
    endif()
    option(WINXP_SUPPORT "Make binaries compatible with Windows XP and Vista" OFF)
    if(WINXP_SUPPORT)
        # force use of workarounds for CONDITION_VARIABLE and atomic
        # intrinsics introduced after XP
        add_definitions(-D_WIN32_WINNT=_WIN32_WINNT_WINXP -D_WIN32_WINNT_WIN7=0x0601)
    else(WINXP_SUPPORT)
        # default to targeting Windows 7 for the NUMA APIs
        add_definitions(-D_WIN32_WINNT=_WIN32_WINNT_WIN7)
    endif(WINXP_SUPPORT)
endif()

include(version) # determine X265_VERSION and X265_LATEST_TAG
include_directories(. common encoder "${PROJECT_BINARY_DIR}")

option(ENABLE_PPA "Enable PPA profiling instrumentation" OFF)
if(ENABLE_PPA)
    add_definitions(-DENABLE_PPA)
    list(APPEND PLATFORM_LIBS PPA)
    if(UNIX)
        list(APPEND PLATFORM_LIBS dl)
    endif(UNIX)
    add_subdirectory(profile/PPA)
endif(ENABLE_PPA)

option(ENABLE_VTUNE "Enable Vtune profiling instrumentation" OFF)
if(ENABLE_VTUNE)
    add_definitions(-DENABLE_VTUNE)
    include_directories($ENV{VTUNE_AMPLIFIER_XE_2015_DIR}/include)
    list(APPEND PLATFORM_LIBS vtune)
    link_directories($ENV{VTUNE_AMPLIFIER_XE_2015_DIR}/lib64)
    if(WIN32)
        list(APPEND PLATFORM_LIBS libittnotify.lib)
    else()
        list(APPEND PLATFORM_LIBS libittnotify.a dl)
    endif()
    add_subdirectory(profile/vtune)
endif(ENABLE_VTUNE)

option(DETAILED_CU_STATS "Enable internal profiling of encoder work" OFF)
if(DETAILED_CU_STATS)
    add_definitions(-DDETAILED_CU_STATS)
endif(DETAILED_CU_STATS)

add_subdirectory(encoder)
add_subdirectory(common)

if((MSVC_IDE OR XCODE) AND ENABLE_ASSEMBLY)
    # this is required because of this cmake bug
    # http://www.cmake.org/Bug/print_bug_page.php?bug_id=8170
    if(WIN32)
        set(SUFFIX obj)
    else()
        set(SUFFIX o)
    endif()
    foreach(ASM ${MSVC_ASMS})
        set(YASM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/common/x86/${ASM})
        list(APPEND YASM_SRCS ${YASM_SRC})
        list(APPEND YASM_OBJS ${ASM}.${SUFFIX})
        add_custom_command(
            OUTPUT ${ASM}.${SUFFIX}
            COMMAND ${YASM_EXECUTABLE} ARGS ${YASM_FLAGS} ${YASM_SRC} -o ${ASM}.${SUFFIX}
            DEPENDS ${YASM_SRC})
    endforeach()
endif()

source_group(ASM FILES ${YASM_SRCS})
add_library(x265-static STATIC $<TARGET_OBJECTS:encoder> $<TARGET_OBJECTS:common> ${YASM_OBJS} ${YASM_SRCS})
if(NOT MSVC)
    set_target_properties(x265-static PROPERTIES OUTPUT_NAME x265)
endif()
if(EXTRA_LIB)
    target_link_libraries(x265-static ${EXTRA_LIB})
endif()
if(EXTRA_LINK_FLAGS)
    list(APPEND LINKER_OPTIONS ${EXTRA_LINK_FLAGS})
endif()
install(TARGETS x265-static
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    ARCHIVE DESTINATION ${LIB_INSTALL_DIR})
install(FILES x265.h "${PROJECT_BINARY_DIR}/x265_config.h" DESTINATION include)

if(CMAKE_RC_COMPILER)
    # The resource compiler does not need CFLAGS or macro defines. It
    # often breaks them
    string(REPLACE "<FLAGS>" "" CMAKE_RC_COMPILE_OBJECT "${CMAKE_RC_COMPILE_OBJECT}")
    string(REPLACE "<DEFINES>" "" CMAKE_RC_COMPILE_OBJECT "${CMAKE_RC_COMPILE_OBJECT}")

    # convert X265_LATEST_TAG (ex: 0.7) and X265_TAG_DISTANCE (ex: 103) to
    # @X265_VERSION_MAJOR@,@X265_VERSION_MINOR@,@X265_BRANCH_ID@,@X265_TAG_DISTANCE@
    string(REPLACE "." ";" VERSION_LIST "${X265_LATEST_TAG}")
    list(GET VERSION_LIST 0 X265_VERSION_MAJOR)
    list(GET VERSION_LIST 1 X265_VERSION_MINOR)
    set(X265_BRANCH_ID 0) # TODO: 0 - stable, 1 - default or other
    set(X265_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/x265.rc")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/x265.rc.in" "${X265_RC_FILE}" @ONLY)
endif()

if(NOT (MSVC_IDE OR XCODE))
    add_custom_target(clean-generated COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/clean-generated.cmake)
endif()

option(ENABLE_SHARED "Build shared library" ON)
if(ENABLE_SHARED)
    add_library(x265-shared SHARED "${PROJECT_BINARY_DIR}/x265.def" ${YASM_OBJS}
                ${X265_RC_FILE} $<TARGET_OBJECTS:encoder> $<TARGET_OBJECTS:common>)
    target_link_libraries(x265-shared ${PLATFORM_LIBS})
    if(MSVC)
        set_target_properties(x265-shared PROPERTIES OUTPUT_NAME libx265)
    else()
        set_target_properties(x265-shared PROPERTIES OUTPUT_NAME x265)
    endif()
    if(UNIX)
        set_target_properties(x265-shared PROPERTIES VERSION ${X265_BUILD})
        if(APPLE)
            set_target_properties(x265-shared PROPERTIES MACOSX_RPATH 1)
        else()
            set_target_properties(x265-shared PROPERTIES LINK_FLAGS "-Wl,-Bsymbolic,-znoexecstack")
        endif()
    endif()
    set_target_properties(x265-shared PROPERTIES SOVERSION ${X265_BUILD})
    if(X265_LATEST_TAG)
        if(WINDOWS)
            set_target_properties(x265-shared PROPERTIES VERSION ${X265_LATEST_TAG})
        endif()
        # shared library is not installed if a tag is not found
        install(TARGETS x265-shared
                LIBRARY DESTINATION ${LIB_INSTALL_DIR}
                ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
                RUNTIME DESTINATION ${BIN_INSTALL_DIR})
    endif()
    if(EXTRA_LIB)
        target_link_libraries(x265-shared ${EXTRA_LIB})
    endif()
    if(LINKER_OPTIONS)
        # set_target_properties can't do list expansion
        string(REPLACE ";" " " LINKER_OPTION_STR "${LINKER_OPTIONS}")
        set_target_properties(x265-shared PROPERTIES LINK_FLAGS "${LINKER_OPTION_STR}")
    endif()
endif()

if(X265_LATEST_TAG)
    # convert lists of link libraries into -lstdc++ -lm etc..
    foreach(LIB ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES} ${PLATFORM_LIBS})
        if(IS_ABSOLUTE ${LIB} AND EXISTS ${LIB})
            list(APPEND PLIBLIST "${LIB}")
        else()
            list(APPEND PLIBLIST "-l${LIB}")
        endif()
    endforeach()
    if(PLIBLIST)
        # blacklist of libraries that should not be in Libs.private
        list(REMOVE_ITEM PLIBLIST "-lc" "-lpthread")
        string(REPLACE ";" " " PRIVATE_LIBS "${PLIBLIST}")
    else()
        set(PRIVATE_LIBS "")
    endif(PLIBLIST)

    # Produce a pkg-config file
    configure_file("x265.pc.in" "x265.pc" @ONLY)
    install(FILES       "${CMAKE_CURRENT_BINARY_DIR}/x265.pc"
            DESTINATION "${LIB_INSTALL_DIR}/pkgconfig")
endif()

if(NOT WIN32)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/cmake/cmake_uninstall.cmake"
                   IMMEDIATE @ONLY)
    add_custom_target(uninstall
                      "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake/cmake_uninstall.cmake")
endif()

# Main CLI application
set(ENABLE_CLI ON CACHE BOOL "Build standalone CLI application")
if(ENABLE_CLI)
    file(GLOB InputFiles input/input.cpp input/yuv.cpp input/y4m.cpp input/*.h)
    file(GLOB OutputFiles output/output.cpp output/reconplay.cpp output/*.h
                          output/yuv.cpp output/y4m.cpp # recon
                          output/raw.cpp)               # muxers
    file(GLOB FilterFiles filters/*.cpp filters/*.h)
    source_group(input FILES ${InputFiles})
    source_group(output FILES ${OutputFiles})
    source_group(filters FILES ${FilterFiles})

    check_include_files(getopt.h HAVE_GETOPT_H)
    if(NOT HAVE_GETOPT_H)
        if(MSVC)
            set_source_files_properties(compat/getopt/getopt.c PROPERTIES COMPILE_FLAGS "/wd4100 /wd4131 -DHAVE_STRING_H=1")
        endif(MSVC)
        include_directories(compat/getopt)
        set(GETOPT compat/getopt/getopt.c compat/getopt/getopt.h)
    endif(NOT HAVE_GETOPT_H)
    if(MSVC)
        set(ExportDefs "${PROJECT_BINARY_DIR}/x265.def")
    endif(MSVC)

    if(XCODE)
        # Xcode seems unable to link the CLI with libs, so link as one targget
        add_executable(cli ../COPYING ${InputFiles} ${OutputFiles} ${FilterFiles} ${GETOPT} x265.cpp x265.h x265cli.h
                           $<TARGET_OBJECTS:encoder> $<TARGET_OBJECTS:common> ${YASM_OBJS} ${YASM_SRCS})
    else()
        add_executable(cli ../COPYING ${InputFiles} ${OutputFiles} ${FilterFiles} ${GETOPT} ${X265_RC_FILE}
                       ${ExportDefs} x265.cpp x265.h x265cli.h)
        if(WIN32 OR NOT ENABLE_SHARED OR INTEL_CXX)
            # The CLI cannot link to the shared library on Windows, it
            # requires internal APIs not exported from the DLL
            target_link_libraries(cli x265-static ${PLATFORM_LIBS})
        else()
            target_link_libraries(cli x265-shared ${PLATFORM_LIBS})
        endif()
    endif()
    set_target_properties(cli PROPERTIES OUTPUT_NAME x265)
    if(LINKER_OPTIONS)
        # set_target_properties can't do list expansion
        string(REPLACE ";" " " LINKER_OPTION_STR "${LINKER_OPTIONS}")
        set_target_properties(cli PROPERTIES LINK_FLAGS "${LINKER_OPTION_STR}")
    endif()

    install(TARGETS cli DESTINATION ${BIN_INSTALL_DIR})
endif(ENABLE_CLI)

if(ENABLE_ASSEMBLY AND NOT XCODE)
    option(ENABLE_TESTS "Enable Unit Tests" OFF)
    if(ENABLE_TESTS)
        add_subdirectory(test)
    endif()
endif()
