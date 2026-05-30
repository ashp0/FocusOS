cmake_minimum_required(VERSION 3.24)

project(FocusOS
    VERSION 0.1.0
    DESCRIPTION "A full-screen productivity shell for deep work"
    LANGUAGES CXX
)

# The macOS backend isolates Apple framework calls in an Objective-C++ shim
# (MacBackendNative.mm). Enable the OBJCXX language so CMake compiles it.
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Build Apple Silicon by default" FORCE)
endif()

find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core
    Gui
    Multimedia
    Network
    Qml
    Quick
    QuickControls2
    Test
    Widgets
)

qt_standard_project_setup(REQUIRES 6.7)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

set(FOCUSOS_SOURCES
    src/main.cpp
    src/blocker/BlockerPolicy.cpp
    src/blocker/BlockerPolicy.h
    src/blocker/BlockerHost.cpp
    src/blocker/BlockerHost.h
    src/core/NotesStore.cpp
    src/core/NotesStore.h
    src/core/MusicEngine.cpp
    src/core/MusicEngine.h
    src/core/InspirationStore.cpp
    src/core/InspirationStore.h
    src/core/MediaKeys.cpp
    src/core/MediaKeys.h
    src/core/RoutineManager.cpp
    src/core/RoutineManager.h
    src/core/StatsStore.cpp
    src/core/StatsStore.h
    src/core/SystemStatus.cpp
    src/core/SystemStatus.h
    src/core/TOTPEngine.cpp
    src/core/TOTPEngine.h
    src/core/Timer.cpp
    src/core/Timer.h
    src/core/Updater.cpp
    src/core/Updater.h
    src/platform/PlatformBackend.h
    src/shell/ShellWindow.cpp
    src/shell/ShellWindow.h
)

if(APPLE)
    list(APPEND FOCUSOS_SOURCES
        src/platform/macos/MacBackend.cpp
        src/platform/macos/MacBackend.h
        src/platform/macos/MacBackendNative.h
        src/platform/macos/MacBackendNative.mm
    )
    # MacBackendNative.mm is written with ARC ownership semantics (no manual
    # retain/release, `alloc init` without a balancing release, `__block`
    # object captures expected to be retained). Without -fobjc-arc it compiles
    # under MRC, where a captured NSError* is not retained and gets deallocated
    # when the run-loop's autorelease pool drains — leaving a dangling pointer
    # that crashes when the error message is later read. Compile it under ARC.
    set_source_files_properties(
        src/platform/macos/MacBackendNative.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
elseif(UNIX)
    list(APPEND FOCUSOS_SOURCES
        src/platform/linux/LinuxBackend.cpp
        src/platform/linux/LinuxBackend.h
        src/platform/linux/NetGate.cpp
        src/platform/linux/NetGate.h
    )
endif()

# KF6GlobalAccel lets FocusOS claim the volume/brightness media keys session-
# wide (see src/core/MediaKeys.cpp). It is optional: without it the keys still
# work while FocusOS itself is focused, just not over a routine app. Only worth
# probing on Linux (KWin/Wayland); the macOS build never needs it.
if(UNIX AND NOT APPLE)
    find_package(KF6GlobalAccel QUIET)
endif()

qt_add_executable(focusos
    MACOSX_BUNDLE
    ${FOCUSOS_SOURCES}
)

target_include_directories(focusos PRIVATE src)

qt_add_qml_module(focusos
    URI FocusOS
    VERSION 1.0
    QML_FILES
        src/shell/Main.qml
        src/shell/ActivitiesPanel.qml
        src/shell/InfoPanel.qml
        src/shell/AmbientLayer.qml
        src/shell/MissionView.qml
        src/shell/NotesDrawer.qml
        src/shell/UnlockModal.qml
        src/shell/ProgressOverlay.qml
    RESOURCES
        assets/qml/theme.js
        assets/music/README.txt
        assets/music/ambient_default.ogg
        assets/fonts/ShareTechMono-Regular.ttf
        assets/fonts/IBMPlexMono-Regular.ttf
        assets/fonts/IBMPlexMono-SemiBold.ttf
)

target_link_libraries(focusos PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)

if(KF6GlobalAccel_FOUND)
    target_link_libraries(focusos PRIVATE KF6::GlobalAccel)
    target_compile_definitions(focusos PRIVATE FOCUSOS_HAS_KGLOBALACCEL=1)
    message(STATUS "KF6GlobalAccel found — global volume/brightness media keys enabled")
else()
    message(STATUS "KF6GlobalAccel not found — media keys limited to when FocusOS is focused")
endif()

if(APPLE)
    # Apple frameworks backing the native macOS shim:
    #   AppKit            — NSApplicationPresentationOptions kiosk mode, NSWorkspace
    #   Foundation        — NSString/NSArray bridging, NSRunLoop pumping
    #   IOKit             — IOPMAssertionCreateWithName display-sleep inhibition
    #   NetworkExtension  — NEFilterManager content-filter network policy
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(NETWORK_EXTENSION_FRAMEWORK NetworkExtension REQUIRED)

    target_link_libraries(focusos PRIVATE
        ${APPKIT_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${NETWORK_EXTENSION_FRAMEWORK}
    )

    # EndpointSecurity backs the AUTH_EXEC process blocker. It is only usable in
    # a signed build carrying com.apple.developer.endpoint-security.client, but
    # we link + compile it whenever the SDK ships the framework so the code path
    # exists; MacBackendNative.mm degrades to a clear error when it's absent.
    find_library(ENDPOINT_SECURITY_FRAMEWORK EndpointSecurity)
    if(ENDPOINT_SECURITY_FRAMEWORK)
        target_link_libraries(focusos PRIVATE ${ENDPOINT_SECURITY_FRAMEWORK})
        target_compile_definitions(focusos PRIVATE FOCUSOS_HAS_ENDPOINT_SECURITY=1)
    else()
        message(STATUS "EndpointSecurity framework not found — AUTH_EXEC process blocking disabled in this build")
    endif()
endif()

enable_testing()

qt_add_executable(totp_tests
    tests/totp_tests.cpp
    src/core/TOTPEngine.cpp
    src/core/TOTPEngine.h
)

target_include_directories(totp_tests PRIVATE src)
target_link_libraries(totp_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME totp_tests COMMAND totp_tests)

set_target_properties(focusos PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.focusos.shell
    MACOSX_BUNDLE_BUNDLE_NAME FocusOS
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
)
