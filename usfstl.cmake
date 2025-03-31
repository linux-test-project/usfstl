cmake_minimum_required(VERSION 3.15)

project(usfstl_test C)

include(ExternalProject)

if(MINGW OR CYGWIN OR WIN32)
    set(_usfstl_windows ON)
endif()

set(USFSTL_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})

add_custom_target(tested)
add_custom_target(support)
add_custom_target(build
    DEPENDS tested support
)

# provide
macro(usfstl_configure_framework)
    set(options SCHED_CTRL SKIP_ASAN_STR VHOST_USER)
    set(oneValueArgs
        BIN_PATH
        CC_OPT
        LINK_OPT
        LOGDIR
        CONTEXT_BACKEND
        FUZZING
        FUZZER
        POSTFIX
    )
    set(multiValueArgs "")
    cmake_parse_arguments(USFSTL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

    if(USFSTL_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "usfstl_configure_framework: Invalid arguments: ${USFSTL_UNPARSED_ARGUMENTS}")
    endif()

    string(APPEND USFSTL_CC_OPT " -DUSFSTL_USE_ASSERT_PROFILING=1 -g -gdwarf-2 -mno-ms-bitfields")
    # -m32, -mfentry and -fpic aren't compatible, so if we have -m32 add -fno-pic
    if("${USFSTL_CC_OPT}" MATCHES "-m32")
        string(APPEND USFSTL_CC_OPT " -fno-pic")
        set(_global_pack LL)
    else()
        set(_global_pack QQ)
    endif()

    set(USFSTL_TEST_SECTION text_test)

    string(APPEND USFSTL_LINK_OPT " ${USFSTL_CC_OPT}")

    if(USFSTL_SKIP_ASAN_STR)
        string(APPEND USFSTL_CC_OPT " -DUSFSTL_WANT_NO_ASAN_STRING=1")
        string(APPEND USFSTL_LINK_OPT " -ldl")
    endif()

    if(NOT USFSTL_CONTEXT_BACKEND)
        if(_usfstl_windows)
            set(USFSTL_CONTEXT_BACKEND pthread)
        else()
            set(USFSTL_CONTEXT_BACKEND ucontext)
        endif()
    endif()

    set(USFSTL_FRAMEWORK_TARGET "usfstl${USFSTL_POSTFIX}")

    if("${USFSTL_CONTEXT_BACKEND}" STREQUAL "pthread")
        string(APPEND USFSTL_LINK_OPT " -pthread")
    endif()

    if("${USFSTL_FUZZING}"  STREQUAL "1")
        if(NOT USFSTL_FUZZER)
            set(USFSTL_FUZZER afl-gcc)
        endif()

        if("${USFSTL_FUZZER}" STREQUAL "afl-gcc")
            set(_usfstl_fuzzer AFL_GCC)
            set(CMAKE_C_COMPILER afl-gcc)
            set(CMAKE_C_COMPILER_ID "GNU")
            set(ENV{AFL_SKIP_BIN_CHECK} 1)
        elseif("${USFSTL_FUZZER}" STREQUAL "afl-clang-fast")
            set(_usfstl_fuzzer AFL_CLANG_FAST)
            set(CMAKE_C_COMPILER afl-clang-fast)
            set(CMAKE_C_COMPILER_ID "Clang")
        elseif("${USFSTL_FUZZER}" STREQUAL "libfuzzer")
            set(_usfstl_fuzzer LIB_FUZZER)
            set(CMAKE_C_COMPILER clang-9)
            set(CMAKE_C_COMPILER_ID "Clang")
        elseif("${USFSTL_FUZZER}" STREQUAL "afl-gcc-fast")
            set(_usfstl_fuzzer AFL_GCC_FAST)
            set(CMAKE_C_COMPILER afl-gcc-fast)
            set(CMAKE_C_COMPILER_ID "GNU")
        else()
            message(FATAL_ERROR "fuzzer value is unexpected ${USFSTL_FUZZER}")
        endif()
    elseif("${USFSTL_FUZZING}" STREQUAL "repro")
        set(_usfstl_fuzzer REPRO)
    elseif("${USFSTL_FUZZING}")
        message(FATAL_ERROR "The framework's 'FUZZING' argument must be either '1' or 'repro'")
    endif()

    ExternalProject_Add(${USFSTL_FRAMEWORK_TARGET}-build
        PREFIX ${USFSTL_FRAMEWORK_TARGET}
        CMAKE_GENERATOR Ninja
        SOURCE_DIR ${USFSTL_BASE_DIR}
        BINARY_DIR ${USFSTL_BIN_PATH}/${USFSTL_FRAMEWORK_TARGET}
        TMP_DIR ${USFSTL_LOGDIR}/tmp
        STAMP_DIR ${USFSTL_LOGDIR}/stamps
        LOG_DIR ${USFSTL_LOGDIR}
        LOG_BUILD TRUE
        CMAKE_CACHE_ARGS
            -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
            -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
            -DUSFSTL_CC_OPT:STRING=${USFSTL_CC_OPT}
            -DUSFSTL_CONTEXT_BACKEND:STRING=${USFSTL_CONTEXT_BACKEND}
            -DUSFSTL_FUZZ:STRING=${_usfstl_fuzzer}
            -DUSFSTL_SCHED_CTRL:BOOL=${USFSTL_SCHED_CTRL}
            -DUSFSTL_SKIP_ASAN_STR:BOOL=${USFSTL_SKIP_ASAN_STR}
            -DUSFSTL_VHOST_USER:BOOL=${USFSTL_VHOST_USER}
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
        USES_TERMINAL TRUE
        BUILD_BYPRODUCTS ${USFSTL_BIN_PATH}/${USFSTL_FRAMEWORK_TARGET}/libusfstl.a
    )

    add_library(${USFSTL_FRAMEWORK_TARGET} STATIC IMPORTED)
    set_target_properties(${USFSTL_FRAMEWORK_TARGET} PROPERTIES IMPORTED_LOCATION ${USFSTL_BIN_PATH}/${USFSTL_FRAMEWORK_TARGET}/libusfstl.a)
    add_dependencies(${USFSTL_FRAMEWORK_TARGET} ${USFSTL_FRAMEWORK_TARGET}-build)
    add_dependencies(build ${USFSTL_FRAMEWORK_TARGET})
endmacro()

# The name of the tested target will be "tested-<NAME>" where name is the value of the optional NAME argument
function(usfstl_add_tested)
    set(options)
    set(requiredOneValArgs
        TESTED_SRC_DIR
        TESTED_LIB_NAME
    )
    set(optionalOneValArgs
        NAME
    )
    set(oneValArgs ${requiredOneValArgs} ${optionalOneValArgs})

    set(requiredMultiValArgs
        TESTED_BUILD_COMMAND
    )
    set(optionalMultiValArgs
        TESTED_CMAKE_CACHE_ARGS
    )
    set(multiValArgs ${requiredMultiValArgs} ${optionalMultiValArgs})
    cmake_parse_arguments(usfstl_add_tested
        "${options}"
        "${oneValArgs}"
        "${multiValArgs}" ${ARGN}
    )

    # input validation
    if(usfstl_add_tested_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "usfstl_add_tested: Unexpected arguments - ${usfstl_add_tested_UNPARSED_ARGUMENTS}")
    endif()

    foreach(arg IN LISTS requiredOneValArgs requiredMultiValArgs)
        if(NOT usfstl_add_tested_${arg})
            message(FATAL_ERROR "usfstl_add_tested': missing required argument ${arg}")
        endif()
    endforeach()

    set(name "tested-${usfstl_add_tested_NAME}")

    if(usfstl_add_tested_TESTED_CMAKE_CACHE_ARGS)
        set(tested_cmake_cache_args CMAKE_CACHE_ARGS ${usfstl_add_tested_TESTED_CMAKE_CACHE_ARGS})
    endif()

    # Add a project that builds the tested
    ExternalProject_Add(${name}-build
        PREFIX tested-${name}
        CMAKE_GENERATOR Ninja
        SOURCE_DIR ${usfstl_add_tested_TESTED_SRC_DIR}
        BINARY_DIR ${USFSTL_BIN_PATH}/${name}
        BUILD_COMMAND ${usfstl_add_tested_TESTED_BUILD_COMMAND}
        TMP_DIR ${USFSTL_LOGDIR}/tmp
        STAMP_DIR ${USFSTL_LOGDIR}/stamps
        LOG_DIR ${USFSTL_LOGDIR}
        LOG_BUILD TRUE
        ${tested_cmake_cache_args}
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
        USES_TERMINAL 1
        BUILD_BYPRODUCTS ${USFSTL_BIN_PATH}/${name}/${usfstl_add_tested_TESTED_LIB_NAME}
        CMAKE_CACHE_ARGS
            -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
            -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
    )

    # add the tested library (links the previous build target)
    add_library(${name} STATIC IMPORTED)
    set_target_properties(${name}
        PROPERTIES
        IMPORTED_LOCATION ${USFSTL_BIN_PATH}/${name}/${usfstl_add_tested_TESTED_LIB_NAME})
    add_dependencies(${name} ${name}-build)

    # finally, add this tested target as a dependency of the general "tested" target
    add_dependencies(tested ${name})
endfunction()

function(usfstl_add_support)
    set(options)
    set(requiredOneValArgs
        SUPPORT_SRC_DIR
        SUPPORT_LIB_NAME
    )
    set(optionalOneValArgs
        NAME
    )
    set(oneValArgs ${requiredOneValArgs} ${optionalOneValArgs})

    set(requiredMultiValArgs
        SUPPORT_BUILD_COMMAND
    )
    set(optionalMultiValArgs
        SUPPORT_CMAKE_CACHE_ARGS
    )
    set(multiValArgs ${requiredMultiValArgs} ${optionalMultiValArgs})
    cmake_parse_arguments(usfstl_add_support
        "${options}"
        "${oneValArgs}"
        "${multiValArgs}" ${ARGN}
    )

    # input validation
    if(usfstl_add_support_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "usfstl_add_support: Unexpected arguments - ${usfstl_add_support_UNPARSED_ARGUMENTS}")
    endif()

    foreach(arg IN LISTS requiredOneValArgs requiredMultiValArgs)
        if(NOT usfstl_add_support_${arg})
            message(FATAL_ERROR "usfstl_add_support: missing required argument ${arg}")
        endif()
    endforeach()

    set(name ${usfstl_add_support_NAME})
    if(NOT TARGET tested-${name})
        message(FATAL_ERROR "usfstl_add_support: a tested target with the same NAME (${name}) must be added before its support")
    endif()

    set(support_name "support-${name}")

    # Add support project
    ExternalProject_Add(${support_name}-build
        PREFIX ${support_name}
        CMAKE_GENERATOR Ninja
        SOURCE_DIR ${usfstl_add_support_SUPPORT_SRC_DIR}
        BINARY_DIR ${USFSTL_BIN_PATH}/${support_name}
        BUILD_COMMAND ${usfstl_add_support_SUPPORT_BUILD_COMMAND}
        TMP_DIR ${USFSTL_LOGDIR}/tmp
        STAMP_DIR ${USFSTL_LOGDIR}/stamps
        LOG_DIR ${USFSTL_LOGDIR}
        LOG_BUILD TRUE
        CMAKE_CACHE_ARGS
            -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
            -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
            ${usfstl_add_support_SUPPORT_CMAKE_CACHE_ARGS}
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
        BUILD_BYPRODUCTS ${USFSTL_BIN_PATH}/${support_name}/${usfstl_add_support_SUPPORT_LIB_NAME}
        USES_TERMINAL 1
        DEPENDS tested-${name}-build
    )
    add_library(${support_name} STATIC IMPORTED)
    set_target_properties(${support_name}
        PROPERTIES
        IMPORTED_LOCATION ${USFSTL_BIN_PATH}/${support_name}/${usfstl_add_support_SUPPORT_LIB_NAME})
    add_dependencies(${support_name} ${support_name}-build)
    add_dependencies(support ${support_name})
endfunction()

function(usfstl_add_test)
    set(options)
    set(requiredOneValArgs
        TEST_NAME
    )
    set(optionalOneValArgs
        NAME
        TEST_CC_OPT
    )
    set(oneValArgs ${requiredOneValArgs} ${optionalOneValArgs})

    set(requiredMultiValArgs
    )
    set(optionalMultiValArgs
        TEST_SOURCES
    )
    set(multiValArgs ${requiredMultiValArgs} ${optionalMultiValArgs})
    cmake_parse_arguments(usfstl_add_test
        "${options}"
        "${oneValArgs}"
        "${multiValArgs}" ${ARGN}
    )

    # input validation
    if(usfstl_add_test_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "usfstl_add_test: Unexpected arguments - ${usfstl_add_test_UNPARSED_ARGUMENTS}")
    endif()

    foreach(arg IN LISTS requiredOneValArgs requiredMultiValArgs)
        if(NOT usfstl_add_test_${arg})
            message(FATAL_ERROR "usfstl_add_test': missing required argument ${arg}")
        endif()
    endforeach()

    set(name ${usfstl_add_test_NAME})
    if(NOT TARGET support-${name})
        message(FATAL_ERROR "usfstl_add_test: a support target with the same NAME (${name}) must be added before the test")
    endif()

    # create the test target (executable)
    set(test_target ${name}-${usfstl_add_test_TEST_NAME})
    add_executable(${test_target}
        ${usfstl_add_test_TEST_SOURCES}
    )
    set_target_properties(${test_target}
        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${name}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${name}"
        OUTPUT_NAME "${usfstl_add_test_TEST_NAME}"
    )

    target_include_directories(${test_target} PRIVATE
        ${USFSTL_BASE_DIR}/include/
    )
    if(NOT usfstl_add_test_TEST_CC_OPT)
        set(usfstl_add_test_TEST_CC_OPT ${USFSTL_CC_OPT})
    endif()


    set(cc_opts ${usfstl_add_test_TEST_CC_OPT} -MMD -MP -pg -mfentry)
    separate_arguments(cc_opts)
    target_compile_options(${test_target} PRIVATE ${cc_opts})

    add_custom_command(TARGET ${test_target}
        PRE_LINK
        COMMAND bash -c "_objs=\"$<TARGET_OBJECTS:${test_target}>\" objs=\"\${_objs//\;/ }\" ; for obj in $objs ; do objcopy --rename-section=.text=${USFSTL_TEST_SECTION} $obj ; done"
        VERBATIM
    )

    set(link_opts ${USFSTL_LINK_OPT})
    separate_arguments(link_opts)
    target_link_options(${test_target} PRIVATE ${cc_opts} ${link_opts})
    target_link_libraries(${test_target}
        ${USFSTL_FRAMEWORK_TARGET}
        tested-${name}
        support-${name}
        tested-${name}
        support-${name}
    )
    if(_usfstl_windows)
        target_link_libraries(${test_target} ws2_32)
    endif()

    set(globals_file ${CMAKE_BINARY_DIR}/${name}/${usfstl_add_test_TEST_NAME}${CMAKE_EXECUTABLE_SUFFIX}.globals)
    set(_gcov_tls "__(llvm_)?gcov|_*emutls")
    set(_sanitizers ".*_(l|a|ub)san")
    set(_sections "\\.bss|\\.data|___|__end__")
    set(_mangled_names "_Z.*(GlobCopy|pglob_copy|scandir|qsort|preinit|ubsan|ioctl|CurrentUBR|(S|s)uppression|asan_after_init_array|VptrCheck|(i|I)nterceptor|printf|xdrrec|MlockIsUnsupportedvE7printed)")
    set(_san_symbols "_Z.*__(sanitizer|interception|sancov)")
    set(_internal "replaced_headers|__usfstl_assert_info|usfstl_tested_files|__unnamed")
    set(_ignore_variables " . (${_gcov_tls}|${_sanitizers}|${_sections}|${_mangled_names}|${_san_symbols}|${_internal})")

    add_custom_command(
        OUTPUT ${globals_file}
        COMMAND nm -S --size-sort ${CMAKE_BINARY_DIR}/${name}/${usfstl_add_test_TEST_NAME} | sort | grep -E -v ${_ignore_variables} | perl -ne "binmode(stdout); m/^([0-9a-f]*) ([0-9a-f]*) [dDbB] .*/ && print pack(\"${_global_pack}\",hex($1), hex($2))" > ${globals_file}
        VERBATIM
        DEPENDS ${test_target}
    )
    add_custom_target(${test_target}-globals ALL DEPENDS ${globals_file})
    add_dependencies(build ${test_target}-globals)
endfunction()

function(usfstl_add_test_sources)
    set(options)
    set(requiredOneValArgs
        TEST_NAME
        PROJECT_NAME
    )
    set(optionalOneValArgs
        NAME
    )
    set(oneValArgs ${requiredOneValArgs} ${optionalOneValArgs})

    set(requiredMultiValArgs
        TEST_SOURCES
    )
    set(optionalMultiValArgs
        TESTED_FILES
        TEST_INCS
    )
    set(multiValArgs ${requiredMultiValArgs} ${optionalMultiValArgs})
    cmake_parse_arguments(usfstl_add_test_sources
        "${options}"
        "${oneValArgs}"
        "${multiValArgs}" ${ARGN}
    )

    # input validation
    if(usfstl_add_test_sources_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "usfstl_add_test_sources: Unexpected arguments - ${usfstl_add_test_sources_UNPARSED_ARGUMENTS}")
    endif()

    foreach(arg IN LISTS requiredOneValArgs requiredMultiValArgs)
        if(NOT usfstl_add_test_sources_${arg})
            message(FATAL_ERROR "usfstl_add_test_sources': missing required argument ${arg}")
        endif()
    endforeach()

    set(name ${usfstl_add_test_sources_NAME})
    set(test_name ${name}-${usfstl_add_test_sources_TEST_NAME})
    if(NOT TARGET ${test_name})
        message(FATAL_ERROR "usfstl_add_test_sources: cannot add sources to non-existant test target '${test_name}'")
    endif()

    target_sources(${test_name} PRIVATE
        ${usfstl_add_test_sources_TEST_SOURCES}
    )

    string(REPLACE " " "\", \"" tested_files ${usfstl_add_test_sources_TESTED_FILES})
    string(PREPEND tested_files "\"")
    string(APPEND tested_files "\",")
    set_property(SOURCE ${usfstl_add_test_sources_TEST_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS "USFSTL_TESTED_FILES=${tested_files}")
    set_property(SOURCE ${usfstl_add_test_sources_TEST_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS "USFSTL_TEST_NAME=${usfstl_add_test_sources_PROJECT_NAME}")

    set_property(SOURCE ${usfstl_add_test_sources_TEST_SOURCES} APPEND PROPERTY INCLUDE_DIRECTORIES "${usfstl_add_test_sources_TEST_INCS}")
endfunction()
