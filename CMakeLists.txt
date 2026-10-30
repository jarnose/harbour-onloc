cmake_minimum_required(VERSION 3.19)
project(harbour-onloc VERSION 1.0.0 LANGUAGES CXX)

# The application and the background service are two packages built from one
# tree: rpm/harbour-onloc.spec builds the application (the defaults), and
# daemon/onloc-daemon.spec passes -DBUILD_APP=OFF -DBUILD_DAEMON=ON. See
# docs/daemon.md for why they are separate.
option(BUILD_APP "Build the Silica application" ON)
option(BUILD_DAEMON "Build the onlocd background service" OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(BUILD_APP)

find_package(Qt5 COMPONENTS Core Qml Gui Quick Network Positioning LinguistTools REQUIRED)

include(FindPkgConfig)
pkg_search_module(SAILFISH sailfishapp REQUIRED)
pkg_search_module(SAILFISHSECRETS sailfishsecrets REQUIRED)

add_executable(harbour-onloc
    src/harbour-onloc.cpp
    src/api/onlocclient.cpp
    src/shared/daemonconfig.cpp
    src/shared/locationjson.cpp
    src/secrets/secretvault.cpp
    src/model/maplistmodel.cpp
    src/map/mercator.cpp
    src/map/vectortile.cpp
    src/map/maptilesource.cpp
    src/map/mapcanvas.cpp
    src/controller/appcontroller.cpp
)

target_compile_definitions(harbour-onloc PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
    ONLOC_VERSION="${PROJECT_VERSION}"
)

# -DDAEMON_RPM=<path to onloc-daemon-*.rpm> embeds that package in the
# application binary as a Qt resource, and Settings grows an entry that writes
# it out and asks the system to install it. Same design as harbour-suuntosync's
# notification daemon: the daemon cannot run in the Sailjail sandbox, so it is
# its own package, and the application carries it and hands it to the OS's
# installer, which asks the user. The application installs nothing itself.
#
# Optional on purpose: the daemon's RPM is the product of a separate build of
# this same tree (daemon/onloc-daemon.spec), so requiring it would make the
# application unbuildable alone and the ordering circular. Without it the
# application builds as before and the Settings entry is absent, not inert.
set(DAEMON_RPM "" CACHE FILEPATH
    "onloc-daemon RPM to embed in the application for in-app install")

# A conventional path as well as the option, because the package build never
# reaches the cmake command line: the spec calls %cmake, and threading an
# option through it would need an rpm macro, an sfdk argument and a CI
# incantation that all have to agree. Dropping the file here needs none of it.
# CI builds the daemon first and copies it here; gitignored, since a built RPM
# has no business in the source tree.
if(NOT DAEMON_RPM AND EXISTS "${CMAKE_SOURCE_DIR}/daemon/embed/onloc-daemon.rpm")
    set(DAEMON_RPM "${CMAKE_SOURCE_DIR}/daemon/embed/onloc-daemon.rpm")
endif()

if(DAEMON_RPM)
    if(NOT EXISTS "${DAEMON_RPM}")
        message(FATAL_ERROR "DAEMON_RPM does not exist: ${DAEMON_RPM}")
    endif()
    # The RPM lead's magic (ed ab ee db): a path typo that pointed at a
    # tarball would otherwise yield an application that offers an install and
    # hands the installer something unreadable - found only on a phone.
    file(READ "${DAEMON_RPM}" DAEMON_RPM_LEAD HEX LIMIT 4)
    if(NOT DAEMON_RPM_LEAD STREQUAL "edabeedb")
        message(FATAL_ERROR
                "DAEMON_RPM is not an RPM (lead is 0x${DAEMON_RPM_LEAD}, "
                "expected 0xedabeedb): ${DAEMON_RPM}")
    endif()

    # The .qrc is generated, because it names a path only known at configure
    # time; the RPM is copied beside it under a stable name so the resource
    # path carries no version. "daemon-embed", not "rpms": the build is
    # in-tree, and a one-letter difference between a source directory and a
    # generated one is a trap. The *resource* path is set by the prefix below.
    configure_file("${DAEMON_RPM}"
                    "${CMAKE_CURRENT_BINARY_DIR}/daemon-embed/onloc-daemon.rpm"
                    COPYONLY)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/daemon-embed/daemon-rpm.qrc"
            "<RCC>\n"
            "  <qresource prefix=\"/rpms\">\n"
            "    <file>onloc-daemon.rpm</file>\n"
            "  </qresource>\n"
            "</RCC>\n")
    qt5_add_resources(DAEMON_RPM_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/daemon-embed/daemon-rpm.qrc")
    target_sources(harbour-onloc PRIVATE ${DAEMON_RPM_RESOURCE})
    target_compile_definitions(harbour-onloc PRIVATE ONLOC_EMBEDDED_DAEMON_RPM)
    message(STATUS "Embedding the background service: ${DAEMON_RPM}")
endif()

target_include_directories(harbour-onloc PRIVATE
    $<BUILD_INTERFACE:
    ${SAILFISH_INCLUDE_DIRS}
    ${SAILFISHSECRETS_INCLUDE_DIRS}
>)

target_link_libraries(harbour-onloc
    Qt5::Quick
    Qt5::Network
    Qt5::Positioning
    ${SAILFISH_LDFLAGS}
    ${SAILFISHSECRETS_LDFLAGS}
)

install(TARGETS harbour-onloc
    RUNTIME DESTINATION bin
)
install(DIRECTORY qml
    DESTINATION share/harbour-onloc
)
install(FILES harbour-onloc.desktop
    DESTINATION share/applications
)
foreach(size 86x86 108x108 128x128 172x172)
    install(FILES icons/${size}/harbour-onloc.png
        DESTINATION share/icons/hicolor/${size}/apps
    )
endforeach()

# Source strings are English; translations/harbour-onloc.ts is the lupdate
# extraction base and each harbour-onloc-<lang>.ts a translated copy.
# qt5_add_translation only runs lrelease, never lupdate.
file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/translations/*.ts")
file(GLOB LANG_TS_FILES "${CMAKE_SOURCE_DIR}/translations/harbour-onloc-*.ts")
qt5_add_translation(QM_FILES ${LANG_TS_FILES})
add_custom_target(translations ALL DEPENDS ${QM_FILES})
add_dependencies(harbour-onloc translations)

install(FILES ${QM_FILES}
    DESTINATION share/harbour-onloc/translations
)

add_custom_target(distfiles
    SOURCES
        harbour-onloc.desktop
        qml/harbour-onloc.qml
        qml/cover/CoverPage.qml
        qml/pages/LoginPage.qml
        qml/pages/MainPage.qml
        qml/pages/DevicePage.qml
        qml/pages/TrackingPage.qml
        qml/pages/SettingsPage.qml
        docs/base-map.md
        qml/pages/Format.js
        rpm/harbour-onloc.spec
        daemon/onloc-daemon.spec
        daemon/onlocd.service
        docs/daemon.md
        README.md
        ${TS_FILES})

endif() # BUILD_APP

if(BUILD_DAEMON)

find_package(Qt5 COMPONENTS Core Network Positioning DBus REQUIRED)

add_executable(onlocd
    src/onlocd.cpp
    src/api/onlocclient.cpp
    src/shared/daemonconfig.cpp
    src/shared/locationjson.cpp
    src/daemon/daemon.cpp
    src/daemon/locationreporter.cpp
    src/daemon/socketioclient.cpp
    src/daemon/commandhandler.cpp
)
target_compile_definitions(onlocd PRIVATE ONLOCD_VERSION="${PROJECT_VERSION}")
target_link_libraries(onlocd
    Qt5::Core
    Qt5::Network
    Qt5::Positioning
    Qt5::DBus
)
install(TARGETS onlocd RUNTIME DESTINATION bin)
install(FILES daemon/onlocd.service DESTINATION lib/systemd/user)

endif() # BUILD_DAEMON

file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}
${CMAKE_BINARY_DIR}/harbour-onloc:bin
")
