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
    Src/crypto.c
    Src/crypto_test.c   # TEMP: crypto bring-up test, remove later
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

# Generate .bin from .elf
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating binary: ${CMAKE_PROJECT_NAME}.bin"
)

# Flash target via UART using STM32CubeProgrammer
#
# This flashes via a small helper script so we can:
#  - interactively ask which serial port to use
#  - kill processes that currently hold the serial device(s)
set(FLASH_PORT "/dev/ttyUSB0" CACHE STRING "Default serial port for UART flashing (used as prompt default)")
set(FLASH_BAUD "115200"       CACHE STRING "Baud rate for UART flashing")
set(FLASH_KILL_ALL ON          CACHE BOOL   "Kill processes using /dev/ttyUSB* and /dev/ttyACM* before flashing")

add_custom_target(flash
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/flash_uart.sh
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
        ${FLASH_BAUD}
        ${FLASH_PORT}
        ${FLASH_KILL_ALL}
    USES_TERMINAL
    COMMENT "Flashing ${CMAKE_PROJECT_NAME}.bin to STM32 via UART (will prompt for port)"
)
