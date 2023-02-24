add_library(galactic_unicorn INTERFACE)

pico_generate_pio_header(galactic_unicorn ${CMAKE_CURRENT_LIST_DIR}/galactic_unicorn.pio)

target_sources(galactic_unicorn INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/galactic_unicorn.cpp
)

target_include_directories(galactic_unicorn INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(galactic_unicorn INTERFACE pico_stdlib hardware_adc hardware_pio hardware_dma)