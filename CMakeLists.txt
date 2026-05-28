cmake_minimum_required(VERSION 3.24)

project(FocusOS
    VERSION 0.1.0
    DESCRIPTION "A full-screen productivity shell for deep work"
    LANGUAGES CXX
)

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
    src/core/NotesStore.cpp
    src/core/NotesStore.h
    src/core/MusicEngine.cpp
    src/core/MusicEngine.h
    src/core/InspirationStore.cpp
    src/core/InspirationStore.h
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
    src/platform/PlatformBackend.h
    src/shell/ShellWindow.cpp
    src/shell/ShellWindow.h
)

if(APPLE)
    list(APPEND FOCUSOS_SOURCES
        src/platform/macos/MacBackend.cpp
        src/platform/macos/MacBackend.h
    )
elseif(UNIX)
    list(APPEND FOCUSOS_SOURCES
        src/platform/linux/LinuxBackend.cpp
        src/platform/linux/LinuxBackend.h
        src/platform/linux/NetGate.cpp
        src/platform/linux/NetGate.h
    )
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
