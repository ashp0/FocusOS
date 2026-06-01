import QtQuick
import QtQuick.Window

// Self-contained deep-space starfield with a slow fly-through ("warp") motion:
// stars stream outward from a central vanishing point as if the viewer were
// drifting forward through space, growing and brightening as they approach,
// then respawning far away near the centre. A gentle twinkle rides on top, and
// a thin layer of cosmic dust drifts for parallax. Transparent background.
// Used by the pitch-black idle screen and the ambient backdrop.
//
// Paints on a dedicated render thread and pauses when nothing can be seen
// (unfocused / hidden / display asleep), so it costs nothing while invisible.
Item {
    id: root

    property bool showDust: true
    // How fast the viewer drifts forward. Small = calm. Each tick a star's depth
    // shrinks by this fraction, so nearer stars accelerate outward (perspective).
    property real warpSpeed: 0.016
    // Master brightness multiplier (lets callers dim a layered overlay field).
    property real fieldOpacity: 1
    // Scales the star count. The thin overlay field drawn over the UI panels runs
    // at a lower density — fewer stars to draw every frame (battery) and less
    // motion competing with on-screen text.
    property real densityScale: 1

    // Stop repainting when the app is unfocused/minimised, this layer is hidden
    // or transparent — so the screensaver burns nothing behind a focused routine
    // app or once the panel has been put to sleep.
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
        opacity: root.fieldOpacity
        renderStrategy: Canvas.Threaded

        property var points: []
        // Near/far depth clamps. A star lives between zFar (just spawned, deep)
        // and zNear (about to pass the viewer); below zNear it respawns.
        readonly property real zNear: 0.10
        readonly property real zFar: 1.0

        readonly property var palette: ["#e8dcc8", "#ffffff", "#c9a84c", "#9eb8c0", "#7da7d9", "#d6c2ff"]

        // A star's fixed direction from the vanishing point (ux, uy in [-1, 1])
        // plus a depth z. Screen position is centre + (u / z) * halfExtent, so as
        // z shrinks the star slides outward and accelerates — the fly-through.
        function makeStar(randomDepth) {
            const isFlasher = Math.random() < 0.16
            // Bias direction away from dead-centre so stars don't pile up at the
            // vanishing point; magnitude shapes how quickly they reach an edge.
            const ux = (Math.random() * 2 - 1)
            const uy = (Math.random() * 2 - 1)
            return {
                ux: ux,
                uy: uy,
                z: randomDepth ? (zNear + Math.random() * (zFar - zNear)) : zFar,
                // Per-star depth-speed jitter keeps the field from pulsing in lockstep.
                zSpeed: 0.7 + Math.random() * 0.6,
                size: isFlasher ? (0.7 + Math.random() * 1.3) : (0.45 + Math.random() * 0.9),
                baseAlpha: isFlasher ? (0.5 + Math.random() * 0.4) : (0.22 + Math.random() * 0.3),
                phase: Math.random() * Math.PI * 2,
                twinkle: 0.4 + Math.random() * 1.1,
                flashChance: isFlasher ? (0.010 + Math.random() * 0.020) : 0,
                flashing: 0,
                color: palette[Math.floor(Math.random() * palette.length)],
                diffractionStrong: isFlasher && Math.random() < 0.4
            }
        }

        function seed() {
            const area = Math.max(1, width * height)
            const base = Math.max(120, Math.min(360, Math.round(area / 6200)))
            const count = Math.max(24, Math.round(base * Math.max(0.05, root.densityScale)))
            const next = []
            for (let i = 0; i < count; ++i) {
                next.push(makeStar(true))
            }
            points = next
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
            const speed = root.warpSpeed
            for (let i = 0; i < points.length; ++i) {
                const p = points[i]
                // Exponential approach: closer stars move outward faster, which is
                // what makes it read as forward motion rather than a uniform pan.
                p.z -= p.z * speed * p.zSpeed
                p.phase += p.twinkle * 0.06
                if (p.flashing > 0) {
                    p.flashing -= 0.06
                    if (p.flashing < 0) p.flashing = 0
                } else if (p.flashChance > 0 && Math.random() < p.flashChance) {
                    p.flashing = 1
                }
                if (p.z <= zNear) {
                    // Passed the viewer — recycle it back into the deep field.
                    points[i] = makeStar(false)
                }
            }
            requestPaint()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const cx = width / 2
            const cy = height / 2

            for (let i = 0; i < points.length; ++i) {
                const p = points[i]
                const k = 1 / p.z                       // perspective scale (1..10)
                const sx = cx + p.ux * cx * k
                const sy = cy + p.uy * cy * k
                if (sx < -40 || sx > width + 40 || sy < -40 || sy > height + 40) {
                    continue
                }

                // Brightness and size grow as the star nears (k larger). depth in
                // 0..1 where 1 = closest.
                const depth = Math.min(1, (k - 1) / 9)
                const tw = 0.6 + Math.sin(p.phase) * 0.3
                const flashBoost = p.flashing > 0 ? (p.flashing * 1.1) : 0
                const alpha = Math.min(1, Math.max(0.03,
                    (p.baseAlpha * (0.45 + depth * 0.9) * tw + flashBoost) * root.fieldOpacity))
                const radius = Math.max(0.3, p.size * (0.6 + depth * 1.8) + flashBoost * 1.4)

                // A short streak trailing back toward the vanishing point sells the
                // travel — only for nearer stars, kept faint so it stays calm.
                if (depth > 0.32) {
                    const trail = (depth - 0.32) * 26
                    const dirLen = Math.max(0.0001, Math.hypot(sx - cx, sy - cy))
                    const tx = sx - (sx - cx) / dirLen * trail
                    const ty = sy - (sy - cy) / dirLen * trail
                    ctx.strokeStyle = rgba(p.color, alpha * 0.4)
                    ctx.lineWidth = Math.max(0.6, radius * 0.8)
                    ctx.beginPath()
                    ctx.moveTo(tx, ty)
                    ctx.lineTo(sx, sy)
                    ctx.stroke()
                }

                if (p.flashing > 0) {
                    const glow = ctx.createRadialGradient(sx, sy, 0, sx, sy, radius * 5)
                    glow.addColorStop(0, rgba(p.color, alpha * 0.5))
                    glow.addColorStop(1, rgba(p.color, 0))
                    ctx.fillStyle = glow
                    ctx.beginPath()
                    ctx.arc(sx, sy, radius * 5, 0, Math.PI * 2)
                    ctx.fill()
                }

                ctx.fillStyle = rgba(p.color, alpha)
                ctx.beginPath()
                ctx.arc(sx, sy, radius, 0, Math.PI * 2)
                ctx.fill()

                // Diffraction spikes on the brightest near stars / active flashers.
                if (p.flashing > 0 || (p.diffractionStrong && depth > 0.5)) {
                    const reach = radius * (p.diffractionStrong ? 5.0 : 2.4) + (p.flashing > 0 ? radius * 5 : 0)
                    ctx.strokeStyle = rgba(p.color, alpha * 0.4)
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(sx - reach, sy)
                    ctx.lineTo(sx + reach, sy)
                    ctx.moveTo(sx, sy - reach)
                    ctx.lineTo(sx, sy + reach)
                    ctx.stroke()
                }
            }
        }

        onWidthChanged: seed()
        onHeightChanged: seed()

        Timer {
            interval: 60
            running: root.animationActive
            repeat: true
            onTriggered: stars.tick()
        }
    }

    Canvas {
        id: dust
        anchors.fill: parent
        visible: root.showDust
        opacity: 0.5 * root.fieldOpacity
        renderStrategy: Canvas.Threaded

        property var motes: []

        function seed() {
            const count = 32
            const next = []
            const cx = width / 2
            const cy = height / 2
            for (let i = 0; i < count; ++i) {
                const x = Math.random() * Math.max(1, width)
                const y = Math.random() * Math.max(1, height)
                // Drift gently outward from centre to echo the stars' fly-through.
                const dx = x - cx
                const dy = y - cy
                const len = Math.max(1, Math.hypot(dx, dy))
                const sp = 0.05 + Math.random() * 0.12
                next.push({
                    x: x, y: y,
                    vx: dx / len * sp,
                    vy: dy / len * sp,
                    r: 0.4 + Math.random() * 1.1,
                    alpha: 0.04 + Math.random() * 0.09
                })
            }
            motes = next
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
            const cx = width / 2
            const cy = height / 2
            for (let i = 0; i < motes.length; ++i) {
                const m = motes[i]
                m.x += m.vx
                m.y += m.vy
                // Recycle motes that drift off the edges back near the centre.
                if (m.x < -10 || m.x > width + 10 || m.y < -10 || m.y > height + 10) {
                    const ang = Math.random() * Math.PI * 2
                    const rad = Math.random() * 40
                    m.x = cx + Math.cos(ang) * rad
                    m.y = cy + Math.sin(ang) * rad
                    const dx = m.x - cx
                    const dy = m.y - cy
                    const len = Math.max(1, Math.hypot(dx, dy))
                    const sp = 0.05 + Math.random() * 0.12
                    m.vx = dx / len * sp
                    m.vy = dy / len * sp
                }
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
            interval: 90
            running: root.showDust && root.animationActive
            repeat: true
            onTriggered: dust.tick()
        }
    }

    onDensityScaleChanged: stars.seed()

    Component.onCompleted: {
        stars.seed()
        dust.seed()
    }
}
