cmake_minimum_required(VERSION 3.21)

project(us3qt
    VERSION 0.1.0
    DESCRIPTION "Universal S3 UI - Qt edition"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(US3QT_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# ---------------------------------------------------------------------------
# Core library: S3 transport, config, credentials, transfers.
# Kept separate from the GUI so tests can link it without a display.
# ---------------------------------------------------------------------------
add_library(us3qt_core STATIC
    src/core/Log.cpp
    src/core/Log.h
    src/core/SigV4.cpp
    src/core/SigV4.h
    src/core/S3Types.h
    src/core/S3Error.cpp
    src/core/S3Error.h
    src/core/S3Client.cpp
    src/core/S3Client.h
    src/core/S3Config.cpp
    src/core/S3Config.h
    src/core/CredentialStore.cpp
    src/core/CredentialStore.h
    src/core/ConnectionStore.cpp
    src/core/ConnectionStore.h
    src/core/TransferQueue.cpp
    src/core/TransferQueue.h
    src/compat/TargetProfile.cpp
    src/compat/TargetProfile.h
)

target_include_directories(us3qt_core PUBLIC src)
target_link_libraries(us3qt_core PUBLIC Qt6::Core Qt6::Network)

# DPAPI lives in Crypt32; linking it here rather than at the executable keeps
# the dependency attached to the code that actually calls CryptProtectData.
if(WIN32)
    target_link_libraries(us3qt_core PUBLIC crypt32)
endif()

# ---------------------------------------------------------------------------
# GUI library
#
# A library rather than sources compiled straight into the executable, so the
# widget-level logic — the object model, the theme — can be tested without
# launching the whole window. Everything below needs Widgets.
# ---------------------------------------------------------------------------
add_library(us3qt_ui STATIC
    src/ui/Theme.cpp
    src/ui/Theme.h
    src/ui/ObjectModel.cpp
    src/ui/ObjectModel.h
    src/ui/ObjectBrowser.cpp
    src/ui/ObjectBrowser.h
    src/ui/ConnectDialog.cpp
    src/ui/ConnectDialog.h
    src/ui/BucketManager.cpp
    src/ui/BucketManager.h
    src/ui/TransferPanel.cpp
    src/ui/TransferPanel.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

target_link_libraries(us3qt_ui PUBLIC
    us3qt_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# ---------------------------------------------------------------------------
# GUI executable
# ---------------------------------------------------------------------------
add_executable(us3qt WIN32
    src/main.cpp
    assets/resources.qrc
)

target_link_libraries(us3qt PRIVATE us3qt_ui)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(US3QT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
