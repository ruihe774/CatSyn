find_package(Git)

if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    message(WARNING "Git not found: cannot retrieve commit hash")
    set(COMMIT_HASH "0000000000000000000000000000000000000000")
endif()

string(SUBSTRING ${COMMIT_HASH} 0 8 SHORT_COMMIT_HASH)
