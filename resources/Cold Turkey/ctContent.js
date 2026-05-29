/*	Cold Turkey Blocker Chrome Extension v4.9
	Copyright (c) 2026 Cold Turkey Software Inc.
*/

var ytTimer;
var embedTimer;
var miniTimer;
var pageBlocked = false;
var pageContentCheckedOnload = false;

if (typeof navigator.serviceWorker != "undefined") {
	navigator.serviceWorker.register = function() {
		console.warn('Service worker blocked by Cold Turkey Blocker.');
		return Promise.reject(new Error('Service worker registration blocked by Cold Turkey Blocker.'));
	};
	navigator.serviceWorker.getRegistration = function() {
		console.warn('Service worker blocked by Cold Turkey Blocker.');
		return Promise.resolve(null);
	};
	Object.defineProperty(navigator.serviceWorker, 'controller', {
		get() {
			console.warn('Service worker controller blocked by Cold Turkey Blocker.');
			return null;
		}
	});
}

window.addEventListener("yt-page-data-updated", function() { 
	checkPage();
});

window.addEventListener("popstate", function() {
	if (pageBlocked) {
		window.location.reload(true);
	} else {
		window.setTimeout(init, 500);
	}
});

window.addEventListener("pageshow", (e) => {
	if (e.persisted) {
		if (pageBlocked) {
			window.location.reload(true);
		} else {
			window.setTimeout(init, 500);
		}
	}
});

window.addEventListener("message", (event) => {
	if (event.source == window && event.data && event.data.command == "cold-turkey-blocker-pause") {
		chrome.runtime.sendMessage({ command: "pause", key: event.data.key }, function(response) { } );
	}
});

init();

function init() {
	if (!window.location.href.match(/^((https|http):\/\/)(www.)?youtube\.com\/watch\?.*v=.*/) && !window.location.href.match(/^((https|http):\/\/)(www.)?youtube\.com\/playlist\?.*list=.*/)) {
		chrome.runtime.sendMessage({command: "checkBlockUrl", site: window.location.href, embedded: (window.self != window.top)}, function(response) {
			handleResponse(response);	
		});
		window.clearInterval(ytTimer);
		window.addEventListener("DOMContentLoaded", function() {
			if (!pageBlocked) {
				if (document.visibilityState == "visible") {
					pageContentCheckedOnload = true;
					checkPage();
				} else {
					document.addEventListener("visibilitychange", function () {
						if (document.visibilityState == "visible" && !pageContentCheckedOnload) {
							pageContentCheckedOnload = true;
							checkPage();
						}
					});
				}
			}
		});
	} else {
		checkPage();
	}
}

function checkPage() {
	
	/* Restore mute to user setting in case we blocked it before */
	try {
		chrome.runtime.sendMessage({command: "unmuteTab"});
	} catch (e) { }
	
	/* Check window title block */
	try {
		chrome.runtime.sendMessage({command: "checkBlockTitle", title: window.document.title, site: window.location.href, embedded: (window.self != window.top)}, function(response) {
			handleResponse(response);
		});
	} catch (e) { }
	
	/* Check if adult sites should be blocked (if *.xxx is added to block) */
	try {
		if (document.head.querySelector("meta[name='rating' i]") != null && typeof document.head.querySelector("meta[name='rating' i]").content == "string") {
			if (document.head.querySelector("meta[name='rating' i]").content.toLowerCase() == "rta-5042-1996-1400-1577-rta" || document.head.querySelector("meta[name='rating' i]").content.toLowerCase() == "adult") {
				chrome.runtime.sendMessage({command: "checkBlockAdult", url: window.location.href, embedded: (window.self != window.top)}, function(response) {
					handleResponse(response);
				});
			}
		}
	} catch (e) { }

	/* Check Google search page auto-correct */
	if (window.location.href.toLowerCase().match(/^((https|http):\/\/)(www.)?google\.com\/search\?.*q=.*/)) {
		let links = document.querySelectorAll('a');
		links.forEach(link => {
			if (link.href && link.href.includes('&spell=1')) {
				chrome.runtime.sendMessage({command: "checkBlockUrl", site: link.href, embedded: (window.self != window.top)}, function(response) {
					handleResponse(response);	
				});
			}
		});
	}

	/* YouTube blocking */
	if (window.location.href.toLowerCase().match(/^((https|http):\/\/)(www.)?youtube\.com\/watch\?.*v=.*/) || window.location.href.toLowerCase().match(/^((https|http):\/\/)(www.)?youtube\.com\/playlist\?.*list=.*/)) {
		
		window.clearTimeout(ytTimer);
		ytTimer = window.setTimeout(checkPage, 4000);

		if (document.readyState == "loading") {
			
			window.addEventListener("yt-page-data-updated", function() { 
				checkPage();
			});

		} else {
			
			let ownerURL = document.querySelector('ytd-video-owner-renderer a[href*="/@"]')?.href || document.querySelector('ytd-video-description-infocards-section-renderer a#header[href*="/@"]')?.href || "";

			try {
				chrome.runtime.sendMessage({command: "checkBlockYouTube", url: window.location.href, channel: ownerURL, embedded: (window.self != window.top)}, function(response) {
					handleResponse(response);
				});
			} catch (e) { }

		}

	}

}

function handleResponse(response) {
	
	if (response.block.action == "block") {

		if (response.version <= 3) {
			if (window.self == window.top) {
				window.stop();
				var param = '';	
				param = param + (response.isPro ? '?pro=true' : '?pro=false');
				param = param + '&rand=' + Math.round(Math.random() * 10000000).toString();
				blockPage("3.0", param, "");
			}
		} else if (response.version == 4) {
			if ((window.self == window.top) || response.blockEmbedded) {
				window.stop();
				var param = '';
				param = param + ((window.self != window.top) ? '?embed=true' : '?embed=false');
				param = param + (response.isPro ? '&pro=true' : '&pro=false');
				param = param + (response.blockCharity ? '&blockCharity=true' : '&blockCharity=false');
				param = param + '&rand=' + Math.round(Math.random() * 10000000).toString();
				blockPage("4.0", param, "");
			}
		} else if (response.version == 5) {
			var param = '';
			param = param + ((window.self != window.top) ? '?embed=true' : '?embed=false');
			param = param + (response.isPro ? '&pro=true' : '&pro=false');
			param = param + (response.blockCharity ? '&blockCharity=true' : '&blockCharity=false');
			param = param + '&rand=' + Math.round(Math.random() * 10000000).toString();
			blockPage("4.9", param, {"blockId": response.block.blockId, "type": response.block.type, "url": response.block.url, "rule": response.block.rule, "lock": response.block.lock, "password": response.block.password, "randomText": response.block.randomText});
		}

	}

	if (typeof response.block.restrictYouTubeControls == "boolean" && response.block.restrictYouTubeControls) {

		/* Remove video suggestion overlays, next, prev buttons on embedded YouTube videos */
		if (window.self != window.top && window.location.href.match(/^((https|http):\/\/)(www.)?youtube(\-nocookie)?\.com\/.*/)) {
			embedTimer = window.setInterval(function() {
				if (document.getElementsByClassName("ytp-pause-overlay").length > 0) {
					Array.from(document.getElementsByClassName("ytp-pause-overlay")).forEach((element) => element.style.visibility = "hidden");
				}
				if (document.getElementsByClassName("videowall-endscreen").length > 0) {
					Array.from(document.getElementsByClassName("videowall-endscreen")).forEach((element) => element.style.visibility = "hidden");
				}
				if (document.getElementsByClassName("ytp-next-button").length > 0) {
					Array.from(document.getElementsByClassName("ytp-next-button")).forEach((element) => element.style.visibility = "hidden");
				}
				if (document.getElementsByClassName("ytp-prev-button").length > 0) {
					Array.from(document.getElementsByClassName("ytp-prev-button")).forEach((element) => element.style.visibility = "hidden");
				}
			}, 500);
		}
		
		/* Close YouTube miniplayer */
		if (window.self == window.top && window.location.href.match(/^((https|http):\/\/)(www.)?youtube\.com(\/.*)?$/)) {
			miniTimer = window.setInterval(function() {
				if (document.querySelectorAll(".ytp-miniplayer-close-button").length > 0) {
					Array.from(document.querySelectorAll(".ytp-miniplayer-close-button")).forEach((element) => element.click());
				}
			}, 500);
		}

	}
	
}

function unicode_to_b64url(str) {
	return unicode_to_b64(str)
		.replace(/\+/g, '-')
		.replace(/\//g, '_')
		.replace(/=+$/, '');
}

function unicode_to_b64(str) {
	const encoded = encodeURIComponent(str)
		.replace(/%([0-9A-F]{2})/g, (_, p1) => String.fromCharCode('0x' + p1));
	return btoa(encoded);
}

function blockPage(version, param, reason) {
	
	if (!pageBlocked) {
		
		pageBlocked = true;
		window.clearInterval(ytTimer);
		
		var encodedReason = "";	
		if (reason != "") {
			encodedReason = '&reason=' + unicode_to_b64url(JSON.stringify(reason));
		}
		
		window.stop();
		stopEventsAndWorkers();
		
		var iframe = document.createElement('iframe');
		iframe.src = chrome.runtime.getURL('ctFrame.html?url=' + encodeURIComponent('https://getcoldturkey.com/blocked/' + version + "/" + param) + encodedReason);
		iframe.style.cssText = 'position: absolute; top: 0; left: 0; width: 100%; height: 100%; border: 0;';
		
		var tmp = document.createElement("div");
		tmp.appendChild(iframe);
		
		var blockPage = '<html><head><title>Blocked by Cold Turkey</title></head><body style="margin:0 !important;">' + tmp.innerHTML + '</body></html>';
		
		window.setInterval(function () {
			if (window.self == window.top) { if (chrome.runtime?.id) { chrome.runtime.sendMessage({command: "muteTab"}); } }
			if ("pictureInPictureElement" in document) {
				if (document.pictureInPictureElement) {
					document.pictureInPictureElement.pause();
					document.exitPictureInPicture();
				}
			}
		}, 500);
		
		document.documentElement.innerHTML = blockPage;

		window.setInterval(function () {
			var j = document.querySelectorAll(atob("aWZyYW1l"));
			for (var i = 0; i < j.length; i++) {
				if (typeof j[i].src == 'string' && !j[i].src.startsWith(atob("Y2hyb21lLWV4dGVuc2lvbjovLw=="))) {
					j[i].parentNode.removeChild(j[i]);
				}
			}
		}, 5000);
		
	}
	
}

function stopEventsAndWorkers() {

	window.onbeforeunload = null;
	document.onmouseout = null;
	document.onmouseleave = null;
	window.onmouseout = null;
	window.onmouseleave = null;

	var maxId = setTimeout(function(){}, 0);
	for(var i=0; i < maxId; i+=1) { 
		clearTimeout(i);
	}

	window.Worker = function() {
		return { postMessage(){}, terminate(){} };
	};

}