cmake_minimum_required(VERSION 3.24)
project(PDFtoJPGQt VERSION 1.1.0 LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Pdf)
qt_standard_project_setup()
qt_add_executable(PDF-to-JPG_QT WIN32 main.cpp converter.cpp converter.h resume.h resources.qrc app.rc)
target_link_libraries(PDF-to-JPG_QT PRIVATE Qt6::Widgets Qt6::Pdf)
# 版本號唯一來源是 project(VERSION)；C++ 與 app.rc 都由這裡取得
target_compile_definitions(PDF-to-JPG_QT PRIVATE
 APP_VERSION_STR="${PROJECT_VERSION}"
 APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
 APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
 APP_VERSION_PATCH=${PROJECT_VERSION_PATCH})
target_compile_options(PDF-to-JPG_QT PRIVATE /utf-8 /W4 /EHsc)
target_link_options(PDF-to-JPG_QT PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
install(TARGETS PDF-to-JPG_QT RUNTIME DESTINATION .)

option(PDF2JPG_TESTING "Enable diagnostic hooks" OFF)
if(PDF2JPG_TESTING)
 target_compile_definitions(PDF-to-JPG_QT PRIVATE PDF2JPG_TESTING)
endif()
if(PDF2JPG_TESTING)
 qt_add_executable(PDF-to-JPG-engine-tests tests/engine_tests.cpp converter.h)
 target_include_directories(PDF-to-JPG-engine-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
 target_link_libraries(PDF-to-JPG-engine-tests PRIVATE Qt6::Widgets Qt6::Pdf)
 target_compile_definitions(PDF-to-JPG-engine-tests PRIVATE PDF2JPG_TESTING)
 target_compile_options(PDF-to-JPG-engine-tests PRIVATE /utf-8 /W4 /EHsc)
endif()
