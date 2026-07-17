cmake_minimum_required(VERSION 4.2)

project(meowOS)

set(CMAKE_CXX_STANDARD 20)

add_executable(${PROJECT_NAME}
        meow/src/main.cpp
        meow/src/cli/command_parser.cpp
        meow/src/package/parser.cpp
)

target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/meow/include
)

find_package(SQLite3 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(tomlplusplus REQUIRED)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        SQLite::SQLite3
        LibArchive::LibArchive
        tomlplusplus::tomlplusplus
)