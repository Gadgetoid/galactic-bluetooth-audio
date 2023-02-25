add_library(classic_fft INTERFACE)

target_sources(classic_fft INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/classic_fft.cpp
  ${CMAKE_CURRENT_LIST_DIR}/lib/fixed_fft.cpp
)

target_include_directories(classic_fft INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)

# Choose one:
# SCALE_LOGARITHMIC
# SCALE_SQRT
# SCALE_LINEAR
target_compile_definitions(classic_fft INTERFACE
  -DSCALE_SQRT
)