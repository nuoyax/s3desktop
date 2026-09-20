cmake_minimum_required(VERSION 3.21)

project(s3desktop
    VERSION 0.1.0
    DESCRIPTION "A desktop browser for S3-compatible object storage"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(S3DESKTOP_BUILD_TESTS "Build unit tests" ON)

# Static linking is opt-in rather than inferred from the Qt build: a static Qt
# install is a separate prefix, and guessing from Qt6Core_LIBRARIES would make
# the build silently change character when someone points CMAKE_PREFIX_PATH at a
# different Qt. Off by default so the shipped Qt works out of the box.
option(S3DESKTOP_STATIC "Link Qt statically; requires a static Qt build" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# ---------------------------------------------------------------------------
# Core library: S3 transport, config, credentials, transfers.
# Kept separate from the GUI so tests can link it without a display.
# ---------------------------------------------------------------------------
add_library(s3desktop_core STATIC
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

target_include_directories(s3desktop_core PUBLIC src)
target_link_libraries(s3desktop_core PUBLIC Qt6::Core Qt6::Network)

# DPAPI lives in Crypt32; linking it here rather than at the executable keeps
# the dependency attached to the code that actually calls CryptProtectData.
if(WIN32)
    target_link_libraries(s3desktop_core PUBLIC crypt32)
endif()

# ---------------------------------------------------------------------------
# GUI library
#
# A library rather than sources compiled straight into the executable, so the
# widget-level logic — the object model, the theme — can be tested without
# launching the whole window. Everything below needs Widgets.
# ---------------------------------------------------------------------------
add_library(s3desktop_ui STATIC
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

target_link_libraries(s3desktop_ui PUBLIC
    s3desktop_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# ---------------------------------------------------------------------------
# GUI executable
# ---------------------------------------------------------------------------
add_executable(s3desktop WIN32
    src/main.cpp
    assets/resources.qrc
)

target_link_libraries(s3desktop PRIVATE s3desktop_ui)

# ---------------------------------------------------------------------------
# Static Qt
# ---------------------------------------------------------------------------
if(S3DESKTOP_STATIC)
    # A static Qt links no plugin by default, so the platform, style, TLS and
    # image-format plugins have to be named explicitly. Without the windows
    # plugin the executable builds cleanly and then dies at startup with "this
    # application failed to start because no Qt platform plugin could be
    # initialized" — the failure is at runtime, so it is worth stating here
    # rather than leaving it to be rediscovered.
    #
    # These are not optional for this application: it is a GUI app (platform),
    # it talks to an https endpoint (TLS), and it displays PNG/ICO assets
    # (imageformats).
    qt_import_plugins(s3desktop
        INCLUDE_BY_TYPE
            platforms Qt6::QWindowsIntegrationPlugin
            styles Qt6::QModernWindowsStylePlugin
            tls Qt6::QSchannelBackendPlugin
            imageformats Qt6::QICOPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
    )

    if(MINGW)
        # -static also binds libgcc, libstdc++ and libwinpthread, so the result
        # does not depend on the toolchain's runtime DLLs being on PATH either.
        target_link_options(s3desktop PRIVATE -static)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(S3DESKTOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
