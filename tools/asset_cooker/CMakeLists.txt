add_executable(heartstead_asset_cooker main.cpp)
target_link_libraries(heartstead_asset_cooker PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_asset_cooker
  PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_asset_cooker)
