cmake_minimum_required(VERSION 3.16)
project(lowfat_mcu C)

option(LOWFAT_HOST_TEST_BUILD "Build desktop pthread tests" OFF)

add_library(lowfat_mcu STATIC
  lowfat_mcu.c
  lowfat_extra_mcu.c
  lowfat_fallback_mcu.c
  lowfat_config_stm32.c
)
target_include_directories(lowfat_mcu PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(LOWFAT_HOST_TEST_BUILD)
  find_package(Threads REQUIRED)
  target_compile_definitions(lowfat_mcu PRIVATE LOWFAT_HOST_TEST=1)
  target_link_libraries(lowfat_mcu PUBLIC Threads::Threads)
  add_executable(lowfat_host_tests host_main.c lowfat_tests.c)
  target_compile_definitions(lowfat_host_tests PRIVATE LOWFAT_HOST_TEST=1)
  target_link_libraries(lowfat_host_tests PRIVATE lowfat_mcu Threads::Threads)
endif()

option(LOWFAT_WITH_MICROROS "Build the rcutils allocator adapter" OFF)
if(LOWFAT_WITH_MICROROS)
  target_sources(lowfat_mcu PRIVATE microros_lowfat_allocator.c)
endif()

# The containing STM32 project must provide FreeRTOS headers and link this
# library after the kernel. For a desktop-only test, define LOWFAT_HOST_TEST
# and link pthread. Enable LOWFAT_WITH_MICROROS only where rcutils headers are
# already present.
