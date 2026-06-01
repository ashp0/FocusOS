import QtQuick
import QtQuick.Window

// Deep-space starfield with a slow fly-through ("warp") motion: stars stream
// outward from a central vanishing point as the viewer drifts forward, with a
// gentle rotation + lateral drift that reads as crossing between galaxies, a
// soft galactic-core glow and per-star twinkle. Transparent background.
// Used by the pitch-black idle screen and the ambient backdrop.
//
// The whole field is drawn by a fragment shader on the GPU (starfield.frag).
//
// IMPORTANT: a fullscreen procedural fragment shader is NOT automatically
// cheaper than the old CPU Canvas — it evaluates an expensive per-pixel function
// (six depth slices, each with a 1/d² glow term that is non-zero everywhere) for
// *every* pixel of the panel, every frame. At native HiDPI resolution × 60 fps ×
// the two-to-three starfield layers FocusOS stacks (backdrop + thin overlay +
// idle screen), that pegs an integrated GPU and starves the compositor — which
// is why the cursor and video stutter. The old Canvas drew a few hundred star
// sprites at ~16 fps, far less total work.
//
// So we bound the GPU work two ways without changing the look:
//   1. Render the shader into a reduced-resolution layer (renderScale) and let
//      the GPU bilinear-upscale it. The field is soft/glow-heavy, so the upscale
//      is invisible while pixel cost drops with the square of the scale (0.5 ⇒ ¼).
//   2. Throttle the clock to ~30 fps. The drift is extremely slow, so half the
//      frames look identical but cost nothing on the idle screen (where the
//      starfield is the only thing animating, so a skipped clock tick = a
//      skipped scene render).
//
// Rendering pauses entirely when nothing can be seen (unfocused / hidden /
// display asleep), so it costs nothing while invisible.
Item {
    id: root

    // Backdrop shows dust + the core glow; the thin overlay over the panels
    // turns both off. Kept as a property for the existing callers.
    property bool showDust: true
    // Fly-through speed multiplier (1 = calm default). No caller overrides it.
    property real warpSpeed: 1.0
    // Master brightness multiplier (dims a layered overlay field).
    property real fieldOpacity: 1
    // Scales the star count; the thin overlay field runs at a lower density so
    // its motion doesn't compete with on-screen text.
    property real densityScale: 1
    // Fraction of native resolution the shader is rasterised at (see header).
    // 0.5 ⇒ a quarter of the fragments; the soft field upscales cleanly.
    property real renderScale: 0.5
    // Clock cap. The warp is slow enough that 30 fps is indistinguishable from
    // 60 while halving render cost on the idle screen.
    property real frameInterval: 1.0 / 30.0

    // Stop rendering when the app is unfocused/minimised, this layer is hidden
    // or transparent — so the screensaver burns nothing behind a focused routine
    // app or once the panel has been put to sleep.
    property bool animationActive: Qt.application.active &&
                                   root.visible &&
                                   root.opacity > 0.01 &&
                                   (Window.window ? Window.window.visibility !== Window.Minimized
                                                  && Window.window.visibility !== Window.Hidden : true)

    ShaderEffect {
        id: sky
        anchors.fill: parent
        blending: true

        // Rasterise into a smaller FBO and bilinear-upscale to fill — this is
        // where the bulk of the GPU saving comes from (see header).
        layer.enabled: root.renderScale < 0.999
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(2, Math.round(width * root.renderScale)),
                                   Math.max(2, Math.round(height * root.renderScale)))

        // Uniforms — matched to starfield.frag by property name.
        property real iTime: 0
        property size iResolution: Qt.size(Math.max(1, width), Math.max(1, height))
        property real fieldOpacity: root.fieldOpacity
        property real density: Math.max(0.04, Math.min(1.0, root.densityScale))
        property real coreGlow: root.densityScale >= 0.9 ? 0.5 : 0.0
        property real dust: (root.showDust && root.densityScale >= 0.9) ? 1.0 : 0.0

        fragmentShader: "qrc:/qt/qml/FocusOS/starfield.frag.qsb"
    }

    // Advances the shader clock only while the field is visible, capped at
    // ~frameInterval. FrameAnimation ticks once per rendered frame and is paused
    // with `running`, so a hidden field consumes nothing. We accumulate frameTime
    // and only push it to the `iTime` uniform once a frame-interval's worth has
    // built up: that both keeps motion continuous across pause/resume and means
    // intermediate vsyncs don't dirty the shader (no needless re-render).
    FrameAnimation {
        running: root.animationActive
        property real pending: 0
        onTriggered: {
            pending += frameTime * root.warpSpeed
            if (pending >= root.frameInterval) {
                sky.iTime += pending
                pending = 0
            }
        }
    }
}
