find_package(Qt6 REQUIRED COMPONENTS Test)

set(_core ${CMAKE_SOURCE_DIR}/core)
add_executable(coretest coretest.cpp
    ${_core}/alveuscore.cpp ${_core}/alveuscore.h ${_core}/rules.cpp ${_core}/rules.h
    ${_core}/thumbnails.cpp ${_core}/thumbnails.h ${_core}/integrations.cpp ${_core}/integrations.h
    ${_core}/runner.cpp ${_core}/runner.h ${_core}/drives.cpp ${_core}/drives.h
    ${_core}/fileops.cpp ${_core}/fileops.h)
target_compile_definitions(coretest PRIVATE ALVEUS_VERSION="${ALVEUS_DISPLAY_VERSION}")
target_link_libraries(coretest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus Qt6::Test)
add_test(NAME coretest COMMAND coretest)
set_tests_properties(coretest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Renders the desktop UI offscreen with stand-in Plasma/Kirigami modules
add_executable(uiharness uiharness.cpp)
target_link_libraries(uiharness PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
add_subdirectory(mockplasmoid)

# Exercise the real desktop QML scene with the test Plasma/Kirigami modules.
add_dependencies(uiharness alveuscoreplugin mockplasmoid)
add_test(NAME uiharness COMMAND uiharness
    "${CMAKE_SOURCE_DIR}/tests/scene.qml"
    "${CMAKE_BINARY_DIR}/uiharness.png"
    400 1600 900 rich)
set(_uiharness_import_path
    "${CMAKE_BINARY_DIR}/qml:${CMAKE_BINARY_DIR}/mocks:${CMAKE_SOURCE_DIR}/tests/mocks:${CMAKE_SOURCE_DIR}/package/contents/ui")
set_tests_properties(uiharness PROPERTIES
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;ALVEUS_NO_DBUS=1;QML_IMPORT_PATH=${_uiharness_import_path};XDG_DATA_HOME=${CMAKE_BINARY_DIR}/uiharness-data;XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/uiharness-cache;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/uiharness-config"
    TIMEOUT 30)
