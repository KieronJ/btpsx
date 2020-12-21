find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIR})

add_executable(btpsx main.cpp)
target_compile_features(btpsx PRIVATE cxx_std_17)

add_subdirectory(common)
add_subdirectory(core)

target_link_libraries(btpsx PRIVATE btpsx::common btpsx::core)
target_link_libraries(btpsx PRIVATE stdc++fs nlohmann_json::nlohmann_json ${SDL2_LIBRARY} spdlog::spdlog)
