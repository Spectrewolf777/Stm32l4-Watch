cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME WatchWatchWatch)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

file(GLOB_RECURSE TINYUSB_SOURCES 
    "tinyusb/*.c"
    "tinyusb/portable/synopsys/dwc2/*.c"
)

file(GLOB_RECURSE LVGL_SOURCES
    "Drivers/lvgl/lvgl/src/*.c"
)




file(GLOB_RECURSE UI_SOURCES
    "Drivers/lvgl/UI/*.c"
)


# LVGL at -O0 is several times slower and makes animations choppy / tear more.
# Compile the graphics library and generated UI with -O2 even in Debug builds;
# your own code keeps the Debug -O0 so it is still easy to step through.
set_source_files_properties(${LVGL_SOURCES} ${UI_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Drivers/GUI_DEV/GUI_Paint.c  
    Drivers/Config/DEV_Config.c
    
   Drivers/example/LCD_1inch28_test.c  
   Drivers/LCD/LCD_1inch28.c          
   Drivers/Fonts/font16.c
   Drivers/Fonts/font20.c
   Drivers/Fonts/font24.c
   Drivers/LCD/Touch_Driver.c
   Core/Src/usb_descriptors.c
   Core/Src/msc_disk.c
   Core/Src/board.c
   Core/Src/rv_3028_c7.c
   Core/Src/GC9A01.c
   Core/Src/lv_port_disp.c
   Core/Src/serial_cmd.c
   Core/Src/lsm6dsv16x_stepcounter.c
   Core/Src/lsm6dsv16x_reg.c
   Core/Src/wrist_wake.c
   Core/Src/cst816t.c
   Core/Src/lv_port_indev.c
   Core/Src/i2c_dev.c
   Core/Src/max17048.c
   Core/Src/hdc2010.c
   Core/Src/bmp388.c
   Core/Src/lis3mdl.c
   Core/Src/drv2605l.c
   Core/Src/m24512.c
   Core/Src/sensors.c
   Core/Src/ui_watch.c
   Core/Src/mic.c
   Core/Src/recorder.c
   Core/Src/power_mon.c
   Core/Src/sd_glue.c
   Core/Src/weather.c
   Core/Src/rtc_tasks.c
   Core/Src/uart_cmd.c
   ${TINYUSB_SOURCES}
   ${LVGL_SOURCES}
   ${UI_SOURCES}
   
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Core/Inc
    Drivers/Config
    Drivers/example
    Drivers/Fonts
    Drivers/GUI_DEV
    Drivers/image
    Drivers/LCD
    tinyusb
    Drivers/lvgl
    Drivers/lvgl/UI
   
    
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
