import QtQuick
import QtQuick.Window

// Self-contained deep-space starfield: twinkling stars + slow drifting dust on
// a transparent background. Used by the pitch-black idle screen (and available
// for the ambient backdrop). Paints on a dedicated render thread and pauses
// when nothing can be seen, so it costs nothing while hidden.
Item {
    id: root

    property bool showDust: true

    // Stop repainting when the app is unfocused/minimised or this layer is
    // hidden — matches AmbientLayer's behaviour so the screensaver doesn't burn
    // GPU behind a focused routine app.
    property bool animationActive: Qt.application.active &&
                                   root.visible &&
                                   root.opacity > 0.01 &&
                                   (Window.window ? Window.window.visibility !== Window.Minimized
                                                  && Window.window.visibility !== Window.Hidden : true)

    onAnimationActiveChanged: {
        if (animationActive) {
            stars.requestPaint()
            if (showDust) {
                dust.requestPaint()
            }
        }
    }

    Canvas {
        id: stars
        anchors.fill: parent
        renderStrategy: Canvas.Threaded

        property var points: []

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
            running: root.animationActive
            repeat: true
            onTriggered: stars.tick()
        }
    }

    Canvas {
        id: dust
        anchors.fill: parent
        visible: root.showDust
        opacity: 0.55
        renderStrategy: Canvas.Threaded

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
            running: root.showDust && root.animationActive
            repeat: true
            onTriggered: dust.tick()
        }
    }

    Component.onCompleted: {
        stars.seed()
        dust.seed()
    }
}
