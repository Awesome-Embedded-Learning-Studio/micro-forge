# 参数化编译 TAMCPP 1/2/3 样例的 C++ elf,借 micro-forge 的 STM32CubeF1 HAL。
# 不复制 TAMCPP 源码(绝对路径引用),用 build_sample.sh 编译。
# TAMCPP_ROOT 由父 CMakeLists 解析(cache 变量,不进 git)。

find_program(ARM_GCC arm-none-eabi-gcc)
find_program(ARM_GXX arm-none-eabi-g++)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)
if(NOT ARM_GCC OR NOT ARM_GXX OR NOT ARM_OBJCOPY)
    message(STATUS "arm-none-eabi toolchain incomplete, skipping TAMCPP firmware")
    return()
endif()

set(CUBE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/STM32CubeF1")
set(OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# 编一个样例:short(产物名/ target 后缀)、dir(TAMCPP 子目录)、extra(额外 .cpp)
function(tamcpp_build_sample short dir)
    set(extra ${ARGN})
    set(sample ${TAMCPP_ROOT}/${dir})
    set(ld ${sample}/STM32F103C8TX_FLASH.ld)
    set(deps ${CMAKE_CURRENT_SOURCE_DIR}/build_sample.sh ${ld}
        ${sample}/main.cpp ${sample}/system/clock.cpp
        ${sample}/system/hal_mock.c ${sample}/system/syscall.c)
    foreach(e ${extra})
        list(APPEND deps ${sample}/${e})
    endforeach()
    add_custom_command(
        OUTPUT ${OUT_DIR}/${short}.elf ${OUT_DIR}/${short}.bin
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/build_sample.sh
                ${short} ${dir} ${TAMCPP_ROOT} ${CUBE_BASE} ${OUT_DIR} ${extra}
        DEPENDS ${deps}
        COMMENT "[tamcpp/${short}] building ${short}.elf (STM32CubeF1 + TAMCPP C++)"
        VERBATIM
    )
    add_custom_target(tamcpp_${short}_firmware ALL
        DEPENDS ${OUT_DIR}/${short}.elf ${OUT_DIR}/${short}.bin)
endfunction()

tamcpp_build_sample(led    1_led_control)
tamcpp_build_sample(button 2_button_control)
tamcpp_build_sample(uart   3_uart_logger system/printf_redirect.cpp system/uart_irq.cpp)
