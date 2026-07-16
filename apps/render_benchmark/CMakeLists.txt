add_executable(heartstead_render_benchmark main.cpp)
target_link_libraries(heartstead_render_benchmark PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_render_benchmark
  PRIVATE
    HEARTSTEAD_RENDER_BENCHMARK_ASSET_DIR="$<TARGET_FILE_DIR:heartstead_render_benchmark>/assets"
)
heartstead_configure_target(heartstead_render_benchmark)

add_custom_command(
  TARGET heartstead_render_benchmark
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../render_smoke/assets"
          "$<TARGET_FILE_DIR:heartstead_render_benchmark>/assets"
  COMMENT "Copying renderer benchmark shader assets"
)
