cmake_minimum_required(VERSION 3.16)

project(UMLgenerator VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# -----------------------------------------------------------------------------
# Автоматическая настройка путей libclang для macOS, Windows и Linux
# -----------------------------------------------------------------------------
if(APPLE)
    if(DEFINED ENV{LLVM_INSTALL_DIR})
        set(LLVM_ROOT "$ENV{LLVM_INSTALL_DIR}")
    elseif(EXISTS "/opt/homebrew/opt/llvm")
        set(LLVM_ROOT "/opt/homebrew/opt/llvm")
    else()
        set(LLVM_ROOT "/usr/local/opt/llvm")
    endif()

    set(LIBCLANG_LIBRARY "${LLVM_ROOT}/lib/libclang.dylib")
    set(LIBCLANG_INCLUDE_DIR "${LLVM_ROOT}/include")

    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
elseif(WIN32)
    if(DEFINED ENV{LLVM_INSTALL_DIR})
        set(LLVM_ROOT "$ENV{LLVM_INSTALL_DIR}")
    else()
        set(LLVM_ROOT "C:/Program Files/LLVM")
    endif()

    set(LIBCLANG_INCLUDE_DIR "${LLVM_ROOT}/include")
    find_library(LIBCLANG_LIBRARY
        NAMES libclang clang
        PATHS "${LLVM_ROOT}/lib" "${LLVM_ROOT}/bin"
        NO_DEFAULT_PATH
    )
    if(NOT LIBCLANG_LIBRARY)
        set(LIBCLANG_LIBRARY "${LLVM_ROOT}/lib/libclang.lib")
    endif()
elseif(UNIX)
    find_library(LIBCLANG_LIBRARY
        NAMES clang libclang
        PATHS "$ENV{LLVM_INSTALL_DIR}/lib" /usr/lib /usr/lib/llvm-18/lib /usr/lib/llvm-17/lib /usr/lib/llvm-16/lib /usr/lib/x86_64-linux-gnu
    )
    find_path(LIBCLANG_INCLUDE_DIR
        NAMES clang-c/Index.h
        PATHS "$ENV{LLVM_INSTALL_DIR}/include" /usr/include /usr/lib/llvm-18/include /usr/lib/llvm-17/include /usr/lib/llvm-16/include
    )
endif()

set(PROJECT_SOURCES
    src/main.cpp
    ui/mainwindow.cpp
    ui/mainwindow.h
    ui/mainwindow.ui
    model/uml_class.h
    model/uml_class.cpp
    model/uml_relationship.h
    model/uml_relationship.cpp
    model/uml_diagram.h
    model/uml_diagram.cpp
    diagramview.h
    diagramview.cpp
    parser/cpp_parser.h
    parser/cpp_parser.cpp
)

qt_add_executable(UMLgenerator
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_link_libraries(UMLgenerator PRIVATE
    Qt6::Widgets
    "${LIBCLANG_LIBRARY}"
)

target_include_directories(UMLgenerator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    "${LIBCLANG_INCLUDE_DIR}"
)

if(APPLE AND MACOS_SDK_PATH)
    target_compile_definitions(UMLgenerator PRIVATE
        UML_MACOS_SDK_PATH="${MACOS_SDK_PATH}"
    )
endif()

set_target_properties(UMLgenerator PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

if(APPLE)
    set_target_properties(UMLgenerator PROPERTIES BUILD_RPATH "${LLVM_ROOT}/lib")
endif()

if(WIN32 AND EXISTS "${LLVM_ROOT}/bin/libclang.dll")
    add_custom_command(TARGET UMLgenerator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LLVM_ROOT}/bin/libclang.dll"
            "$<TARGET_FILE_DIR:UMLgenerator>/libclang.dll"
    )
endif()

include(GNUInstallDirs)
install(TARGETS UMLgenerator
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(UMLgenerator)