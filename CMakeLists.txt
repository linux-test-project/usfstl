cmake_minimum_required(VERSION 3.15)

project(usfstl C ASM)

add_library(usfstl STATIC "")

separate_arguments(USFSTL_CC_OPT)
target_compile_options(usfstl PRIVATE
    ${USFSTL_CC_OPT}
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-format-zero-length
    -Werror
    -D_FILE_OFFSET_BITS=64 # Necessary to write large (>2GiB) log files
    -D_GNU_SOURCE=1

    # TODO: the following defines are really only needed for the files under src/dwarf
    -DHAVE_ATOMIC_FUNCTIONS
    -DHAVE_DECL_STRNLEN=1
    -DHAVE_DL_ITERATE_PHDR=1
    -DHAVE_SYNC_FUNCTIONS
)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # usfstl actually uses this gnu extension, so don't warn on it in clang
    target_compile_options(usfstl PRIVATE -Wno-gnu-variable-sized-type-not-at-end)
endif()

if(USFSTL_SKIP_ASAN_STR)
    target_compile_options(usfstl PRIVATE -DUSFSTL_WANT_NO_ASAN_STRING=1)
endif()

if(USFSTL_FUZZ)
    target_compile_options(usfstl PRIVATE -DUSFSTL_USE_FUZZING -DUSFSTL_FUZZER_${USFSTL_FUZZ})
endif()

if(MINGW OR CYGWIN)
    set(windows ON)
elseif(NOT UNIX)
    message(ERROR "usfstl only support Unix, MinGW, and CygWin")
endif()

add_subdirectory(include)
add_subdirectory(src)

target_include_directories(usfstl PRIVATE include)
