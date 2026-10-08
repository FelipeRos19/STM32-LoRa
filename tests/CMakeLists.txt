# Host (PC) unit tests for the hardware-independent modules in App/.
# Built only with the "host-tests" preset (STM32_LORA_HOST_TESTS=ON).

set(APP_PURE_SRC
    ${CMAKE_SOURCE_DIR}/App/Src/crc16.c
    ${CMAKE_SOURCE_DIR}/App/Src/proto_frame.c
    ${CMAKE_SOURCE_DIR}/App/Src/lora_toa.c
    ${CMAKE_SOURCE_DIR}/App/Src/alert_packet.c
    ${CMAKE_SOURCE_DIR}/App/Src/proto_msgs.c
)

add_executable(app_tests test_main.c ${APP_PURE_SRC})
target_include_directories(app_tests PRIVATE ${CMAKE_SOURCE_DIR}/App/Inc)
target_compile_options(app_tests PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
target_link_options(app_tests PRIVATE -fsanitize=address,undefined)

add_test(NAME app_tests COMMAND app_tests)
