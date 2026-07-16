add_executable(heartstead_render_smoke main.cpp)
target_link_libraries(heartstead_render_smoke PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_render_smoke
  PRIVATE
    HEARTSTEAD_RENDER_SMOKE_ASSET_DIR="$<TARGET_FILE_DIR:heartstead_render_smoke>/assets"
)
heartstead_configure_target(heartstead_render_smoke)

add_custom_command(
  TARGET heartstead_render_smoke
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:heartstead_render_smoke>/assets"
  COMMENT "Copying render smoke shader assets"
)
