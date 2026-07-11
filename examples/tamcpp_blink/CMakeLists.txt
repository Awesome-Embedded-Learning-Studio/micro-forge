# TAMCPP C++ HAL 样例(1_led_control / 2_button_control / 3_uart_logger)。
# elf 由 firmware/ 参数化编译(借 micro-forge 的 STM32CubeF1 HAL,不改 TAMCPP 仓库)。
# runner 加载 elf 验证:led 看 PC13 toggle、button 注入 PA0 看 toggle、uart 看 TX 输出。

# TAMCPP_ROOT: stm32f1-tutorials 根,绝不硬编码进 git(cache 变量 + 环境变量 + 默认)。
if(NOT TAMCPP_ROOT)
    if(DEFINED ENV{TAMCPP_ROOT})
        set(TAMCPP_ROOT "$ENV{TAMCPP_ROOT}" CACHE PATH "TAMCPP stm32f1-tutorials root")
    else()
        set(TAMCPP_ROOT "$ENV{HOME}/Tutorial_AwesomeModernCPP/code/stm32f1-tutorials"
            CACHE PATH "TAMCPP stm32f1-tutorials root")
    endif()
endif()
if(NOT EXISTS "${TAMCPP_ROOT}/1_led_control/main.cpp")
    message(STATUS "TAMCPP_ROOT not found (${TAMCPP_ROOT}), skipping tamcpp samples")
    return()
endif()
message(STATUS "TAMCPP_ROOT = ${TAMCPP_ROOT}")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/STM32CubeF1/Drivers")
    message(STATUS "STM32CubeF1 submodule not found, skipping tamcpp samples")
    return()
endif()

add_subdirectory(firmware)

# ── runner: 每样例一个(elf 由 firmware target 编出,POST_BUILD copy 到 runner 目录)─
add_executable(tamcpp_blink_runner runner.cpp)
target_link_libraries(tamcpp_blink_runner PRIVATE micro_forge)
add_dependencies(tamcpp_blink_runner tamcpp_led_firmware)
add_custom_command(TARGET tamcpp_blink_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/led.elf
        ${CMAKE_CURRENT_BINARY_DIR}/tamcpp_led.elf)

if(TARGET tamcpp_button_firmware)
    add_executable(tamcpp_button_runner runner_button.cpp)
    target_link_libraries(tamcpp_button_runner PRIVATE micro_forge)
    add_dependencies(tamcpp_button_runner tamcpp_button_firmware)
    add_custom_command(TARGET tamcpp_button_runner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/firmware/button.elf
            ${CMAKE_CURRENT_BINARY_DIR}/tamcpp_button.elf)
endif()

if(TARGET tamcpp_uart_firmware)
    add_executable(tamcpp_uart_runner runner_uart.cpp)
    target_link_libraries(tamcpp_uart_runner PRIVATE micro_forge)
    add_dependencies(tamcpp_uart_runner tamcpp_uart_firmware)
    add_custom_command(TARGET tamcpp_uart_runner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/firmware/uart.elf
            ${CMAKE_CURRENT_BINARY_DIR}/tamcpp_uart.elf)
endif()
