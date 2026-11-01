qt_add_library(mockplasmoid MODULE)
qt_add_qml_module(mockplasmoid
    URI org.kde.plasma.plasmoid
    VERSION 1.0
    PLUGIN_TARGET mockplasmoid
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mocks/org/kde/plasma/plasmoid
    SOURCES mockplasmoid.h mockplasmoid.cpp
    QML_FILES ContainmentItem.qml
)
target_link_libraries(mockplasmoid PRIVATE Qt6::Core Qt6::Qml Qt6::Quick)
