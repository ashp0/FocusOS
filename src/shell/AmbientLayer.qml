import QtQuick
import QtQuick.Window
import QtMultimedia
import "qrc:/qt/qml/FocusOS/assets/qml/theme.js" as Theme

Item {
    id: root

    property var inspirationAssets: inspirationStore.assets
    property int assetIndex: -1
    property real mediaOpacityMax: 0.85
    property real mediaOpacityMin: 0.04
    property int fadeDurationMs: 30 * 60 * 1000  // 30 minutes
    property real fadeProgress: 0                 // 0 = full, 1 = nearly gone
    property real mediaOpacity: mediaOpacityMax - (mediaOpacityMax - mediaOpacityMin) * fadeProgress
    property real mediaTransitionFactor: 0    // 0..1; animated during crossfade
    property bool showBackground: true
    property bool showMedia: true
    property bool showStars: true
    property bool showDust: true       // background layer paints dust; overlay layer turns it off
    property bool showCircles: false   // legacy slot — circles removed for space feel
    property real circleOpacityScale: 0
    property real starOpacityScale: 1
    // Halt the starfield's fly-through motion (keep twinkle) while a routine is
    // paused — forwarded to the shared StarField.
    property bool paused: false
    // Star-count multiplier passed to the shared StarField (the thin overlay
    // field over the panels runs at a lower density to save power).
    property real starDensity: 1
    property int imageHoldMs: 10000
    // Fallback hold before rotating off a video when its real duration isn't
    // reported yet (onDurationChanged refines this to one full play-through).
    property int videoFallbackHoldMs: 45000
    property int activeAssetType: 0    // 0=image, 1=video
    // Bound to the persisted cycle anchor: resumes after a relaunch, and updates
    // whenever resetFadeCycle() rewrites it (task start / logout).
    property double fadeStartTime: inspirationStore.fadeStartMs

    function resetFade() {
        // Persist a fresh start time; fadeStartTime is bound to it and updates,
        // then recompute progress immediately so the media snaps back to full.
        inspirationStore.resetFadeCycle()
        tickFade()
        fadeTicker.restart()
    }

    function tickFade() {
        const elapsed = Date.now() - fadeStartTime
        const t = Math.max(0, Math.min(1, elapsed / fadeDurationMs))
        fadeProgress = t
    }

    // The fade is derived from wall-clock time, so there's no need to tick while
    // this layer can't be seen (app unfocused, or the panel asleep). Pause then,
    // and recompute once on the way back so the opacity snaps to the right value.
    property bool canBeSeen: Qt.application.active && root.visible && root.opacity > 0.01
    onCanBeSeenChanged: if (canBeSeen) tickFade()

    // Whether the wallpaper video should keep decoding. We pause it only when the
    // window is genuinely off-screen (minimised / hidden / display asleep) — NOT
    // merely unfocused. On the bare kwin_wayland session `Qt.application.active`
    // is unreliable (the shell often never registers as "active"), so gating the
    // decode on focus, as an earlier power tweak did, left the home-screen
    // wallpaper frozen on its first frame. Window visibility is dependable.
    property bool windowVisible: Window.window
        ? (Window.window.visibility !== Window.Minimized
           && Window.window.visibility !== Window.Hidden)
        : true
    property bool videoShouldDecode: showMedia && activeAssetType === 1
                                     && windowVisible && currentAssetUrl().length > 0
    onVideoShouldDecodeChanged: {
        // A playing MediaPlayer keeps the FFmpeg pipeline decoding frames even
        // while the panel is off-screen — pure wasted CPU/power. Pause then,
        // resume when the window comes back.
        if (videoShouldDecode) {
            videoPlayer.play()
        } else if (activeAssetType === 1) {
            videoPlayer.pause()
        }
    }

    Timer {
        id: fadeTicker
        interval: 1000      // update opacity every second
        running: root.canBeSeen
        repeat: true
        onTriggered: root.tickFade()
    }

    Connections {
        target: routineManager
        function onActiveChanged() {
            // Starting a task resets the media to fully visible and restarts the
            // 30-minute fade. Ending a task leaves the cycle running; it next
            // resets on the following task start or on logout / return to login.
            if (routineManager.active) {
                root.resetFade()
            }
        }
    }

    function boundedAssetIndex() {
        if (!inspirationAssets || inspirationAssets.length <= 0) {
            return -1
        }
        if (assetIndex < 0 || assetIndex >= inspirationAssets.length) {
            return 0
        }
        return assetIndex
    }

    function currentAsset() {
        const index = boundedAssetIndex()
        return index >= 0 ? inspirationAssets[index] : null
    }

    function currentAssetUrl() {
        const asset = currentAsset()
        return asset ? String(asset.url || "") : ""
    }

    function currentAssetType() {
        const asset = currentAsset()
        return asset ? String(asset.type || "") : ""
    }

    function pickRandomIndex() {
        if (!inspirationAssets || inspirationAssets.length <= 0) {
            return -1
        }
        if (inspirationAssets.length === 1) {
            return 0
        }
        let next = Math.floor(Math.random() * inspirationAssets.length)
        if (next === assetIndex) {
            next = (next + 1) % inspirationAssets.length
        }
        return next
    }

    function startAsset(index) {
        if (index < 0 || index >= inspirationAssets.length) {
            videoPlayer.stop()
            mediaTransitionFactor = 0
            return
        }
        assetIndex = index
        slideTimer.stop()
        const type = currentAssetType()
        if (type === "video") {
            activeAssetType = 1
            // Clear and re-set the source so Qt re-loads even if the URL
            // is unchanged (single-video looping case).
            videoPlayer.stop()
            videoPlayer.source = ""
            videoPlayer.source = currentAssetUrl()
            videoPlayer.play()
            // A lone video loops forever at the player level (see loops below);
            // with several assets, schedule a hand-off. Seed it with a fallback
            // hold now and let onDurationChanged refine it to one play-through
            // once the real duration is known.
            if (inspirationAssets.length > 1) {
                slideTimer.interval = videoFallbackHoldMs
                slideTimer.restart()
            }
        } else {
            activeAssetType = 0
            videoPlayer.stop()
            slideTimer.interval = imageHoldMs
            slideTimer.restart()
        }
        mediaTransitionFactor = 1
    }

    function advance() {
        const next = pickNextIndex()
        if (next < 0) {
            videoPlayer.stop()
            mediaTransitionFactor = 0
            return
        }
        crossfade.beginTo(next)
    }

    function pickNextIndex() {
        if (!inspirationAssets || inspirationAssets.length <= 0) {
            return -1
        }
        if (inspirationAssets.length === 1) {
            return 0
        }
        return pickRandomIndex()
    }

    function syncMediaLayer() {
        // Layers that don't paint media (the thin star overlay, or the backdrop
        // while a routine is active) must not run the video pipeline — a hidden
        // MediaPlayer still decodes frames and burns CPU/GPU for nothing.
        if (!showMedia || !inspirationAssets || inspirationAssets.length <= 0) {
            assetIndex = -1
            videoPlayer.stop()
            mediaTransitionFactor = 0
            slideTimer.stop()
            return
        }
        if (assetIndex < 0 || assetIndex >= inspirationAssets.length) {
            startAsset(Math.floor(Math.random() * inspirationAssets.length))
        } else {
            startAsset(assetIndex)
        }
    }

    Rectangle {
        id: background
        anchors.fill: parent
        visible: root.showBackground
        color: "#04050a"

        // subtle radial nebula — fixed, no breathing circles
        Rectangle {
            anchors.centerIn: parent
            width: Math.max(parent.width, parent.height) * 1.4
            height: width
            radius: width / 2
            opacity: 0.18
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: "#0b1428" }
                GradientStop { position: 0.55; color: "#04060d" }
                GradientStop { position: 1.0; color: "#020205" }
            }
        }
    }

    Item {
        id: mediaLayer
        anchors.fill: parent
        visible: root.showMedia
        opacity: root.showMedia ? root.mediaOpacity * root.mediaTransitionFactor : 0

        Image {
            anchors.fill: parent
            visible: root.activeAssetType === 0 && root.currentAssetType() === "image"
            source: visible ? root.currentAssetUrl() : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            mipmap: true
        }

        VideoOutput {
            id: videoOutput
            anchors.fill: parent
            visible: root.activeAssetType === 1 && root.currentAssetType() === "video"
            fillMode: VideoOutput.PreserveAspectCrop
        }

        MediaPlayer {
            id: videoPlayer
            videoOutput: videoOutput
            // Inspiration videos are ambient wallpaper: they always play silently.
            // A muted AudioOutput keeps the FFmpeg backend from stalling on a
            // missing/idle audio sink (the bare kwin session has no audio daemon
            // at first) while guaranteeing no sound.
            audioOutput: AudioOutput {
                muted: true
                volume: 0
            }
            // Loop at the player level so a single video runs continuously instead
            // of freezing on its last frame. Rotation between multiple assets is
            // driven by slideTimer (seeded in startAsset, refined below), since
            // EndOfMedia never fires with infinite looping.
            loops: MediaPlayer.Infinite

            onDurationChanged: {
                // Reference the player's own duration property rather than the
                // injected signal parameter (the latter is deprecated in Qt 6 QML).
                if (root.activeAssetType === 1 && videoPlayer.duration > 0
                        && root.inspirationAssets && root.inspirationAssets.length > 1) {
                    slideTimer.interval = Math.max(videoPlayer.duration, root.imageHoldMs)
                    slideTimer.restart()
                }
            }
        }
    }

    // Deep-space starfield with the fly-through motion. Shared with the idle
    // screen via the StarField component (one implementation, no duplication).
    StarField {
        anchors.fill: parent
        visible: root.showStars
        showDust: root.showStars && root.showDust
        fieldOpacity: root.starOpacityScale
        densityScale: root.starDensity
        paused: root.paused
    }

    Timer {
        id: slideTimer
        repeat: false
        onTriggered: root.advance()
    }

    SequentialAnimation {
        id: crossfade
        property int pendingIndex: -1

        function beginTo(nextIndex) {
            pendingIndex = nextIndex
            restart()
        }

        NumberAnimation {
            target: root
            property: "mediaTransitionFactor"
            to: 0
            duration: 1400
            easing.type: Easing.InOutQuad
        }
        ScriptAction { script: root.startAsset(crossfade.pendingIndex) }
        NumberAnimation {
            target: root
            property: "mediaTransitionFactor"
            to: 1
            duration: 1600
            easing.type: Easing.InOutQuad
        }
    }

    onInspirationAssetsChanged: syncMediaLayer()
    // Toggling media visibility (e.g. entering/leaving a routine) tears down or
    // rebuilds the video pipeline rather than leaving it decoding while hidden.
    onShowMediaChanged: syncMediaLayer()

    Component.onCompleted: {
        syncMediaLayer()
    }
}
