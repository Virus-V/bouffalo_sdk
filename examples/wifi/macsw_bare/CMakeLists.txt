cmake_minimum_required(VERSION 3.15)


find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# User
sdk_add_compile_definitions(-DCONFIG_CLI_CMD_ENABLE)

sdk_add_include_directories(.)

sdk_set_main_file(main.c)

# Add sensor driver source files
target_sources(app PRIVATE sensor_i2c.c)
target_sources(app PRIVATE aht20.c)
target_sources(app PRIVATE bmp280.c)

project(macsw_test)
