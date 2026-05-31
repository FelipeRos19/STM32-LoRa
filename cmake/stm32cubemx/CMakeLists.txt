cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)
# STM32CubeMX generated symbols (macros)
set(MX_Defines_Syms 
	CORE_CM4 
	USE_HAL_DRIVER 
	STM32WLE5xx 
	STM32_THREAD_SAFE_STRATEGY=4
    $<$<CONFIG:Debug>:DEBUG>
)

# STM32CubeMX generated include paths
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/trace/adv_trace
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/misc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/timer
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/lpm/tiny_lpm
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/RTOS2/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32WLxx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/SubGHz_Phy/radio_driver
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# STM32CubeMX generated application sources
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/app_freertos.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/aes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/iwdg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/rng.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/subghz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/stm32_lpm_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/sys_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/app_subghz_phy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/subghz_phy_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/radio_board_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/timer_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/stm32_adv_trace_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/usart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/stm32wlxx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/stm32wlxx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/stm32wlxx_hal_timebase_tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../startup_stm32wle5xx.s
)

# STM32 HAL/LL Drivers
set(STM32_Drivers_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Src/system_stm32wlxx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_tim_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_exti.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cryp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cryp_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_crc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_iwdg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rng.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rng_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_subghz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_uart_ex.c
)

# Drivers Midllewares


set(Utilities_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/trace/adv_trace/stm32_adv_trace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/misc/stm32_mem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/misc/stm32_systime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/misc/stm32_tiny_sscanf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/misc/stm32_tiny_vsnprintf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/timer/stm32_timer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/lpm/tiny_lpm/stm32_lpm.c
)
set(FreeRTOS_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/croutine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/event_groups.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/timers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c
)
set(SubGHz_Phy_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/SubGHz_Phy/radio_driver/wl_lr_fhss.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_fw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/SubGHz_Phy/radio_driver/lr_fhss_mac.c
)

# Link directories setup
set(MX_LINK_DIRS

)
# Project static libraries
set(MX_LINK_LIBS 
    STM32_Drivers
    ${TOOLCHAIN_LINK_LIBRARIES}
    Utilities	FreeRTOS	SubGHz_Phy	
)
# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Create STM32_Drivers static library
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)


# Create Utilities static library
add_library(Utilities OBJECT)
target_sources(Utilities PRIVATE ${Utilities_Src})
target_link_libraries(Utilities PUBLIC stm32cubemx)

# Create FreeRTOS static library
add_library(FreeRTOS OBJECT)
target_sources(FreeRTOS PRIVATE ${FreeRTOS_Src})
target_link_libraries(FreeRTOS PUBLIC stm32cubemx)

# Create SubGHz_Phy static library
add_library(SubGHz_Phy OBJECT)
target_sources(SubGHz_Phy PRIVATE ${SubGHz_Phy_Src})
target_link_libraries(SubGHz_Phy PUBLIC stm32cubemx)

# Add STM32CubeMX generated application sources to the project
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})

# Add libraries to the project
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
