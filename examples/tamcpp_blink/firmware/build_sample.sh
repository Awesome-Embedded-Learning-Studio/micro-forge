#!/usr/bin/env bash
# 编译单个 TAMCPP C++ 样例 elf,借 micro-forge 的 STM32CubeF1 HAL(不改 TAMCPP 仓库)。
# 用法:build_sample.sh <short> <dir> <tamcpp_root> <cube_base> <out_dir> [extra_cpp...]
#   extra_cpp: 相对样例根的额外 .cpp
#   3_uart_logger 需: system/printf_redirect.cpp system/uart_irq.cpp
set -euo pipefail
short=$1; dir=$2; tamcpp_root=$3; cube=$4; out=$5; shift 5
extra=("$@")

sample=$tamcpp_root/$dir
ld=$sample/STM32F103C8TX_FLASH.ld
cmsis=$cube/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates
obj=$out/obj_$short
mkdir -p "$obj"

common=(-mcpu=cortex-m3 -mthumb -O2 -g -Wall -Wextra -Wno-missing-field-initializers
        -ffunction-sections -fdata-sections -DUSE_HAL_DRIVER -DSTM32F103xB -DHSE_VALUE=8000000)
inc=(-I"$sample" -I"$sample/system" -I"$cube/Drivers/CMSIS/Include"
     -I"$cube/Drivers/CMSIS/Device/ST/STM32F1xx/Include" -I"$cube/Drivers/STM32F1xx_HAL_Driver/Inc")

echo "--- [tamcpp/$short] C sources (HAL + system + syscall) ---"
for f in "$cube"/Drivers/STM32F1xx_HAL_Driver/Src/*.c "$cmsis/system_stm32f1xx.c" "$sample/system/hal_mock.c" "$sample/system/syscall.c"; do
    [[ "$f" == *_template.c ]] && continue
    arm-none-eabi-gcc "${common[@]}" "${inc[@]}" -c "$f" -o "$obj/$(basename "$f" .c).o"
done

echo "--- [tamcpp/$short] C++ sources (main + clock + extra) ---"
arm-none-eabi-g++ "${common[@]}" -std=c++23 -fno-exceptions -fno-rtti "${inc[@]}" -c "$sample/main.cpp" -o "$obj/main.o"
arm-none-eabi-g++ "${common[@]}" -std=c++23 -fno-exceptions -fno-rtti "${inc[@]}" -c "$sample/system/clock.cpp" -o "$obj/clock.o"
if [[ ${#extra[@]} -gt 0 ]]; then
    for e in "${extra[@]}"; do
        arm-none-eabi-g++ "${common[@]}" -std=c++23 -fno-exceptions -fno-rtti "${inc[@]}" -c "$sample/$e" -o "$obj/$(basename "$e" .cpp).o"
    done
fi

echo "--- [tamcpp/$short] startup.s ---"
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -x assembler-with-cpp -c "$cmsis/gcc/startup_stm32f103xb.s" -o "$obj/startup.o"

echo "--- [tamcpp/$short] link (g++ for libstdc++ init) ---"
arm-none-eabi-g++ -mcpu=cortex-m3 -mthumb -T"$ld" -nostartfiles -specs=nano.specs -specs=nosys.specs \
    -Wl,--gc-sections -Wl,-Map="$out/$short.map" -o "$out/$short.elf" "$obj"/*.o
arm-none-eabi-objcopy -O binary "$out/$short.elf" "$out/$short.bin"
arm-none-eabi-size --format=berkeley "$out/$short.elf"
echo "=== [tamcpp/$short] OK: $out/$short.elf ==="
