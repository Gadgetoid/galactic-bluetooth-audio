add_library(display INTERFACE)

pico_generate_pio_header(display ${CMAKE_CURRENT_LIST_DIR}/galactic_unicorn.pio)

target_sources(display INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/galactic_unicorn.cpp
)

target_include_directories(display INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../
)

# Pull in pico libraries that we need
target_link_libraries(display INTERFACE pico_stdlib hardware_adc hardware_pio hardware_dma)

set(DISPLAY_NAME "Galactic Unicorn")