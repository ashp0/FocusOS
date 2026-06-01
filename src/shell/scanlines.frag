#version 440

// Faint CRT scanline overlay — replaces a full-screen CPU Canvas that, though
// painted only once, allocated a device-sized backing store (large on Retina).
// Drawn procedurally on the GPU with no backing store.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2  iResolution;   // pixel size — scanlines are spaced in device pixels
};

void main() {
    // A 2px-on / 2px-off horizontal line pattern, matching the old Canvas.
    float y = qt_TexCoord0.y * iResolution.y;
    float line = step(2.0, mod(y, 4.0));   // 0 on the line, 1 in the gap
    float a = (1.0 - line) * 0.018;
    vec3 col = vec3(0.627, 0.706, 0.863);  // rgb(160,180,220)
    fragColor = vec4(col * a, a) * qt_Opacity;
}
