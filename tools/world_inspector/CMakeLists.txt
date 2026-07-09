add_executable(heartstead_world_inspector main.cpp)
target_link_libraries(heartstead_world_inspector PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_world_inspector
  PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_world_inspector)
