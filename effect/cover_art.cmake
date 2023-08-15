add_library(cover_art INTERFACE)

target_sources(cover_art INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/cover_art.cpp
)

target_include_directories(cover_art INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)
