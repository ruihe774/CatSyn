cmake_minimum_required(VERSION 3.21)

project(Valve
    VERSION 1.0.0
    DESCRIPTION "Buffered pipe"
)

set(OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}/valve${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(OUTPUT "${OUTPUT_PATH}"
    COMMAND rustc --crate-name valve --crate-type bin --edition=2018 "${CMAKE_CURRENT_SOURCE_DIR}/src/main.rs" -C opt-level=3 -C panic=abort -C lto -o "${OUTPUT_PATH}"
    MAIN_DEPENDENCY src/main.rs
)
add_custom_target(valve ALL DEPENDS "${OUTPUT_PATH}")
install(FILES "${OUTPUT_PATH}" TYPE BIN)
