cmake_minimum_required(VERSION 3.9)

project(jomt LANGUAGES CXX VERSION "0.0.0")

include(GNUInstallDirs)
include(cmake/use_ccache.cmake)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
  set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

# common build and install options
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt stuff
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)


if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
  add_compile_options(/W3)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG /DQT_NO_DEBUG_OUTPUT")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DQT_NO_DEBUG_OUTPUT -march=native -mtune=native -Wall -Wextra -pedantic")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -march=native -mtune=native -Wall -Wextra -pedantic")
endif()

find_package(Qt5 COMPONENTS Core Widgets Charts DataVisualization REQUIRED)
  
set(JOMT_SOURCE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(JOMT_RESOURCES
  ${JOMT_SOURCE_DIR}/resources/jomt.qrc)  
  
set(JOMT_FORMS
  ${JOMT_SOURCE_DIR}/ui/mainwindow.ui
  ${JOMT_SOURCE_DIR}/ui/result_selector.ui
  ${JOMT_SOURCE_DIR}/ui/plotter_linechart.ui
  ${JOMT_SOURCE_DIR}/ui/plotter_barchart.ui
  ${JOMT_SOURCE_DIR}/ui/plotter_boxchart.ui
  ${JOMT_SOURCE_DIR}/ui/plotter_3dbars.ui
  ${JOMT_SOURCE_DIR}/ui/plotter_3dsurface.ui
  ${JOMT_SOURCE_DIR}/ui/series_dialog.ui)

set(JOMT_SOURCES
  ${JOMT_SOURCE_DIR}/main.cpp
  ${JOMT_SOURCE_DIR}/mainwindow.cpp
  ${JOMT_SOURCE_DIR}/benchmark_results.cpp
  ${JOMT_SOURCE_DIR}/result_parser.cpp
  ${JOMT_SOURCE_DIR}/plot_parameters.cpp
  ${JOMT_SOURCE_DIR}/commandline_handler.cpp
  ${JOMT_SOURCE_DIR}/result_selector.cpp
  ${JOMT_SOURCE_DIR}/plotter_linechart.cpp  
  ${JOMT_SOURCE_DIR}/plotter_barchart.cpp
  ${JOMT_SOURCE_DIR}/plotter_boxchart.cpp
  ${JOMT_SOURCE_DIR}/plotter_3dbars.cpp
  ${JOMT_SOURCE_DIR}/plotter_3dsurface.cpp
  ${JOMT_SOURCE_DIR}/series_dialog.cpp)

set(JOMT_HEADERS
  ${JOMT_SOURCE_DIR}/include/mainwindow.h
  ${JOMT_SOURCE_DIR}/include/benchmark_results.h
  ${JOMT_SOURCE_DIR}/include/result_parser.h
  ${JOMT_SOURCE_DIR}/include/plot_parameters.h
  ${JOMT_SOURCE_DIR}/include/commandline_handler.h
  ${JOMT_SOURCE_DIR}/include/result_selector.h
  ${JOMT_SOURCE_DIR}/include/plotter_linechart.h
  ${JOMT_SOURCE_DIR}/include/plotter_barchart.h
  ${JOMT_SOURCE_DIR}/include/plotter_boxchart.h
  ${JOMT_SOURCE_DIR}/include/plotter_3dbars.h
  ${JOMT_SOURCE_DIR}/include/plotter_3dsurface.h
  ${JOMT_SOURCE_DIR}/include/series_dialog.h
)

set(CMAKE_AUTOUIC_SEARCH_PATHS ${JOMT_SOURCE_DIR}/ui)


qt_wrap_cpp(JOMT_SOURCES ${JOMT_HEADERS})
qt_wrap_ui(JOMT_SOURCES ${JOMT_FORMS})

add_executable(${PROJECT_NAME}
  ${JOMT_SOURCES}
  ${JOMT_RESOURCES})
  
target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${JOMT_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME}
  Qt5::Core
  Qt5::Widgets
  Qt5::Charts
  Qt5::DataVisualization)
  
include(cmake/colored_output.cmake)  

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
