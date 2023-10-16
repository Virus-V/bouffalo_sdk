cmake_minimum_required(VERSION 3.15)

include(proj.conf)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

file(GLOB_RECURSE sources "${CMAKE_CURRENT_SOURCE_DIR}/libfdt/*.c")

target_sources(app PRIVATE
  rv32i_xtheade_lz4.S md5.c
  ${sources}
)

sdk_set_main_file(main.c)

sdk_set_linker_script(${CMAKE_CURRENT_SOURCE_DIR}/bl808_flash_d0.ld)

sdk_add_include_directories(./libfdt)

project(loader_d0)
