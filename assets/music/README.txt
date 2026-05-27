FocusOS ambient music
=====================

Place .mp3 or .ogg files in ~/.focusos/music/ to override the bundled
fallback drone.

The bundled fallback is:

assets/music/ambient_default.ogg

It is a generated 55Hz + 110Hz ambient drone with slow tremolo. The requested
first-launch Qt Multimedia OGG generator is intentionally not implemented
because Qt Multimedia does not provide a small, direct OGG encoder path for this
app.
