add_library(display INTERFACE)

pico_generate_pio_header(display ${CMAKE_CURRENT_LIST_DIR}/widget.pio)

target_sources(display INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/widget.cpp
)

target_include_directories(display INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../
)

# Pull in pico libraries that we need
target_link_libraries(display INTERFACE pico_stdlib hardware_adc hardware_pio hardware_dma)

set(DISPLAY_NAME "Widget")

target_compile_definitions(blunicorn PRIVATE
    EFFECTS_ON_CORE1=1
    PICO_AUDIO_I2S_DATA_PIN=8
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=9
    PICO_AUDIO_I2C_AMP_ENABLE=11
    BLUETOOTH_DEVICE_NAME="${DISPLAY_NAME}"
)