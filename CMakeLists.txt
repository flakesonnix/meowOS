cmake_minimum_required(VERSION 3.20)

project(meowOS)

set(CMAKE_CXX_STANDARD 20)

# Common library sources
set(MEOW_SOURCES
    meow/src/archive/archive.cpp
    meow/src/package/parser.cpp
    meow/src/package/load.cpp
    meow/src/repository/repository.cpp
    meow/src/repository/version.cpp
    meow/src/repository/resolver.cpp
    meow/src/install/installer.cpp
    meow/src/download/downloader.cpp
    meow/src/download/queue.cpp
    meow/src/error/printer.cpp
    meow/src/database/database.cpp
    meow/src/doctor/doctor.cpp
    meow/src/dependency/resolver.cpp
    meow/src/dependency/constraint.cpp
    meow/src/transaction/transaction.cpp
    meow/src/remove/remove.cpp
    meow/src/upgrade/upgrade.cpp
    meow/src/lock/lockfile.cpp
    meow/src/verify/verifier.cpp
    meow/src/repair/repair.cpp
    meow/src/sync/sync.cpp
    meow/src/update/updater.cpp
    meow/src/log/logger.cpp
    meow/src/config/config.cpp
    meow/src/crypto/signature.cpp
    meow/src/crypto/keystore.cpp
    meow/src/builder/builder.cpp
    meow/src/repo-builder/repo_builder.cpp
    meow/src/format/version.cpp
    meow/src/database/migration.cpp
)

# CLI tools
add_executable(meow
    meow/src/main.cpp
    ${MEOW_SOURCES}
)

add_executable(meow-build
    meow/src/meow-build/main.cpp
    ${MEOW_SOURCES}
)

add_executable(meow-repo
    meow/src/meow-repo/main.cpp
    ${MEOW_SOURCES}
)

target_include_directories(meow PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_include_directories(meow-build PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_include_directories(meow-repo PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)

find_package(SQLite3 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(tomlplusplus QUIET)
if(NOT tomlplusplus_FOUND)
    include(FetchContent)
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

foreach(tgt meow meow-build meow-repo)
    target_link_libraries(${tgt}
        PRIVATE
        SQLite::SQLite3
        LibArchive::LibArchive
        OpenSSL::Crypto
        CURL::libcurl
        tomlplusplus::tomlplusplus
    )
endforeach()
