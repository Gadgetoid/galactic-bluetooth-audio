add_library(rainbow_fft INTERFACE)

target_sources(rainbow_fft INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/rainbow_fft.cpp
  ${CMAKE_CURRENT_LIST_DIR}/lib/fixed_fft.cpp
)

target_include_directories(rainbow_fft INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)

# Choose one:
# SCALE_LOGARITHMIC
# SCALE_SQRT
# SCALE_LINEAR
target_compile_definitions(rainbow_fft INTERFACE
  -DSCALE_SQRT
)