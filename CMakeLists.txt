cmake_minimum_required(VERSION 3.25.1)
project(seabattle
        VERSION 0.2.0.0
        LANGUAGES CXX
        HOMEPAGE_URL "https://github.com/khromenokroman/seabattle")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Собирать тесты" ON)

find_package(nlohmann_json 3 REQUIRED)
find_package(Threads REQUIRED)

include(GNUInstallDirs)

configure_file(Version.in Version @ONLY)

# cpp-httplib header-only из third_party: пакет Debian собран в разделяемую библиотеку без .a
add_library(httplib INTERFACE)
target_include_directories(httplib SYSTEM INTERFACE third_party)

set(SEABATTLE_WEB_DIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/web")

# игровая логика без сети
add_library(${PROJECT_NAME}-core STATIC
        core/src/board.cpp
        core/src/fleet.cpp
        core/src/game.cpp
)
target_include_directories(${PROJECT_NAME}-core PUBLIC core/include)
target_compile_options(${PROJECT_NAME}-core PRIVATE -Wall -Wextra)

# сервер: комнаты, HTTP API, события SSE
add_library(${PROJECT_NAME}-server STATIC
        server/api.cpp
        server/json_io.cpp
        server/options.cpp
        server/rooms.cpp
)
target_include_directories(${PROJECT_NAME}-server PUBLIC server)
target_compile_definitions(${PROJECT_NAME}-server PUBLIC
        SEABATTLE_VERSION="${PROJECT_VERSION}"
        SEABATTLE_WEB_DIR="${SEABATTLE_WEB_DIR}")
target_compile_options(${PROJECT_NAME}-server PRIVATE -Wall -Wextra)
target_link_libraries(${PROJECT_NAME}-server PUBLIC
        ${PROJECT_NAME}-core httplib nlohmann_json::nlohmann_json Threads::Threads)

add_executable(${PROJECT_NAME}
        server/main.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}-server)

if (BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(${PROJECT_NAME}-core-tests
            tests/core/board_test.cpp
            tests/core/fleet_test.cpp
            tests/core/game_test.cpp
    )
    target_link_libraries(${PROJECT_NAME}-core-tests PRIVATE ${PROJECT_NAME}-core GTest::gtest_main)
    gtest_discover_tests(${PROJECT_NAME}-core-tests)

    add_executable(${PROJECT_NAME}-server-tests
            tests/server/api_test.cpp
            tests/server/json_io_test.cpp
            tests/server/options_test.cpp
            tests/server/rooms_test.cpp
    )
    target_link_libraries(${PROJECT_NAME}-server-tests PRIVATE ${PROJECT_NAME}-server GTest::gtest_main)
    gtest_discover_tests(${PROJECT_NAME}-server-tests)
endif ()

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY web/
        DESTINATION ${SEABATTLE_WEB_DIR}
)
install(FILES debian/copyright
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# служба systemd: пользователь seabattle, параметры в /etc/default/seabattle
configure_file(debian/seabattle.service.in seabattle.service @ONLY)
configure_file(debian/conffiles.in conffiles @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/seabattle.service
        DESTINATION lib/systemd/system
)
install(FILES debian/default
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/default
        RENAME seabattle
)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "khromenokroman")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "khromenokroman <roma55592@yandex.ru>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Network Battleship game server with web client")
set(CPACK_PACKAGE_DESCRIPTION "Two players connect from a browser, create a room or join by code,\nplace the fleet and play by classic rules. The server keeps both boards.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")

set(CPACK_GENERATOR "DEB")
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "adduser, init-system-helpers")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        ${CMAKE_CURRENT_BINARY_DIR}/conffiles
        ${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst
        ${CMAKE_CURRENT_SOURCE_DIR}/debian/prerm
        ${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm
)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

include(CPack)
