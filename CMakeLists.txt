
cmake_minimum_required(VERSION 3.16)

project(UMLgenerator VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# -----------------------------------------------------------------------------
# Поиск libclang для macOS, Windows и Linux
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

    if(NOT EXISTS "${LIBCLANG_LIBRARY}")
        message(FATAL_ERROR
            "Не найдена libclang: ${LIBCLANG_LIBRARY}")
    endif()

    if(NOT EXISTS "${LIBCLANG_INCLUDE_DIR}/clang-c/Index.h")
        message(FATAL_ERROR
            "Не найден заголовок clang-c/Index.h")
    endif()

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

    find_path(LIBCLANG_INCLUDE_DIR
        NAMES clang-c/Index.h
        PATHS
            "${LLVM_ROOT}/include"
        NO_DEFAULT_PATH
    )

    find_library(LIBCLANG_LIBRARY
        NAMES libclang clang
        PATHS
            "${LLVM_ROOT}/lib"
            "${LLVM_ROOT}/bin"
        NO_DEFAULT_PATH
    )

    if(NOT LIBCLANG_INCLUDE_DIR)
        message(FATAL_ERROR
            "Не найдены заголовки libclang. Проверь установку LLVM.")
    endif()

    if(NOT LIBCLANG_LIBRARY)
        message(FATAL_ERROR
            "Не найдена библиотека libclang. Проверь установку LLVM.")
    endif()

elseif(UNIX)

    find_library(LIBCLANG_LIBRARY
        NAMES clang libclang
        PATHS
            "$ENV{LLVM_INSTALL_DIR}/lib"
            /usr/lib
            /usr/lib/llvm-18/lib
            /usr/lib/llvm-17/lib
            /usr/lib/llvm-16/lib
            /usr/lib/llvm-15/lib
            /usr/lib/llvm-14/lib
            /usr/lib/x86_64-linux-gnu
            /usr/lib/aarch64-linux-gnu
    )

    find_path(LIBCLANG_INCLUDE_DIR
        NAMES clang-c/Index.h
        PATHS
            "$ENV{LLVM_INSTALL_DIR}/include"
            /usr/include
            /usr/lib/llvm-18/include
            /usr/lib/llvm-17/include
            /usr/lib/llvm-16/include
            /usr/lib/llvm-15/include
            /usr/lib/llvm-14/include
    )

    if(NOT LIBCLANG_LIBRARY)
        message(FATAL_ERROR
            "Не найдена библиотека libclang. "
            "Установи пакет libclang-dev.")
    endif()

    if(NOT LIBCLANG_INCLUDE_DIR)
        message(FATAL_ERROR
            "Не найден clang-c/Index.h. "
            "Установи пакет libclang-dev.")
    endif()

endif()

message(STATUS "libclang library: ${LIBCLANG_LIBRARY}")
message(STATUS "libclang include: ${LIBCLANG_INCLUDE_DIR}")

# -----------------------------------------------------------------------------
# Исходные файлы проекта
# -----------------------------------------------------------------------------

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

# -----------------------------------------------------------------------------
# Создание приложения
# -----------------------------------------------------------------------------

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

# -----------------------------------------------------------------------------
# Дополнительные настройки macOS
# -----------------------------------------------------------------------------

if(APPLE AND MACOS_SDK_PATH)

    target_compile_definitions(UMLgenerator PRIVATE
        UML_MACOS_SDK_PATH="${MACOS_SDK_PATH}"
    )

endif()

# -----------------------------------------------------------------------------
# Настройки приложения для разных платформ
# -----------------------------------------------------------------------------

set_target_properties(UMLgenerator PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING
        ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

if(APPLE)
    set_target_properties(UMLgenerator PROPERTIES
        BUILD_RPATH "${LLVM_ROOT}/lib"
    )
endif()

# -----------------------------------------------------------------------------
# Копирование libclang.dll в папку приложения на Windows
# -----------------------------------------------------------------------------

if(WIN32)

    add_custom_command(TARGET UMLgenerator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBCLANG_LIBRARY}"
            "$<TARGET_FILE_DIR:UMLgenerator>/libclang.dll"
    )

endif()

# -----------------------------------------------------------------------------
# Установка
# -----------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS UMLgenerator
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(UMLgenerator)
