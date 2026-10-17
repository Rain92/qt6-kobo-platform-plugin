cmake_minimum_required(VERSION 3.21)

project(koboplatformplugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt 6 only, use koboplatformplugin.pro for Qt 5
find_package(Qt6 REQUIRED COMPONENTS Core CorePrivate Gui GuiPrivate FbSupportPrivate)

qt_add_plugin(kobo
    SHARED
    PLUGIN_TYPE platforms
    CLASS_NAME KoboPlatformIntegrationPlugin
    src/main.cpp
    src/dither.cpp src/dither.h
    src/einkenums.h
    src/koboinputmanager.cpp src/koboinputmanager.h
    src/kobokeyhandler.cpp src/kobokeyhandler.h
    src/kobotouchhandler.cpp src/kobotouchhandler.h
    src/kobodevicedescriptor.cpp src/kobodevicedescriptor.h
    src/kobofbscreen.cpp src/kobofbscreen.h
    src/koboplatformadditions.cpp src/koboplatformadditions.h
    src/koboplatformfunctions.h
    src/koboplatformintegration.cpp src/koboplatformintegration.h
    src/kobowifimanager.cpp src/kobowifimanager.h
    Resources.qrc
)

target_include_directories(kobo PRIVATE src)
target_compile_definitions(kobo PRIVATE QT_NO_FOREACH)
target_compile_options(kobo PRIVATE -Wno-missing-field-initializers)
set_target_properties(kobo PROPERTIES INSTALL_RPATH "$ORIGIN/../../lib")

target_link_libraries(kobo PRIVATE
    Qt6::Core Qt6::CorePrivate
    Qt6::Gui Qt6::GuiPrivate
    Qt6::FbSupportPrivate
)

# FBInk, built as a static PIC archive by its own Makefile (contains libi2c and fbink_input_scan).
set(FBINK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/FBInk)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FBINK_OUT_DIR Debug)
    set(FBINK_MAKE_FLAGS DEBUG=1)
else()
    set(FBINK_OUT_DIR Release)
    set(FBINK_MAKE_FLAGS)
endif()
set(FBINK_LIB ${FBINK_DIR}/${FBINK_OUT_DIR}/libfbink.a)
# e.g. /path/to/arm-kobo-linux-gnueabihf-gcc -> /path/to/arm-kobo-linux-gnueabihf-
string(REGEX REPLACE "gcc$" "" FBINK_CROSS_COMPILE "${CMAKE_C_COMPILER}")

add_custom_target(fbink
    COMMAND make -C ${FBINK_DIR} CROSS_COMPILE=${FBINK_CROSS_COMPILE} MINIMAL=1 DRAW=1 INPUT=1 KOBO=true ${FBINK_MAKE_FLAGS} pic
    BYPRODUCTS ${FBINK_LIB}
    USES_TERMINAL
    COMMENT "Building FBInk"
)
add_dependencies(kobo fbink)

target_compile_definitions(kobo PRIVATE FBINK_FOR_KOBO)
target_include_directories(kobo PRIVATE ${FBINK_DIR})
target_link_libraries(kobo PRIVATE ${FBINK_LIB})

install(TARGETS kobo LIBRARY DESTINATION plugins/platforms)
