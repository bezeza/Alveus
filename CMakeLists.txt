cmake_minimum_required(VERSION 3.21)

project(alveus VERSION 0.3.1 LANGUAGES CXX)
set(ALVEUS_DISPLAY_VERSION "0.03.1 beta")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick DBus)

include(GNUInstallDirs)

option(ALVEUS_BUILD_TESTS "Build the tests" OFF)

# Where the QML module goes: ask Qt itself, so it lands on the import path
# plasmashell actually uses (it differs between distributions).
if(NOT ALVEUS_QML_DIR)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    execute_process(COMMAND "${_qmake}" -query QT_INSTALL_QML
                    OUTPUT_VARIABLE ALVEUS_QML_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
message(STATUS "Alveus QML module will be installed to: ${ALVEUS_QML_DIR}")

set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

add_subdirectory(core)

# The desktop layout (Plasma containment) itself is plain QML
install(DIRECTORY package/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/ua.bezeza.alveus)

# Application icon used by Plasma, KRunner and desktop launchers
install(FILES data/icons/hicolor/scalable/apps/alveus.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# KRunner (Alt+Space) finds fences and desktop files while Alveus is running
install(FILES data/ua.bezeza.alveus.runner.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/krunner/dbusplugins)

# Translations
find_program(MSGFMT_EXECUTABLE msgfmt)
if(MSGFMT_EXECUTABLE)
    file(GLOB _po_files ${CMAKE_SOURCE_DIR}/po/*.po)
    foreach(_po ${_po_files})
        get_filename_component(_lang ${_po} NAME_WE)
        set(_mo ${CMAKE_BINARY_DIR}/locale/${_lang}/plasma_applet_ua.bezeza.alveus.mo)
        add_custom_command(OUTPUT ${_mo}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/locale/${_lang}
            COMMAND ${MSGFMT_EXECUTABLE} -o ${_mo} ${_po}
            DEPENDS ${_po})
        list(APPEND _mo_files ${_mo})
        install(FILES ${_mo} DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${_lang}/LC_MESSAGES)
    endforeach()
    add_custom_target(translations ALL DEPENDS ${_mo_files})
else()
    message(WARNING "msgfmt not found (package gettext): translations will not be installed")
endif()

if(ALVEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
