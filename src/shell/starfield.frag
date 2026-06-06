#version 440

// GPU deep-space starfield — replaces the old CPU Canvas implementation.
// Renders entirely on the graphics driver: a slow fly-through ("warp") where
// stars stream outward from a vanishing point as the viewer drifts forward,
// with a gentle rotation + lateral drift so it reads as crossing between
// galaxies, plus a soft galactic-core glow and per-star twinkle. Output is
// premultiplied over black so it composits over the UI / wallpaper.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;          // fly-through/zoom clock — FROZEN by QML while paused AND
                          // on the home screen (where we want rotation but no zoom)
    float twinkleTime;    // twinkle clock — keeps advancing even while paused, so
                          // a halted (paused) field still shimmers
    float spinTime;       // rotation clock — independent of the fly-through. Advances
                          // on the home screen (rotation, no zoom) and during the full
                          // warp; FROZEN only when fully paused, so a paused field is
                          // dead-still apart from the twinkle.
    vec2  iResolution;    // pixel size, used only for aspect correction (DPI-safe)
    float fieldOpacity;   // master brightness (dims the thin overlay field)
    float density;        // 0.04..1 — thins stars for the lighter overlay layer
    float coreGlow;       // 0 on the overlay, ~0.5 on the full backdrop
    float dust;           // 0/1 — faint drifting cosmic dust on the backdrop only
};

const float PI = 3.14159265;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

vec2 hash22(vec2 p) {
    vec3 a = fract(vec3(p.xyx) * vec3(123.34, 234.34, 345.65));
    a += dot(a, a + 34.45);
    return fract(vec2(a.x * a.y, a.y * a.z));
}

// Star tint across a warm-gold → white → cool-blue → violet palette.
vec3 starTint(float h) {
    vec3 warm   = vec3(1.00, 0.84, 0.52);
    vec3 white  = vec3(1.00, 0.98, 0.95);
    vec3 cool   = vec3(0.55, 0.72, 1.00);
    vec3 violet = vec3(0.72, 0.62, 1.00);
    if (h < 0.40) return mix(warm,  white,  h / 0.40);
    if (h < 0.75) return mix(white, cool,  (h - 0.40) / 0.35);
    return mix(cool, violet, (h - 0.75) / 0.25);
}

// One depth slice of stars at fly-through progress z (0 = far, 1 = passing).
// As z grows the cell grid coarsens in screen space, so each star slides
// outward and brightens — that is what reads as forward motion.
vec3 starSlice(vec2 uv, float z, float seed, float t, float dens) {
    float spread = 0.04 + z * z * 2.6;   // exponential approach (perspective)
    vec2 g = uv / spread;
    vec2 cell = floor(g);
    vec2 f = fract(g) - 0.5;

    vec2 rnd = hash22(cell + seed * 71.3);
    // Thin the field for the overlay layer (density < 1) by dropping cells.
    float present = step(1.0 - dens, hash21(cell + seed * 13.1));
    vec2 sp = (rnd - 0.5) * 0.72;        // star position inside its cell
    float d = length(f - sp);

    float core = smoothstep(0.055, 0.0, d);
    float glow = 0.010 / (d * d + 0.0035);
    float tw   = 0.55 + 0.45 * sin(t + rnd.x * 6.2831);   // twinkle
    return starTint(rnd.y) * (core + glow * 0.22) * tw * present;
}

void main() {
    vec2 uv = qt_TexCoord0 - 0.5;
    uv.x *= iResolution.x / max(1.0, iResolution.y);

    // Slow rotation: the sense of slowly turning between galaxies. Driven by the
    // independent spin clock so the home screen can rotate without the fly-through.
    float ca = cos(spinTime * 0.008), sa = sin(spinTime * 0.008);
    uv = mat2(ca, -sa, sa, ca) * uv;
    // Lateral drift rides the fly-through clock (part of the travelling motion), so
    // it freezes with the zoom on the home screen and leaves pure rotation.
    uv += 0.05 * vec2(sin(iTime * 0.05), cos(iTime * 0.037));

    float t = iTime * 0.05;   // fly-through speed
    vec3 col = vec3(0.0);
    const int N = 6;
    for (int i = 0; i < N; ++i) {
        float fi = float(i) / float(N);
        float z = fract(fi + t);
        // Fade each slice in as it appears far off and out as it sweeps past.
        float fade = smoothstep(0.0, 0.12, z) * smoothstep(1.0, 0.70, z);
        // Twinkle phase comes from the twinkle clock (not iTime), so the per-star
        // shimmer keeps going even when the warp motion is frozen mid-pause.
        col += starSlice(uv, z, fi + 1.0, twinkleTime * (0.8 + fi), density) * fade;
    }

    float r = length(uv);

    // Faint drifting cosmic dust for parallax (backdrop only).
    if (dust > 0.5) {
        vec2 dp = uv * 3.0 + vec2(iTime * 0.01, -iTime * 0.013);
        float dn = hash21(floor(dp));
        col += vec3(0.18, 0.20, 0.24) * smoothstep(0.92, 1.0, dn) * 0.05;
    }

    // Galactic core glow at the vanishing point — only on the full backdrop.
    vec3 coreCol = mix(vec3(0.80, 0.66, 0.30), vec3(0.42, 0.58, 0.95),
                       smoothstep(0.0, 0.5, r));
    col += coreCol * exp(-r * 3.2) * coreGlow * (0.6 + 0.4 * sin(iTime * 0.2));

    col *= fieldOpacity;
    float a = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0);
    // Premultiplied: col is already the lit contribution over black.
    fragColor = vec4(col, a) * qt_Opacity;
}
