cmake_minimum_required(VERSION 3.16)
project(vesper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # POSIX and Linux headers
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  # snprintf into fixed buffers is deliberate: paths that long are skipped.
  add_compile_options(-Wno-format-truncation)
endif()

set(VESPER_TUNE "" CACHE STRING "CPU flags, e.g. -mcpu=cortex-a35")
if(VESPER_TUNE)
  separate_arguments(tune UNIX_COMMAND "${VESPER_TUNE}")
  add_compile_options(${tune})
endif()

# The parts that need no device: built and tested on any host.
add_library(vesper_common STATIC
  src/shelf.c src/resample.c src/layout.c src/image.c src/font.c)
target_include_directories(vesper_common PUBLIC src third_party/libretro)

option(VESPER_APP "Build the vesper program (Linux, needs libdrm, gbm, egl, glesv2, alsa)" ON)
if(VESPER_APP AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET libdrm gbm egl glesv2 alsa)
  find_package(Threads REQUIRED)
  add_executable(vesper
    src/main.c src/display.c src/gfx.c src/input.c src/audio.c src/core.c src/headless.c)
  target_link_libraries(vesper PRIVATE vesper_common PkgConfig::DEPS Threads::Threads ${CMAKE_DL_LIBS} m)
  install(TARGETS vesper RUNTIME DESTINATION bin)
endif()

option(VESPER_TESTS "Build the host tests" OFF)
if(VESPER_TESTS)
  enable_testing()
  add_executable(vesper_tests tests/tests.c)
  target_link_libraries(vesper_tests PRIVATE vesper_common)
  add_test(NAME vesper_tests COMMAND vesper_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
