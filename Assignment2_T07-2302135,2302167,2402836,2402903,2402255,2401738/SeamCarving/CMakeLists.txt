# =====================================================================================
#  CSD3130 / CSD3131 Assignment 2 - Seam Carving        (used by CI; build.bat on Windows)
#
#    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#    cmake --build build --config Release
#    ctest --test-dir build -C Release --output-on-failure
# =====================================================================================
cmake_minimum_required(VERSION 3.16)
project(SeamCarving CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenMP)

add_library(seamcore STATIC
  src/image.cpp src/energy.cpp src/convolution.cpp src/seam.cpp
  src/seam_dp.cpp src/seam_greedy.cpp src/seam_graphcut.cpp
  src/carver.cpp src/visualize.cpp)
target_include_directories(seamcore PUBLIC src)
if(MSVC)
  target_compile_options(seamcore PUBLIC /W4 /permissive- /utf-8 /EHsc)
else()
  target_compile_options(seamcore PUBLIC -Wall -Wextra)
endif()
if(OpenMP_CXX_FOUND)
  target_link_libraries(seamcore PUBLIC OpenMP::OpenMP_CXX)
endif()
if(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(seamcore PUBLIC Threads::Threads)
endif()

add_executable(seamcarve       apps/seamcarve_cli.cpp)
add_executable(seamcarve_tests tests/tests.cpp)
add_executable(seamcarve_bench apps/benchmark.cpp)
target_link_libraries(seamcarve       PRIVATE seamcore)
target_link_libraries(seamcarve_tests PRIVATE seamcore)
target_link_libraries(seamcarve_bench PRIVATE seamcore)

if(WIN32)
  add_executable(seamcarve_gui WIN32 apps/seamcarve_gui.cpp)
  target_link_libraries(seamcarve_gui PRIVATE seamcore user32 gdi32 comdlg32 comctl32)
endif()

enable_testing()
add_test(NAME correctness COMMAND seamcarve_tests)
add_test(NAME cli_smoke   COMMAND seamcarve --demo ${CMAKE_BINARY_DIR}/demo_out --quick)
