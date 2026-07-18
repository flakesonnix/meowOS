cmake_minimum_required(VERSION 3.20)

project(meowOS)

set(CMAKE_CXX_STANDARD 20)

add_executable(${PROJECT_NAME}
        meow/src/main.cpp
        meow/src/archive/archive.cpp
        meow/src/cli/command_parser.cpp
        meow/src/package/parser.cpp
        meow/src/package/load.cpp
        meow/src/repository/repository.cpp
        meow/src/repository/version.cpp
        meow/src/repository/resolver.cpp
        meow/src/install/installer.cpp
        meow/src/download/downloader.cpp
        meow/src/error/printer.cpp
        meow/src/database/database.cpp
        meow/src/dependency/resolver.cpp
        meow/src/transaction/transaction.cpp
        meow/src/remove/remove.cpp
        meow/src/upgrade/upgrade.cpp
        meow/src/lock/lockfile.cpp
        meow/src/verify/verifier.cpp
)

target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/meow/include
)

find_package(SQLite3 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(tomlplusplus QUIET)
if(NOT tomlplusplus_FOUND)
    include(FetchContent)
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        SQLite::SQLite3
        LibArchive::LibArchive
        tomlplusplus::tomlplusplus
)