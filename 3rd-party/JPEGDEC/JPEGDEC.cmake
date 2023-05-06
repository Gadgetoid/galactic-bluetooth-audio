add_library(JPEGDEC INTERFACE)

target_sources(cover_art INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/JPEGDEC.cpp
)

target_include_directories(cover_art INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)
