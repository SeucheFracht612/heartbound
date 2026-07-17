add_executable(heartstead_dev_game main.cpp)
target_link_libraries(heartstead_dev_game PRIVATE Heartstead::GameRuntime)
target_compile_definitions(
  heartstead_dev_game PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_dev_game)
