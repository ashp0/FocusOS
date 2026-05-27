import QtQuick
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
    property bool showCircles: false   // legacy slot — circles removed for space feel
    property real circleOpacityScale: 0
    property real starOpacityScale: 1
    property int imageHoldMs: 10000
    property int activeAssetType: 0    // 0=image, 1=video
    property double fadeStartTime: Date.now()

    function resetFade() {
        fadeStartTime = Date.now()
        fadeProgress = 0
        fadeTicker.restart()
    }

    function dimForSession() {
        fadeProgress = 1
        fadeTicker.stop()
    }

    function tickFade() {
        const elapsed = Date.now() - fadeStartTime
        const t = Math.max(0, Math.min(1, elapsed / fadeDurationMs))
        fadeProgress = t
    }

    Timer {
        id: fadeTicker
        interval: 1000      // update opacity every second
        running: true
        repeat: true
        onTriggered: root.tickFade()
    }

    Connections {
        target: routineManager
        function onActiveChanged() {
            if (routineManager.active) {
                root.dimForSession()
            } else {
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
        if (!inspirationAssets || inspirationAssets.length <= 0) {
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
            audioOutput: AudioOutput {
                muted: true
                volume: 0
            }
            loops: 1

            onMediaStatusChanged: {
                if (mediaStatus === MediaPlayer.EndOfMedia) {
                    root.advance()
                }
            }
        }
    }

    // Deep-space starfield — fixed positions, twinkling.
    Canvas {
        id: stars
        anchors.fill: parent
        visible: root.showStars
        opacity: root.starOpacityScale

        property var points: []
        property double startTime: Date.now()

        function seed() {
            const area = Math.max(1, width * height)
            const count = Math.max(140, Math.min(420, Math.round(area / 5200)))
            const nextPoints = []
            const colors = ["#e8dcc8", "#ffffff", "#c9a84c", "#9eb8c0", "#7da7d9", "#d6c2ff"]
            for (let i = 0; i < count; ++i) {
                const isFlasher = Math.random() < 0.18
                nextPoints.push({
                    x: Math.random() * Math.max(1, width),
                    y: Math.random() * Math.max(1, height),
                    r: isFlasher ? (0.6 + Math.random() * 1.6) : (0.35 + Math.random() * 1.05),
                    phase: Math.random() * Math.PI * 2,
                    speed: isFlasher ? (1.6 + Math.random() * 2.4) : (0.25 + Math.random() * 0.85),
                    baseAlpha: isFlasher ? (0.45 + Math.random() * 0.4) : (0.18 + Math.random() * 0.32),
                    flashChance: isFlasher ? (0.012 + Math.random() * 0.022) : 0,
                    flashing: 0,
                    color: colors[Math.floor(Math.random() * colors.length)],
                    diffractionStrong: isFlasher && Math.random() < 0.4
                })
            }
            points = nextPoints
            requestPaint()
        }

        function rgba(hex, alpha) {
            const clean = String(hex).replace("#", "")
            const red = parseInt(clean.slice(0, 2), 16)
            const green = parseInt(clean.slice(2, 4), 16)
            const blue = parseInt(clean.slice(4, 6), 16)
            return "rgba(" + red + "," + green + "," + blue + "," + alpha + ")"
        }

        function tick() {
            if (width <= 0 || height <= 0 || points.length <= 0) {
                if (width > 0 && height > 0) {
                    seed()
                }
                return
            }

            for (let i = 0; i < points.length; ++i) {
                const point = points[i]
                point.phase += point.speed * 0.08
                if (point.flashing > 0) {
                    point.flashing -= 0.06
                    if (point.flashing < 0) point.flashing = 0
                } else if (point.flashChance > 0 && Math.random() < point.flashChance) {
                    point.flashing = 1
                }
                if (Math.random() < 0.0004) {
                    point.x = Math.random() * width
                    point.y = Math.random() * height
                }
            }
            requestPaint()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            for (let i = 0; i < points.length; ++i) {
                const p = points[i]
                const twinkle = 0.55 + Math.sin(p.phase) * 0.35
                const flashBoost = p.flashing > 0 ? (p.flashing * 1.2) : 0
                const alpha = Math.min(1, Math.max(0.04, p.baseAlpha * twinkle + flashBoost))
                const radius = p.r * (0.85 + twinkle * 0.5 + flashBoost * 1.5)

                if (p.flashing > 0) {
                    const glow = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, radius * 5)
                    glow.addColorStop(0, rgba(p.color, alpha * 0.5))
                    glow.addColorStop(1, rgba(p.color, 0))
                    ctx.fillStyle = glow
                    ctx.beginPath()
                    ctx.arc(p.x, p.y, radius * 5, 0, Math.PI * 2)
                    ctx.fill()
                }

                ctx.fillStyle = rgba(p.color, alpha)
                ctx.beginPath()
                ctx.arc(p.x, p.y, radius, 0, Math.PI * 2)
                ctx.fill()

                if (p.r > 1.0 || p.flashing > 0) {
                    const reach = radius * (p.diffractionStrong ? 5.5 : 2.6) + (p.flashing > 0 ? radius * 6 : 0)
                    ctx.strokeStyle = rgba(p.color, alpha * 0.45)
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(p.x - reach, p.y)
                    ctx.lineTo(p.x + reach, p.y)
                    ctx.moveTo(p.x, p.y - reach)
                    ctx.lineTo(p.x, p.y + reach)
                    ctx.stroke()
                }
            }
        }

        onWidthChanged: seed()
        onHeightChanged: seed()

        Timer {
            interval: 80
            running: root.showStars
            repeat: true
            onTriggered: stars.tick()
        }
    }

    // Slow drifting cosmic dust — gives motion without circles
    Canvas {
        id: dust
        anchors.fill: parent
        visible: root.showStars
        opacity: 0.55

        property var motes: []

        function seed() {
            const count = 36
            const nextMotes = []
            for (let i = 0; i < count; ++i) {
                nextMotes.push({
                    x: Math.random() * Math.max(1, width),
                    y: Math.random() * Math.max(1, height),
                    r: 0.4 + Math.random() * 1.2,
                    vx: (Math.random() - 0.5) * 0.18,
                    vy: (Math.random() - 0.5) * 0.18,
                    alpha: 0.04 + Math.random() * 0.1
                })
            }
            motes = nextMotes
            requestPaint()
        }

        function tick() {
            if (width <= 0 || height <= 0) {
                return
            }
            if (motes.length <= 0) {
                seed()
                return
            }
            for (let i = 0; i < motes.length; ++i) {
                const m = motes[i]
                m.x += m.vx
                m.y += m.vy
                if (m.x < -10) m.x = width + 10
                else if (m.x > width + 10) m.x = -10
                if (m.y < -10) m.y = height + 10
                else if (m.y > height + 10) m.y = -10
            }
            requestPaint()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            for (let i = 0; i < motes.length; ++i) {
                const m = motes[i]
                ctx.fillStyle = "rgba(180,200,230," + m.alpha + ")"
                ctx.beginPath()
                ctx.arc(m.x, m.y, m.r, 0, Math.PI * 2)
                ctx.fill()
            }
        }

        onWidthChanged: seed()
        onHeightChanged: seed()

        Timer {
            interval: 120
            running: root.showStars
            repeat: true
            onTriggered: dust.tick()
        }
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

    Component.onCompleted: {
        stars.seed()
        dust.seed()
        syncMediaLayer()
    }
}
