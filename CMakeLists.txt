cmake_minimum_required(VERSION 3.19)
project(harbour-onloc CXX)

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
)

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
