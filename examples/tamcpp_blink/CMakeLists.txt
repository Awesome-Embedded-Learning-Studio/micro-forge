# A1 spike: 加载 TAMCPP 1_led_control 的 C++ elf。
# elf 由 /tmp/tamcpp-build/build.sh 编出(A0),runner 默认读 tamcpp_led.elf。
add_executable(tamcpp_blink_runner runner.cpp)
target_link_libraries(tamcpp_blink_runner PRIVATE micro_forge)
