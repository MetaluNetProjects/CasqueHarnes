string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--print-memory-usage")

target_link_libraries(${CMAKE_PROJECT_NAME} 
    hardware_uart
    pico_stdlib
    pico_rand
    hardware_pwm
    hardware_adc
)

