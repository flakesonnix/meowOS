cmake_minimum_required(VERSION 3.20)

project(meowOS)

set(CMAKE_CXX_STANDARD 20)

# Common library sources
set(MEOW_SOURCES
    meow/src/archive/archive.cpp
    meow/src/package/parser.cpp
    meow/src/package/load.cpp
    meow/src/repository/repository.cpp
    meow/src/repository/backend.cpp
    meow/src/repository/http_backend.cpp
    meow/src/repository/manager.cpp
    meow/src/repository/security_policy.cpp
    meow/src/repository/status.cpp
    meow/src/repository/failover.cpp
    meow/src/repository/refresh.cpp
    meow/src/repository/version.cpp
    meow/src/repository/resolver.cpp
    meow/src/install/installer.cpp
    meow/src/download/downloader.cpp
    meow/src/download/queue.cpp
    meow/src/error/printer.cpp
    meow/src/database/database.cpp
    meow/src/doctor/doctor.cpp
    meow/src/hooks/runner.cpp
    meow/src/dependency/resolver.cpp
    meow/src/dependency/constraint.cpp
    meow/src/dependency/legacy_resolver.cpp
    meow/src/dependency/sat_resolver.cpp
    meow/src/dependency/resolver_factory.cpp
    meow/src/sat/problem.cpp
    meow/src/sat/solver.cpp
    meow/src/sat/dpll.cpp
    meow/src/sat/translate.cpp
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

# Shared core library (everything except the CLI entry points) so unit tests
# can link the backend, resolver, and package loader without a main().
add_library(meow_core STATIC ${MEOW_SOURCES})
target_include_directories(meow_core PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)

# CLI tools
add_executable(meow
    meow/src/main.cpp
)
target_link_libraries(meow PRIVATE meow_core)

add_executable(meow-build
    meow/src/meow-build/main.cpp
)
target_link_libraries(meow-build PRIVATE meow_core)

add_executable(meow-repo
    meow/src/meow-repo/main.cpp
)
target_link_libraries(meow-repo PRIVATE meow_core)

add_executable(meow-server
    meow/src/meow-server/main.cpp
)

# Disk/network-free unit tests for the repository backends.
add_executable(meow-unit-backend test/unit/backend_test.cpp)
target_include_directories(meow-unit-backend
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-backend PRIVATE meow_core)

# Disk/network-free unit tests for the package-history / install-reason layer.
add_executable(meow-unit-history test/unit/history_test.cpp)
target_include_directories(meow-unit-history
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-history PRIVATE meow_core)

# Disk/network-free unit tests for optional-dependency expansion (phase 2).
add_executable(meow-unit-optional test/unit/optional_test.cpp)
target_include_directories(meow-unit-optional
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-optional PRIVATE meow_core)

# Translation-first SAT tests: package graph -> CNF clauses (no solving yet).
add_executable(meow-unit-sat test/unit/sat_translate_test.cpp)
target_include_directories(meow-unit-sat
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-sat PRIVATE meow_core)

# Repository-free solver tests: DPLL on raw CNF (no packages involved).
add_executable(meow-unit-sat-solver test/unit/sat_solver_test.cpp)
target_include_directories(meow-unit-sat-solver
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-sat-solver PRIVATE meow_core)

# Translation correctness: CNF semantics + clause counts + deterministic hash.
add_executable(meow-unit-sat-translate-correctness test/unit/sat_translate_correctness_test.cpp)
target_include_directories(meow-unit-sat-translate-correctness
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-sat-translate-correctness PRIVATE meow_core)

# Parity: LegacyResolver vs SatResolver must select the same package set.
add_executable(meow-unit-sat-parity test/unit/sat_parity_test.cpp)
target_include_directories(meow-unit-sat-parity
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-sat-parity PRIVATE meow_core)

# Resolver engine config switch (legacy / sat / auto).
add_executable(meow-unit-resolver-config test/unit/resolver_config_test.cpp)
target_include_directories(meow-unit-resolver-config
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-resolver-config PRIVATE meow_core)

# Secure archive extraction regression tests (path traversal, symlink escape).
add_executable(meow-unit-archive-security test/unit/archive_security_test.cpp)
target_include_directories(meow-unit-archive-security
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-archive-security PRIVATE meow_core LibArchive::LibArchive)

# Repository security policy: require_repository_signature enforcement.
add_executable(meow-unit-security-policy test/unit/security_policy_test.cpp)
target_include_directories(meow-unit-security-policy
    PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_link_libraries(meow-unit-security-policy PRIVATE meow_core)

target_include_directories(meow PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_include_directories(meow-build PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)
target_include_directories(meow-repo PRIVATE ${PROJECT_SOURCE_DIR}/meow/include)

find_package(SQLite3 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(tomlplusplus QUIET)
if(NOT tomlplusplus_FOUND)
    include(FetchContent)
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

foreach(tgt meow meow-build meow-repo meow_core)
    target_link_libraries(${tgt}
        PRIVATE
        SQLite::SQLite3
        LibArchive::LibArchive
        OpenSSL::Crypto
        CURL::libcurl
        tomlplusplus::tomlplusplus
    )
endforeach()

target_link_libraries(meow-server PRIVATE Threads::Threads)
