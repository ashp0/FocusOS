var params = new URLSearchParams(window.location.search);
var paramUrl = params.get('url');
var paramReason = params.get('reason');
var parsedUrl = "";
var parsedReason = {};

if (paramUrl != null) {
	parsedUrl = decodeURIComponent(paramUrl);
}
function b64url_to_unicode(str) {
	str = str.replace(/-/g, '+').replace(/_/g, '/');
	while (str.length % 4) str += '=';
	return b64_to_unicode(str);
}
function b64_to_unicode(str) {
	const binary = atob(str);
	const bytes = Array.from(binary, c => '%' + c.charCodeAt(0).toString(16).padStart(2, '0'));
	return decodeURIComponent(bytes.join(''));
}
if (paramReason != null) {
	parsedReason = JSON.parse(b64url_to_unicode(paramReason));
}

var iframe = document.createElement('iframe');
iframe.src = parsedUrl;
iframe.id = "blocker-frame";
document.body.appendChild(iframe);

window.addEventListener("message", (event) => {
	if (typeof event.data.command != 'undefined') {
		if (event.data.command == "cold-turkey-blocker-get-reason") {
			document.getElementById("blocker-frame").contentWindow.postMessage({ command: "cold-turkey-blocker-reason", reason: parsedReason }, "*");
		} else if (event.data.command == "cold-turkey-blocker-unblock-tab") {
			chrome.runtime.sendMessage({ command: "unblockTab", blockId: parsedReason.blockId, lock: parsedReason.lock, duration: event.data.duration });
		} else if (event.data.command == "cold-turkey-blocker-start-delay") {
			chrome.runtime.sendMessage({ command: "startDelay", blockId: parsedReason.blockId });
		} else if (event.data.command == "cold-turkey-blocker-stop-delay") {
			chrome.runtime.sendMessage({ command: "stopDelay", blockId: parsedReason.blockId });
		} else if (event.data.command == "cold-turkey-blocker-start-break") {
			chrome.runtime.sendMessage({ command: "startBreak", blockId: parsedReason.blockId, lock: parsedReason.lock, duration: event.data.duration });
		} else if (event.data.command == "cold-turkey-blocker-start-randomText-break") {
			chrome.runtime.sendMessage({ command: "startRandomTextBreak", blockId: parsedReason.blockId });
		} else if (event.data.command == "cold-turkey-blocker-reload-delay") {
			chrome.runtime.sendMessage({ command: "reloadDelay" });
		}
	}
});