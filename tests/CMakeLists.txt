find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(platformfileoperationstest platformfileoperationstest.cpp)
target_link_libraries(platformfileoperationstest PRIVATE Qt6::Core Qt6::Test alveusplatform)
add_test(NAME platformfileoperationstest COMMAND platformfileoperationstest)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    set(_core ${CMAKE_SOURCE_DIR}/core)
    add_executable(coretest coretest.cpp
        ${_core}/alveuscore.cpp ${_core}/alveuscore.h ${_core}/rules.cpp ${_core}/rules.h
        ${_core}/thumbnails.cpp ${_core}/thumbnails.h ${_core}/integrations.cpp ${_core}/integrations.h
        ${_core}/runner.cpp ${_core}/runner.h ${_core}/drives.cpp ${_core}/drives.h
        ${_core}/fileops.h)
    target_include_directories(coretest PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(coretest PRIVATE ALVEUS_VERSION="${ALVEUS_DISPLAY_VERSION}")
    target_link_libraries(coretest PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus Qt6::Test alveusplatform)
    add_test(NAME coretest COMMAND coretest)
    set_tests_properties(coretest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(uiharness uiharness.cpp)
target_link_libraries(uiharness PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
add_subdirectory(mockplasmoid)
