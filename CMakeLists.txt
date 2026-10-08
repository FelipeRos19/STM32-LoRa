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
set(CMAKE_PROJECT_NAME STM32-LoRa)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Host unit tests (preset "host-tests"): build only tests/ with the PC compiler
if(STM32_LORA_HOST_TESTS)
    enable_testing()
    add_subdirectory(tests)
    return()
endif()

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

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
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

# Generate .bin/.hex from .elf and print section sizes
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.bin and ${CMAKE_PROJECT_NAME}.hex"
)

# Flash target via UART using STM32CubeProgrammer
#
# This flashes via a small helper script so we can:
#  - optionally ask which serial port to use (FLASH_PROMPT, needs a real terminal)
#  - kill processes that currently hold the serial port (always the selected
#    port; every /dev/ttyUSB* and /dev/ttyACM* only with FLASH_KILL_ALL)
set(FLASH_PORT "/dev/ttyUSB0" CACHE STRING "Serial port for UART flashing (prompt default when FLASH_PROMPT=ON)")
set(FLASH_BAUD "115200"       CACHE STRING "Baud rate for UART flashing")
set(FLASH_PROMPT ON            CACHE BOOL   "Ask for the serial port before flashing (turn OFF in IDEs without a terminal)")
set(FLASH_KILL_ALL OFF         CACHE BOOL   "Kill processes using any /dev/ttyUSB* and /dev/ttyACM*, not only FLASH_PORT")

add_custom_target(flash
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/flash_uart.sh
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
        ${FLASH_BAUD}
        ${FLASH_PORT}
        ${FLASH_KILL_ALL}
        ${FLASH_PROMPT}
    USES_TERMINAL
    COMMENT "Flashing ${CMAKE_PROJECT_NAME}.bin to STM32 via UART"
)
