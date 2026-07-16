add_executable(heartstead_render_smoke main.cpp)
target_link_libraries(heartstead_render_smoke PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_render_smoke
  PRIVATE
    HEARTSTEAD_RENDER_SMOKE_ASSET_DIR="$<TARGET_FILE_DIR:heartstead_render_smoke>/assets"
)
heartstead_configure_target(heartstead_render_smoke)
heartstead_attach_terrain_shader_assets(heartstead_render_smoke)
