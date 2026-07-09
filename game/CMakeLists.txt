add_library(heartstead_game)
add_library(Heartstead::GameRuntime ALIAS heartstead_game)

target_sources(
  heartstead_game
  PRIVATE
    runtime/game_inspection.cpp
    runtime/game_runtime.cpp
    runtime/script_host_commands.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}
    FILES
      runtime/game_inspection.hpp
      runtime/game_runtime.hpp
      runtime/script_host_commands.hpp
)

target_link_libraries(heartstead_game PUBLIC Heartstead::Engine)
target_include_directories(heartstead_game PUBLIC ${PROJECT_SOURCE_DIR})

heartstead_configure_target(heartstead_game)
