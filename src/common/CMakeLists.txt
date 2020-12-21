add_library(common INTERFACE)
add_library(${PROJECT_NAME}::common ALIAS common)
target_compile_features(common INTERFACE cxx_std_17)

target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
