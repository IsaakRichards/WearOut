cmake_minimum_required(VERSION 3.10)
project(WearOut VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WEAROUT_BUILD_TESTS "Build unit tests" ON)
option(WEAROUT_BUILD_CLI "Build the desktop CLI (TCP / emulator)" ON)
option(WEAROUT_BUILD_SAILFISH "Build the Sailfish OS daemon and UI (needs Qt5 + sailfishapp)" OFF)

add_library(wearout_core STATIC
    core/src/protobuf.cpp
    core/src/sha1.cpp
    core/src/messages.cpp
    core/src/framing.cpp
    core/src/session.cpp
)
target_include_directories(wearout_core PUBLIC core/include)
set_target_properties(wearout_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wearout_core PRIVATE -Wall -Wextra)
endif()

if(WEAROUT_BUILD_CLI)
    add_executable(wearout-cli tools/cli/main.cpp)
    target_link_libraries(wearout-cli PRIVATE wearout_core)
endif()

if(WEAROUT_BUILD_TESTS)
    enable_testing()
    add_executable(wearout_tests tests/test_core.cpp)
    target_link_libraries(wearout_tests PRIVATE wearout_core)
    add_test(NAME core COMMAND wearout_tests)
endif()

if(WEAROUT_BUILD_SAILFISH)
    add_subdirectory(sailfish)
endif()
