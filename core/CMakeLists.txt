# The QML module is a loadable plugin that plasmashell picks up via
# "import ua.bezeza.alveus".
set(ALVEUS_CORE_SOURCES
    alveuscore.h alveuscore.cpp
    rules.h rules.cpp
    thumbnails.h thumbnails.cpp
    integrations.h integrations.cpp
    drives.h drives.cpp
    fileops.h
    ${CMAKE_SOURCE_DIR}/platform/interfaces/fileoperations.h
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND ALVEUS_CORE_SOURCES runner.h runner.cpp)
endif()

qt_add_library(alveuscoreplugin MODULE)

qt_add_qml_module(alveuscoreplugin
    URI ua.bezeza.alveus
    VERSION 1.0
    PLUGIN_TARGET alveuscoreplugin
    SOURCES ${ALVEUS_CORE_SOURCES}
)

target_include_directories(alveuscoreplugin PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(alveuscoreplugin PRIVATE ALVEUS_VERSION="${ALVEUS_DISPLAY_VERSION}")
target_link_libraries(alveuscoreplugin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick alveusplatform)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(alveuscoreplugin PRIVATE Qt6::DBus)
    install(TARGETS alveuscoreplugin
            LIBRARY DESTINATION ${ALVEUS_QML_DIR}/ua/bezeza/alveus)
else()
    install(TARGETS alveuscoreplugin
            RUNTIME DESTINATION ${ALVEUS_QML_DIR}/ua/bezeza/alveus
            LIBRARY DESTINATION ${ALVEUS_QML_DIR}/ua/bezeza/alveus)
endif()
install(FILES ${QT_QML_OUTPUT_DIRECTORY}/ua/bezeza/alveus/qmldir
              ${QT_QML_OUTPUT_DIRECTORY}/ua/bezeza/alveus/alveuscoreplugin.qmltypes
        DESTINATION ${ALVEUS_QML_DIR}/ua/bezeza/alveus)
