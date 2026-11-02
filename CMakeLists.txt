cmake_minimum_required(VERSION 3.24)
project(QtSpice VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SPICE REQUIRED IMPORTED_TARGET spice-client-glib-2.0)

add_library(QtSpice src/core/qspicesession.cpp src/core/qspicesession.h src/core/qspicedisplay.cpp include/QtSpice/QSpiceDisplay.h include/QtSpice/QSpiceSession.h src/widgets/qspicewidget.cpp include/QtSpice/QSpiceWidget.h)
add_library(QtSpice::QtSpice ALIAS QtSpice)
target_include_directories(QtSpice PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_definitions(QtSpice PRIVATE QT_NO_KEYWORDS)
target_link_libraries(QtSpice PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets PRIVATE PkgConfig::SPICE)

add_executable(simple-viewer examples/simple-viewer/main.cpp)
target_link_libraries(simple-viewer PRIVATE QtSpice)

include(CTest)
if(BUILD_TESTING)
  add_executable(qspicesession_test tests/qspicesession_test.cpp)
  target_link_libraries(qspicesession_test PRIVATE QtSpice Qt6::Test)
  add_test(NAME qspicesession_test COMMAND qspicesession_test)
  add_executable(qspicedisplay_test tests/qspicedisplay_test.cpp)
  target_link_libraries(qspicedisplay_test PRIVATE QtSpice Qt6::Test)
  add_test(NAME qspicedisplay_test COMMAND qspicedisplay_test)
endif()
