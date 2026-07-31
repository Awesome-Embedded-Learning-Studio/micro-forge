cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

find_program(ARM_GCC arm-none-eabi-gcc)
if(NOT ARM_GCC)
    find_program(ARM_GCC arm-none-linux-gnueabihf-gcc)
endif()
if(NOT ARM_GCC)
    message(WARNING "No ARM GCC found, skipping HAL firmware build")
    return()
endif()
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)
if(NOT ARM_OBJCOPY)
    find_program(ARM_OBJCOPY arm-none-linux-gnueabihf-objcopy)
endif()
if(NOT ARM_OBJCOPY)
    message(WARNING "No ARM objcopy found, skipping HAL firmware build")
    return()
endif()

set(CUBE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/STM32CubeF1")

file(GLOB HAL_SRC
    ${CUBE_BASE}/Drivers/STM32F1xx_HAL_Driver/Src/*.c
)
list(FILTER HAL_SRC EXCLUDE REGEX ".*_template\\.c$")

set(ALL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${HAL_SRC}
)

set(INCLUDE_FLAGS)
list(APPEND INCLUDE_FLAGS
    -I${CMAKE_CURRENT_SOURCE_DIR}
    -I${CUBE_BASE}/Drivers/CMSIS/Include
    -I${CUBE_BASE}/Drivers/CMSIS/Device/ST/STM32F1xx/Include
    -I${CUBE_BASE}/Drivers/STM32F1xx_HAL_Driver/Inc
)

add_custom_command(
    OUTPUT wfi_blink.elf wfi_blink.bin
    COMMAND ${ARM_GCC}
        -mcpu=cortex-m3 -mthumb -nostartfiles
        -DSTM32F103xB -DUSE_HAL_DRIVER -DHSE_VALUE=8000000
        -O2 -g -ffunction-sections -fdata-sections
        ${INCLUDE_FLAGS}
        -T ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103_HAL.ld
        -o wfi_blink.elf
        ${ALL_SOURCES}
        -specs=nano.specs -specs=nosys.specs
        -Wl,--gc-sections
    COMMAND ${ARM_OBJCOPY} -O binary wfi_blink.elf wfi_blink.bin
    DEPENDS ${ALL_SOURCES}
            ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103_HAL.ld
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building wfi_blink firmware (STM32CubeF1 HAL)"
)

add_custom_target(wfi_blink_firmware ALL DEPENDS wfi_blink.elf wfi_blink.bin)
