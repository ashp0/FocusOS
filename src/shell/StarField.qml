import QtQuick
import QtQuick.Window

// Deep-space starfield with a slow fly-through ("warp") motion: stars stream
// outward from a central vanishing point as the viewer drifts forward, with a
// gentle rotation + lateral drift that reads as crossing between galaxies, a
// soft galactic-core glow and per-star twinkle. Transparent background.
// Used by the pitch-black idle screen and the ambient backdrop.
//
// The whole field is drawn by a fragment shader on the GPU (starfield.frag) —
// the old Canvas implementation rasterised hundreds of stars on the CPU every
// frame and allocated full-screen backing stores (very costly on a HiDPI/Retina
// panel). This version hands all of that to the graphics driver: the only CPU
// work per frame is advancing a single `iTime` uniform.
//
// Painting pauses entirely when nothing can be seen (unfocused / hidden /
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

        // Uniforms — matched to starfield.frag by property name.
        property real iTime: 0
        property size iResolution: Qt.size(Math.max(1, width), Math.max(1, height))
        property real fieldOpacity: root.fieldOpacity
        property real density: Math.max(0.04, Math.min(1.0, root.densityScale))
        property real coreGlow: root.densityScale >= 0.9 ? 0.5 : 0.0
        property real dust: (root.showDust && root.densityScale >= 0.9) ? 1.0 : 0.0

        fragmentShader: "qrc:/qt/qml/FocusOS/starfield.frag.qsb"
    }

    // Advances the shader clock only while the field is visible. FrameAnimation
    // ticks once per rendered frame and is paused with `running`, so a hidden
    // field consumes nothing. Accumulating frameTime (rather than reading
    // elapsedTime) keeps the motion continuous across pause/resume.
    FrameAnimation {
        running: root.animationActive
        onTriggered: sky.iTime += frameTime * root.warpSpeed
    }
}
