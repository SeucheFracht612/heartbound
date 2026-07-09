add_executable(heartstead_save_inspector main.cpp)
target_link_libraries(heartstead_save_inspector PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_save_inspector
  PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_save_inspector)
