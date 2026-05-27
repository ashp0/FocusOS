.pragma library

// Exact FocusOS palette values.
var voidColor = "#050508"
var iron = "#0f0f14"
var steel = "#1a1a24"
var voidPanel = "#ee050508"
var ironPanel = "#ee0f0f14"
var steelPanel = "#ee1a1a24"
var crimson = "#8b1a1a"
var crimsonHot = "#c0392b"
var gold = "#c9a84c"
var goldDim = "#7a5c1e"
var textPrimary = "#e8dcc8"
var textDim = "#7a7060"
var textGhost = "#3a3428"

var transitionMs = 120
var panelBorder = 1
var divider = 2

function duration(hours, minutes, seconds) {
    return pad2(hours) + ":" + pad2(minutes) + ":" + pad2(seconds)
}

function pad2(value) {
    return value < 10 ? "0" + value : "" + value
}
