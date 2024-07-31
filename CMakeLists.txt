cmake_minimum_required(VERSION 3.12)

set(NAME blunicorn)

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

include(bluetooth/bluetooth.cmake)

if(HEADLESS)
set(DISPLAY_NAME "Headless Unicorn")
else()
include(effect/rainbow_fft.cmake)
include(effect/classic_fft.cmake)

if(DISPLAY_PATH AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/${DISPLAY_PATH})
include(${CMAKE_CURRENT_LIST_DIR}/${DISPLAY_PATH})
else()
message(WARNING "Using default display (display/galactic/galactic_unicorn.cmake)...")
include(display/galactic/galactic_unicorn.cmake)
endif()
endif()


add_executable(${NAME}
    ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/a2dp_sink.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/btstack_audio_pico.cpp
)

target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/effect
)

target_link_libraries(${NAME}
    picow_bt_example_common
    pico_audio_i2s
    pico_multicore
)

message(WARNING "Display: ${DISPLAY_NAME}")

target_compile_definitions(${NAME} PRIVATE
    PICO_AUDIO_I2S_DATA_PIN=9
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
    BLUETOOTH_DEVICE_NAME="${DISPLAY_NAME}"
)

if(HEADLESS)
target_compile_definitions(${NAME} PRIVATE
    HEADLESS=1
)
else()
target_link_libraries(${NAME}
    display
    rainbow_fft
    classic_fft
)
endif()

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