cmake_minimum_required(VERSION 3.21)

# Host-side test, build against a desktop/host Qt 6 (see main.cpp)
project(touchreplay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core CorePrivate Gui GuiPrivate)

set(PLUGIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

add_executable(touchreplay
    main.cpp
    ${PLUGIN_SRC}/kobotouchhandler.cpp ${PLUGIN_SRC}/kobotouchhandler.h
    ${PLUGIN_SRC}/kobokeyhandler.cpp ${PLUGIN_SRC}/kobokeyhandler.h
)
target_include_directories(touchreplay PRIVATE ${PLUGIN_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/../../FBInk)
target_compile_definitions(touchreplay PRIVATE FBINK_FOR_KOBO)
target_link_libraries(touchreplay PRIVATE Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::GuiPrivate)
