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
    Concurrent
    Core
    DBus
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
    src/core/AppPaths.cpp
    src/core/AppPaths.h
    src/core/FilePicker.cpp
    src/core/FilePicker.h
    src/core/NotesStore.cpp
    src/core/NotesStore.h
    src/core/MusicEngine.cpp
    src/core/MusicEngine.h
    src/core/InspirationStore.cpp
    src/core/InspirationStore.h
    src/core/IdleMonitor.cpp
    src/core/IdleMonitor.h
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
        src/shell/StarField.qml
        src/shell/IdleScreen.qml
        src/shell/MissionView.qml
        src/shell/NotesDrawer.qml
        src/shell/UnlockModal.qml
        src/shell/FileBrowser.qml
        src/shell/ProgressOverlay.qml
    RESOURCES
        assets/qml/theme.js
        assets/music/README.txt
        assets/music/ambient_default.ogg
        assets/fonts/ShareTechMono-Regular.ttf
        assets/fonts/IBMPlexMono-Regular.ttf
        assets/fonts/IBMPlexMono-SemiBold.ttf
)

# GPU shaders (src/shell/*.frag), e.g. the starfield and CRT scanline overlay.
# The system Qt ships the `qsb` tool but not the ShaderTools CMake package, so
# compile each .frag to .qsb directly and embed the results as Qt resources at
# qrc:/qt/qml/FocusOS/<name>.frag.qsb.
find_program(QSB_TOOL
    NAMES qsb qsb6
    HINTS "${QT6_INSTALL_PREFIX}/bin" /usr/lib64/qt6/bin
    REQUIRED
)
set(FOCUSOS_SHADER_QSBS "")
foreach(shader starfield scanlines)
    set(qsb_out "${CMAKE_CURRENT_BINARY_DIR}/${shader}.frag.qsb")
    add_custom_command(
        OUTPUT "${qsb_out}"
        COMMAND "${QSB_TOOL}" --glsl "100 es,120,150" --hlsl 50 --msl 12 -b
                -o "${qsb_out}" "${CMAKE_CURRENT_SOURCE_DIR}/src/shell/${shader}.frag"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shell/${shader}.frag"
        COMMENT "Compiling ${shader}.frag -> ${shader}.frag.qsb"
        VERBATIM
    )
    list(APPEND FOCUSOS_SHADER_QSBS "${qsb_out}")
endforeach()
qt_add_resources(focusos "focusos_shaders"
    PREFIX "/qt/qml/FocusOS"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${FOCUSOS_SHADER_QSBS}
)

target_link_libraries(focusos PRIVATE
    Qt6::Concurrent
    Qt6::Core
    Qt6::DBus
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
    #   AppKit      — NSApplicationPresentationOptions kiosk mode, NSWorkspace
    #   Foundation  — NSString/NSArray bridging, NSRunLoop pumping
    #   IOKit       — IOPMAssertionCreateWithName display-sleep inhibition
    # The network lock is now pf (`pfctl`), not the Network Extension content
    # filter: pf needs only root — no Apple-granted entitlement or System
    # Extension — so it works on a self-signed research build. See
    # MacBackendNative.mm (applyNetworkFilter) and packaging/macos/README.md.
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)

    target_link_libraries(focusos PRIVATE
        ${APPKIT_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
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

if(APPLE)
    # Stable codesigning so TCC permission grants survive a rebuild.
    #
    # The default Qt/linker signature is *ad-hoc* (`codesign --sign -`), whose
    # Designated Requirement is just the binary's cdhash. macOS pins an
    # Accessibility (and Screen-Recording, etc.) grant to that exact cdhash, so
    # every `cmake --build` relinks the binary, changes the cdhash, and silently
    # invalidates the grant — AXIsProcessTrusted() flips back to false and the
    # system re-pops the "FocusOS wants to control this computer" prompt on every
    # launch and every routine engage (and the CGEventTap key blocker, which needs
    # Accessibility, goes inert — so the Mission-Control keys stop being swallowed).
    #
    # Signing with a real certificate (even a free "Apple Development" one) gives an
    # identifier+team based Designated Requirement that does NOT change across
    # rebuilds, so the grant sticks: the user authorises Accessibility once and it
    # stays authorised. We auto-detect an available identity at configure time;
    # override with -DFOCUSOS_CODESIGN_IDENTITY=... (a name or SHA-1 hash), or set it
    # to "-" to fall back to ad-hoc. NOTE: switching identity changes the binary's
    # identity once, so the FIRST launch after this change still needs a single
    # re-grant; it is stable from then on.
    if(NOT DEFINED FOCUSOS_CODESIGN_IDENTITY)
        execute_process(
            COMMAND security find-identity -v -p codesigning
            OUTPUT_VARIABLE _focusos_identities
            ERROR_QUIET)
        # Prefer a Developer ID, else an Apple Development cert; capture its hash.
        string(REGEX MATCH "([0-9A-F]+) \"Developer ID Application" _m "${_focusos_identities}")
        if(NOT _m)
            string(REGEX MATCH "([0-9A-F]+) \"Apple Development" _m "${_focusos_identities}")
        endif()
        if(_m)
            set(FOCUSOS_CODESIGN_IDENTITY "${CMAKE_MATCH_1}" CACHE STRING "codesign identity for FocusOS.app")
        else()
            set(FOCUSOS_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for FocusOS.app")
        endif()
    endif()
    if(FOCUSOS_CODESIGN_IDENTITY STREQUAL "-")
        message(WARNING "FocusOS: no codesigning identity found — signing ad-hoc. "
                        "TCC grants (Accessibility) will be invalidated on every rebuild. "
                        "Create a self-signed cert or use an Apple Development identity and "
                        "re-run cmake with -DFOCUSOS_CODESIGN_IDENTITY=<name-or-hash>.")
    else()
        message(STATUS "FocusOS codesign identity: ${FOCUSOS_CODESIGN_IDENTITY}")
    endif()
    # Re-sign the finished bundle after every build. --identifier keeps the bundle ID
    # stable regardless of the cert; no --options runtime (hardened runtime) so the
    # external Homebrew Qt frameworks still load (library validation stays off), and
    # no entitlements (the restricted Endpoint-Security entitlement needs the ad-hoc
    # SIP-off path in packaging/macos/build-and-sign.sh, which still works on top).
    add_custom_command(TARGET focusos POST_BUILD
        COMMAND /usr/bin/codesign --force
                --sign "${FOCUSOS_CODESIGN_IDENTITY}"
                --identifier com.focusos.shell
                --timestamp=none
                "$<TARGET_BUNDLE_DIR:focusos>"
        COMMENT "Codesigning FocusOS.app (stable identity → TCC grants survive rebuilds)"
        VERBATIM)
endif()
