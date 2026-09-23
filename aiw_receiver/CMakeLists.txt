cmake_minimum_required(VERSION 3.20)
project(AiwReceiver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(AIW_NATIVE     "Compile with -march=native"                 ON)
option(AIW_FAST_MATH  "Compile with -ffast-math"                   ON)
option(AIW_REQUIRE_UHD "Fail if UHD is not found (hardware build)" OFF)
option(AIW_BUILD_TESTS "Build the offline verification tests"      ON)

if(MSVC)
    # Windows + UHD route: Ettus ships UHD for Windows built with MSVC.
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
    add_compile_options(/W3 /permissive- /Zc:__cplusplus /EHsc)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
    if(AIW_NATIVE)
        add_compile_options(/arch:AVX2)
    endif()
    if(AIW_FAST_MATH)
        add_compile_options(/fp:fast)
    endif()
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    add_compile_options(-Wall -Wextra)
    if(AIW_NATIVE)
        add_compile_options(-march=native)
    endif()
    if(AIW_FAST_MATH)
        add_compile_options(-ffast-math)
    endif()
endif()
if(MINGW)
    # Self-contained .exe: no libstdc++/libgcc/winpthread DLLs to ship.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(Threads REQUIRED)
if(AIW_REQUIRE_UHD)
    find_package(UHD REQUIRED)
else()
    find_package(UHD QUIET)
endif()

# Hardware-independent DSP core, shared by the receiver and the tests.
add_library(aiw_core STATIC
    src/equalizer.cpp
    src/fec_reed_solomon.cpp
    src/pfb_clock_sync.cpp
    src/receiver.cpp
    src/sync_correlator.cpp
    src/tx_simulator.cpp
)
target_include_directories(aiw_core PUBLIC include)
target_link_libraries(aiw_core PUBLIC Eigen3::Eigen Threads::Threads)

add_executable(aiw_rx src/main.cpp)
target_link_libraries(aiw_rx PRIVATE aiw_core)
if(UHD_FOUND)
    message(STATUS "UHD found: building with USRP support")
    target_compile_definitions(aiw_rx PRIVATE AIW_HAVE_UHD)
    target_include_directories(aiw_rx PRIVATE ${UHD_INCLUDE_DIRS})
    target_link_libraries(aiw_rx PRIVATE ${UHD_LIBRARIES})
else()
    message(WARNING "UHD not found: aiw_rx will support --source file|sim only")
endif()

if(AIW_BUILD_TESTS)
    enable_testing()
    add_executable(aiw_tests tests/test_main.cpp)
    target_link_libraries(aiw_tests PRIVATE aiw_core)
    add_test(NAME unit_and_loopback COMMAND aiw_tests)
    add_test(NAME rx_sim_end_to_end
             COMMAND aiw_rx --source sim --sim-seconds 3 --quiet --expect-ber0)
endif()
