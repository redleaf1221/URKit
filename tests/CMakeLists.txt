# Host-side unit tests. Every registered case becomes its own CTest test, so
# `ctest`, `ctest -R <pattern>`, `ctest -j` and the IDE all see the same list and
# report a failure against a case name instead of one aggregate "urk_tests".
add_executable(urk_tests
    test_main.cpp
    test_abi.cpp
    test_cursor_leases.cpp
    test_export_policy.cpp
    test_math.cpp
    test_name_matching.cpp
    test_scaffold.cpp
)
target_compile_features(urk_tests PRIVATE cxx_std_20)
target_include_directories(urk_tests PRIVATE
    ${URK_ROOT_DIR}/src
    ${URK_ROOT_DIR}/src/loader
    ${URK_ROOT_DIR}/src/tools/urk
    ${URK_SRC_DIR}/core/loader
    ${URK_SDK_DIR}
)
target_compile_definitions(urk_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if (MSVC)
    target_compile_options(urk_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

# -fprofile-instr-generate writes one .profraw per process; -fcoverage-mapping
# embeds the counters-to-source map in the binary. `llvm-profdata merge` then
# folds the runs and `llvm-cov` (or the IDE) reads the result. Both flags are
# needed on the link line as well, for the profile runtime.
if (URK_TEST_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(urk_tests PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(urk_tests PRIVATE -fprofile-instr-generate)
    # %p keeps parallel cases from writing the same profile file.
    set(URK_TEST_PROFILE_RAW "${CMAKE_CURRENT_BINARY_DIR}/urk_tests-%p.profraw")
elseif (URK_TEST_COVERAGE)
    message(WARNING "URK_TEST_COVERAGE needs Clang; ${CMAKE_CXX_COMPILER_ID} builds urk_tests uninstrumented.")
endif()

# The case list is compiled into the binary, so it cannot be read at configure
# time. The registration file is emptied here (CTest must find it on a fresh
# configure) and rewritten after every build of urk_tests.
set(URK_TEST_REGISTRATIONS "${CMAKE_CURRENT_BINARY_DIR}/urk_tests.cmake")
file(WRITE "${URK_TEST_REGISTRATIONS}" "# Replaced after every build of urk_tests.\n")
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${URK_TEST_REGISTRATIONS}")
add_custom_command(TARGET urk_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DURK_TEST_EXECUTABLE=$<TARGET_FILE:urk_tests>
        -DURK_TEST_REGISTRATIONS=${URK_TEST_REGISTRATIONS}
        -DURK_TEST_PROFILE_RAW=${URK_TEST_PROFILE_RAW}
        -P ${URK_ROOT_DIR}/cmake/DiscoverUrkTests.cmake
    COMMENT "Registering urk_tests cases with CTest"
    VERBATIM
)
urk_disable_vs_vcpkg_integration(urk_tests)
