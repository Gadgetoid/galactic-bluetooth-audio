cmake_minimum_required(VERSION 3.12)

set(NAME bluetooth-boilerplate)

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

include(pico_bt_example_common.cmake)
include(galactic/galactic_unicorn.cmake)

add_executable(${NAME}
    ${CMAKE_CURRENT_LIST_DIR}/picow_bt_example_background.cpp  # int main()
    ${CMAKE_CURRENT_LIST_DIR}/a2dp_sink_demo.cpp
    ${CMAKE_CURRENT_LIST_DIR}/btstack_audio_pico.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fixed_fft.cpp
)

target_link_libraries(${NAME}
    picow_bt_example_common
    pico_audio_i2s
    galactic_unicorn
)

target_compile_definitions(${NAME} PRIVATE
    PICO_AUDIO_I2S_DATA_PIN=9
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
)

pico_enable_stdio_usb(${NAME} 1)
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)