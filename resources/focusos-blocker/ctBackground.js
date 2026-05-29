/*	Cold Turkey Blocker Chrome Extension v4.9
	Copyright (c) 2026 Cold Turkey Software Inc.
*/

var port = null;
var statsTimer = null;
var doubleCheckTimer = null;

var version = 1;

var statsEnabled = false;
var statsEnabledIncognito = false;
var statsActive = true;
var isPro = false;
var ignoreIncognito = false;
var forceAllowFile = false;
var blockInactive = false;
var blockCharity = false;
var blockSplit = false;
var blockEmbedded = false;
var statsStrict = false;
var firstMessage = true;
var splitViewBlockedNotified = false;

var totalEntries = 0;
var paused = "";
var addableBlocks = [];
var unblockedTabs = {};
var unblockedBreakTabs = {};
var unblockedBadgeInfo = {};
var allowanceTabsUrl = {};
var allowanceTabsTitle = {};
var mutedTabInfo = {};
// Seed with an empty `blocks` map so the v5 check functions that read
// `blockListInfo.blocks` directly (checkBlockYouTube / checkBlockAdult are
// called from the message handler with no version gate) don't throw
// "Object.entries(undefined)" on a YouTube/adult page that loads before the
// native host has pushed the first policy. Replaced wholesale once a policy
// arrives.
var blockListInfo = { blocks: {} };
var statsYouTube = {};
var restrictYouTubeControls = false;
var allowYouTubeConsent = false;
var refreshTabForRandomTextBreak = -1;
var refreshTabForDelayBreak = -1;
var refreshTabForDelayBreakCancel = -1;
var refreshTabForDelayStart = -1;

var portError = false;
var missingPermissions = false;
var userConsentedDataCollection = false;
const permissionsRequired = {
	origins: ["<all_urls>"]
}

/* Variables for older versions */
var counter = 0;
var diffBlockList = [];
var diffExceptList = [];
var diffTitleList = [];
var currentBlockList = [];
var currentExceptionList = [];
var currentTitleList = [];

/* Entry Point */
chrome.action.disable();
chrome.alarms.create("oneMinute:Timer", { periodInMinutes: 1 });
try {
	port = chrome.runtime.connectNative('com.focusos.blocker');
	if (port) {
		port.onDisconnect.addListener(function(port) {
			handleOnDisconnect(port);
		});
		port.onMessage.addListener(function(list) {
			handleOnMessage(list);
		});
	}
} catch (e) {
	portError = true;
	updateBadge();
	chrome.action.enable();
}
permissionsCheck("userdata");

/* Chrome Event Listeners */
chrome.alarms.onAlarm.addListener(alarmEvent);
chrome.tabs.onUpdated.addListener(function (tabId, change, info) {
	if (blockSplit) {
		if (typeof change.splitViewId == "number" && change.splitViewId > -1) {
			if (info.url != "") {
				chrome.tabs.create({ url: info.url, active: false, index: info.index }, () => {
					chrome.tabs.remove(tabId);
				});
			} else {
				chrome.tabs.create({ url: info.pendingUrl, active: true }, () => {
					chrome.tabs.remove(tabId);
				});
			}
			if (!splitViewBlockedNotified) {
				splitViewBlockedNotified = true;
				chrome.notifications.create('splitViewBlocked', {
					type: 'basic',
					iconUrl: 'icon128.png',
					title: 'Split View Blocked',
					message: 'Extensions aren\'t allowed in split views, so Cold Turkey Blocker opened it in a new tab instead.',
					priority: 2
				});
				setTimeout(() => {
					splitViewBlockedNotified = false;
				}, 8000);
			}
		}
	}
	if (typeof info.status == "string" && typeof info.url == "string" && info.status == "loading") {
		if (info.url.startsWith("chrome://") || info.url.startsWith("chrome-extension://") || info.url.startsWith("brave://") || info.url.startsWith("opera://") || info.url.startsWith("vivaldi://") || info.url.startsWith("sidekick://") || info.url.startsWith("arc://") || info.url.startsWith("https://chromewebstore.google.com") || info.url.startsWith("https://addons.opera.com")) {
			checkBlockUrl(info.url, true, tabId, false, info.active);
		}
	}
});
chrome.runtime.onMessage.addListener(function(request, sender, sendResponse) {
	switch (request.command) {
		case "checkBlockUrl":
			if (typeof sender.tab == 'undefined' || typeof sender.tab.id == 'undefined') { 
				sendResponse({ version: version, block: {action : "none"}, isPro: isPro, blockCharity: blockCharity });
			} else {
				sendResponse({ version: version, block: checkBlockUrl(request.site, true, sender.tab.id, request.embedded, sender.tab.active), isPro: isPro, blockCharity: blockCharity });
			}
			break;
		case "checkBlockTitle":
			if (typeof sender.tab == 'undefined' || typeof sender.tab.id == 'undefined') { 
				sendResponse({ version: version, block: {action : "none"}, isPro: isPro, blockCharity: blockCharity });
			} else {
				sendResponse({ version: version, block: checkBlockTitle(request.title, request.site, true, sender.tab.id, request.embedded, sender.tab.active), isPro: isPro, blockCharity: blockCharity });
			}
			break;
		case "checkBlockYouTube": 
			if (typeof sender.tab == 'undefined' || typeof sender.tab.id == 'undefined') { 
				sendResponse({ version: version, block: {action : "none"}, isPro: isPro, blockCharity: blockCharity });
			} else {
				sendResponse({ version: version, block: checkBlockYouTube(request.url, request.channel, true, sender.tab.id, request.embedded, sender.tab.active), isPro: isPro, blockCharity: blockCharity });
			}
			break;
		case "checkBlockAdult": 
			if (typeof sender.tab == 'undefined' || typeof sender.tab.id == 'undefined') { 
				sendResponse({ version: version, block: {action : "none"}, isPro: isPro, blockCharity: blockCharity }); 
			} else {
				sendResponse({ version: version, block: checkBlockAdult(request.url, true, sender.tab.id, request.embedded, sender.tab.active), isPro: isPro, blockCharity: blockCharity });
			}
			break;
		case "muteTab": 
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				muteTab(sender.tab.id);
			}
			sendResponse({ result: true });
			break;
		case "unmuteTab": 
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				unmuteTab(sender.tab.id);
			}
			sendResponse({ result: true });
			break;
		case "reloadDelay": 
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				reloadTab(sender.tab.id);
			}
			sendResponse({ result: true });
			break;
		case "checkPermissions": 
			permissionsCheck("all");
			break;
		case "setUserConsentedDataCollection":
			userConsentedDataCollection = true;
			break;
		case "getError": 
			sendResponse({ error: portError });
			break;
		case "listBlocks": 
			chrome.storage.local.get(['last-added'], function(result) {
				sendResponse({ version: version, addableBlocks: listBlocks(), itemCount: totalEntries, paused: paused, lastAdded: result["last-added"] });
			});
			break;
		case "getURLs": 
			getURLs();
			sendResponse({ result: true });
			break;
		case "getBadgeData": 
			chrome.storage.local.get(['badge-data'], function(result) {
				sendResponse({ badge: result["badge-data"] });
			});
			break;
		case "setBadgeData": 
			setBadge(request.badge);
			sendResponse({ result: true });
			break;
		case "unblockTab": 
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				unblockTab(sender.tab.id, request.blockId, request.lock, request.duration);
			}
			sendResponse({ result: true });
			break;
		case "startDelay": 
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				startDelay(sender.tab.id, request.blockId);
			}
			sendResponse({ result: true });
			break;
		case "stopDelay": 
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				stopDelay(sender.tab.id, request.blockId);
			}
			sendResponse({ result: true });
			break;
		case "startBreak":
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				unblockBreakTab(sender.tab.id, request.blockId, request.lock, request.duration);
			}
			sendResponse({ result: true });
			break;
		case "startRandomTextBreak":
			if (typeof sender.tab != 'undefined' && typeof sender.tab.id != 'undefined') {
				startRandomTextBreak(sender.tab.id, request.blockId);
			}
			sendResponse({ result: true });
			break;
		case "open-blocker":
			openBlocker();
			sendResponse({ result: true });
			break;
		case "add-block":
			addBlock(request.block, request.url);
			sendResponse({ result: true });
			break;
		case "pause":
			pause(request.key);
			sendResponse({ result: true });
			break;
	}
	return true;
});
chrome.tabs.onActivated.addListener(function activated(tab) {
	checkOpenTabs();
});
chrome.idle.setDetectionInterval(150);
chrome.idle.onStateChanged.addListener(function stateChanged(state) {
	if (state === "active") {
		statsActive = true;
	} else {
		statsActive = false;
	}
});
chrome.permissions.onAdded.addListener(siteAccessPermissionsChanged);
chrome.permissions.onRemoved.addListener(siteAccessPermissionsChanged);

/* Cold Turkey Blocker Methods */

function handleOnMessage(list) {
	portError = false;
	if (typeof list.version == 'string') {
		version = parseInt(list.version);
	} else if (typeof list.version == 'number') {
		version = list.version;		
	}
	if (version == 1) {
		port.postMessage('counter@' + counter + '@@Chrome');
		counter = 0;
	}
	if (version >= 2) {
		if (typeof list.statsEnabled != 'undefined' && list.statsEnabled == 'true'){
			statsEnabled = true;
		} else {
			statsEnabled = false;
		}
		if (typeof list.statsEnabledIncognito != 'undefined' && list.statsEnabledIncognito == 'true'){
			statsEnabledIncognito = true;
		} else {
			statsEnabledIncognito = false;
		}
		if (typeof list.isPro != 'undefined' && (list.isPro == 'true' || list.isPro == 'pro' || list.isPro == 'trial')) {
			isPro = true;
		}
		if (typeof list.ignoreIncognito != 'undefined' && list.ignoreIncognito == 'true') {
			ignoreIncognito = true;
		}
	}
	if (version >= 4) {
		if (typeof list.forceAllowFile != 'undefined' && list.forceAllowFile == 'true'){
			forceAllowFile = true;
		} else {
			forceAllowFile = false;
		}
		if (typeof list.blockInactive != 'undefined' && list.blockInactive == 'true'){
			blockInactive = true;
		} else {
			blockInactive = false;
		}
		if (typeof list.blockCharity != 'undefined' && list.blockCharity == 'true'){
			blockCharity = true;
		} else {
			blockCharity = false;
		}
		if (typeof list.blockSplit != 'undefined' && list.blockSplit == 'true'){
			blockSplit = true;
		} else {
			blockSplit = false;
		}
		if (typeof list.blockEmbedded != 'undefined' && list.blockEmbedded == 'true'){
			blockEmbedded = true;
		} else {
			blockEmbedded = false;
		}
		if (typeof list.statsStrict != 'undefined' && list.statsStrict == 'true'){
			statsStrict = true;
		} else {
			statsStrict = false;
		}
		if (typeof list.paused != 'undefined' && list.paused != 'false'){
			paused = list.paused;
		} else {
			paused = "";
		}
	}
	if (version >= 1 && version <= 3) {
		if (typeof list.blockList != 'undefined' && list.blockList != ''){
			var thisList = list.blockList.split(/(?<!\\)@/g);
			for(var i=0; i < thisList.length; i++) {
				thisList[i] = thisList[i].replace(/\\/g, '');
				thisList[i] = thisList[i].replace(/\.\*/g, "*");
			}
			diffBlockList = thisList.diff(currentBlockList);
			currentBlockList = thisList.slice();
		} else {
			diffBlockList = [];
			currentBlockList = [];
		}
		if (typeof list.exceptionList != 'undefined' && list.exceptionList != '') {
			var thisList = list.exceptionList.split(/(?<!\\)@/g);
			for(var i=0; i < thisList.length; i++) {
				thisList[i] = thisList[i].replace(/\\/g, '');
				thisList[i] = thisList[i].replace(/\.\*/g, "*");
			}
			diffExceptList = thisList.diff(currentExceptionList);
			currentExceptionList = thisList.slice();
		} else {
			diffExceptList = [];
			currentExceptionList = [];
		}
		if (diffBlockList.length > 0 || diffExceptList.length > 0) {
			diffBlockList = [];
			diffExceptList = [];
			checkOpenTabs();
		}
	}
	if (version == 4) {
		if (typeof list.blockList != 'undefined' && list.blockList != ''){
			var thisList = list.blockList.split(/(?<!\\)@/g);
			for(var i=0; i < thisList.length; i++) {
				thisList[i] = thisList[i].replace(/\\/g, '');
			}
			diffBlockList = thisList.diff(currentBlockList);
			currentBlockList = thisList.slice();
		} else {
			diffBlockList = [];
			currentBlockList = [];
		}
		if (typeof list.exceptionList != 'undefined' && list.exceptionList != '') {
			var thisList = list.exceptionList.split(/(?<!\\)@/g);
			for(var i=0; i < thisList.length; i++) {
				thisList[i] = thisList[i].replace(/\\/g, '');
			}
			diffExceptList = thisList.diff(currentExceptionList);
			currentExceptionList = thisList.slice();
		} else {
			diffExceptList = [];
			currentExceptionList = [];
		}
		if (typeof list.titleList != 'undefined' && list.titleList != ''){
			var thisList = list.titleList.split(/(?<!\\)@/g);
			for(var i=0; i < thisList.length; i++) {
				thisList[i] = thisList[i].replace(/\\/g, '');
			}
			diffTitleList = thisList.diff(currentTitleList);
			currentTitleList = thisList.slice();
		} else {
			diffTitleList = [];
			currentTitleList = [];
		}
		if (typeof list.addableBlocks != 'undefined' && list.addableBlocks != ''){
			addableBlocks = list.addableBlocks.match(/(\\.|[^@])+/g);
			for(var i=0; i < addableBlocks.length; i++) {
				addableBlocks[i] = addableBlocks[i].replace(/\\/g, '');
			}
		} else {
			addableBlocks = [];
		}
		if (diffBlockList.length > 0 || diffExceptList.length > 0 || diffTitleList.length > 0) {
			diffBlockList = [];
			diffExceptList = [];
			diffTitleList = [];
			checkOpenTabs();
		}
		totalEntries = currentBlockList.length + currentTitleList.length;
		chrome.action.enable();
		updateBadge();
	}
	if (version == 5) {
		if (typeof list.blockListInfo != 'undefined' && JSON.stringify(blockListInfo) != JSON.stringify(list.blockListInfo)) {
			var entryCount = 0;
			blockListInfo = list.blockListInfo;
			restrictYouTubeControls = false;
			allowYouTubeConsent = false;
			for (let [blockId, block] of Object.entries(blockListInfo.blocks)) {
				entryCount = entryCount + block.blockList.length + block.titleList.length;
				if (typeof block.allowanceUrlList != "undefined") {
					if (block.allowanceUrlList.findIndex(x => x.startsWith("youtube.com")) > -1) {
						restrictYouTubeControls = true;
					}
				}
				if (block.blockList.findIndex(x => x.startsWith("youtube.com")) > -1) {
					restrictYouTubeControls = true;
					if (block.exceptionList.findIndex(x => x.startsWith("youtube.com")) > -1) {
						allowYouTubeConsent = true;
					}
				}
			}
			totalEntries = entryCount;
			checkOpenTabs();
		}
		chrome.action.enable();
		updateBadge();
	}
	
	if (firstMessage) {
		
		firstMessage = false;
		
		if (version >= 4) {
			if (statsTimer != null) {
				clearTimeout(statsTimer); 
				statsTimer = null;
			}
			statsTimer = setInterval(statsCheckv4Tov5, 2000);
		} else {
			if (statsTimer != null) {
				clearTimeout(statsTimer); 
				statsTimer = null;
			}
			statsTimer = setInterval(statsCheckv1Tov3, 1000);
		}
		if (doubleCheckTimer != null) {
			clearTimeout(doubleCheckTimer); 
			doubleCheckTimer = null;
		}
		doubleCheckTimer = setInterval(doubleCheck, 4000);
		
		chrome.storage.local.get(['muted-tabs'], function(result) {
			if (typeof result['muted-tabs'] == 'undefined') {
				chrome.storage.local.set({'muted-tabs' : {} });
			} else {
				mutedTabInfo = result['muted-tabs'];
			}
		});
		
	}

	if (refreshTabForRandomTextBreak > -1) {
		chrome.tabs.reload(refreshTabForRandomTextBreak).catch(function (e) {});
		refreshTabForRandomTextBreak = -1;
	}

	if (refreshTabForDelayBreakCancel > -1) {
		chrome.tabs.reload(refreshTabForDelayBreakCancel).catch(function (e) {});
		refreshTabForDelayBreakCancel = -1;
	}
	
	if (refreshTabForDelayStart > -1) {
		chrome.tabs.reload(refreshTabForDelayStart).catch(function (e) {});
		refreshTabForDelayStart = -1;
	}

	if (refreshTabForDelayBreak > -1) {
		chrome.tabs.reload(refreshTabForDelayBreak).catch(function (e) {});
		refreshTabForDelayBreak = -1;
	}

}

function handleOnDisconnect(port) {
	if (chrome.runtime.lastError) {
		if (typeof chrome.runtime.lastError.message == "string" && chrome.runtime.lastError.message == "Access to the specified native messaging host is forbidden.") {
			portError = true;
			updateBadge();
			chrome.action.enable();
		}
	}
	currentBlockList = [];
	currentExceptionList = [];
	clearInterval(statsTimer);
	statsTimer = null;
	port = null;
}

async function muteTab(tabId) {
	try {
		var tab = await chrome.tabs.get(tabId);
		if (typeof mutedTabInfo[tabId] == "undefined") {
			mutedTabInfo[tabId] = tab.mutedInfo.muted;
		}
		chrome.storage.local.set({'muted-tabs' : mutedTabInfo });
		chrome.tabs.update(tabId, { muted: true }).catch(function (e) {});
	} catch (e) {}
}

function unmuteTab(tabId) {
	try {
		if (typeof mutedTabInfo[tabId] == "boolean") {
			chrome.tabs.update(tabId, { muted: mutedTabInfo[tabId] });
			delete mutedTabInfo[tabId];
			chrome.storage.local.set({'muted-tabs' : mutedTabInfo });
		}
	} catch (e) {}
}

function siteAccessPermissionsChanged() {
	permissionsCheck("siteaccess");
}

function permissionsCheck(permissions) {
	/* FocusOS: removed the Cold Turkey consent/permission/incognito/file nag tabs
	   and their getcoldturkey.com flow. The extension is force-installed by FocusOS
	   with <all_urls> host access granted, so there is nothing to prompt for. We
	   simply treat data collection as consented (it is local-only, fed to the
	   FocusOS native host) and return. */
	userConsentedDataCollection = true;
}

function checkOpenTabs() {
	if (paused == "") {
		var options;
		if (blockInactive) {
			options = {};
		} else {
			options = {active: true};
		}
		chrome.permissions.contains(permissionsRequired).then((result) => {
			missingPermissions = !result;
			chrome.tabs.query(options, function(allActiveTabs) {
				for (var i = 0; i < allActiveTabs.length; i++) {
					if (!allActiveTabs[i].title.startsWith("Blocked by Cold Turkey")) {
						if (!allActiveTabs[i].url.match(/^((https|http):\/\/)(www.)?youtube\.com\/watch\?.*v=.*/) && !allActiveTabs[i].url.match(/^((https|http):\/\/)(www.)?youtube\.com\/playlist\?.*list=.*/)) {
							var checkBlockUrlResponse = checkBlockUrl(allActiveTabs[i].url, false, allActiveTabs[i].id, false, allActiveTabs[i].active);
							if (checkBlockUrlResponse.action == "block") {
								chrome.tabs.reload(allActiveTabs[i].id);
							}
						}
						var checkBlockTitleResponse = checkBlockTitle(allActiveTabs[i].title, allActiveTabs[i].url, false, allActiveTabs[i].id, false, allActiveTabs[i].active);
						if (checkBlockTitleResponse.action == "block") {
							chrome.tabs.reload(allActiveTabs[i].id);
						}
					} else {
						allowanceTabsUrl[allActiveTabs[i].id] = false;
						allowanceTabsTitle[allActiveTabs[i].id] = false;
						updateBadge();
					}
				}
			});
		});
	}
}

function updateBadge() {
	
	if (portError) {
		chrome.action.setBadgeText({ text: ":(" });
		chrome.action.setBadgeBackgroundColor({color: "#DC2626"}); //red
		try { chrome.action.setBadgeTextColor({color: "#ffffff"}); } catch(e) {} /* Chrome 109 on Windows 7 does not support this */
		return;
	}
	
	chrome.storage.local.get(['badge-data'], function(result) {
		var badgeType = result["badge-data"] ? result["badge-data"] : "total";
		chrome.tabs.query(statsStrict ? {} : {lastFocusedWindow: true, active: true}, function (tabs) {
			if (version == 4 && badgeType != "hidden") {
				
				if (paused != "") {
					chrome.action.setBadgeText({text: "II"});
					chrome.action.setBadgeBackgroundColor({color: "#e17d15"}); //yellow
				} else if (totalEntries > 999) {
					chrome.action.setBadgeText({ text: "+1k" });
					chrome.action.setBadgeBackgroundColor({color: "#d9534f"}); //old red
				} else if (totalEntries > 0) {
					chrome.action.setBadgeText({ text: totalEntries.toString() });
					chrome.action.setBadgeBackgroundColor({color: "#d9534f"}); //old red
				} else {
					chrome.action.setBadgeText({ text: "0" });
					chrome.action.setBadgeBackgroundColor({color: "#4cae4c"}); //old green
				}
				try { chrome.action.setBadgeTextColor({color: "#ffffff"}); } catch(e) {} /* Chrome 109 on Windows 7 does not support this */
				
			} else if (version == 5 && badgeType != "hidden") {
				
				if (paused != "") {
					chrome.action.setBadgeText({text: "II"});
					chrome.action.setBadgeBackgroundColor({color: "#e17d15"}); //yellow
				} else if (badgeType == "total") {
					if (totalEntries > 999) {
						chrome.action.setBadgeText({ text: "+1k" });
						chrome.action.setBadgeBackgroundColor({color: "#DC2626"}); //red
					} else if (totalEntries > 0) {
						chrome.action.setBadgeText({ text: totalEntries.toString() });
						chrome.action.setBadgeBackgroundColor({color: "#DC2626"}); //red
					} else {
						chrome.action.setBadgeText({ text: "0" });
						chrome.action.setBadgeBackgroundColor({color: "#4D7C0F"}); //green
					}
				} else {
					if (badgeType.startsWith("break:")) {
						var blockId = badgeType.replace("break:", "");
						if (typeof blockListInfo.blocks[blockId] != "undefined") {
							var allowance = blockListInfo.blocks[blockId].allowance;
							var allowanceRemaining = blockListInfo.blocks[blockId].allowanceRemaining;
							var pomodoroPeriodRemaining = blockListInfo.blocks[blockId].pomodoroPeriodRemaining;
							var pomodoroPeriodState = blockListInfo.blocks[blockId].pomodoroPeriodState;
							if (allowance != "") {
								var suffix = allowance == "x" ? "x": "m";
								var remaining = parseInt(allowanceRemaining, 10);
								if (remaining > 0) {
									if (unblockedBadgeInfo[blockId]) {
										var unblockRemaining = Math.ceil((unblockedBadgeInfo[blockId] - new Date()) / 1000);
										if (0 < unblockRemaining && unblockRemaining < remaining) {
											remaining = unblockRemaining;
										}
									}
									var numDisplay = allowance == "x" ? remaining.toString() : (remaining < 60 ? "<1" : Math.round(remaining / 60).toString());
									chrome.action.setBadgeText({ text: numDisplay + suffix });
									chrome.action.setBadgeBackgroundColor({color: "#4D7C0F"}); //green
								} else {
									chrome.action.setBadgeText({ text: "0" + suffix });
									chrome.action.setBadgeBackgroundColor({color: "#DC2626"}); //red
								}
							} else if (pomodoroPeriodState != "") {
								chrome.action.setBadgeText({ text: parseInt(pomodoroPeriodRemaining, 10) + "m" });
								if (pomodoroPeriodState == "break") {
									chrome.action.setBadgeBackgroundColor({color: "#4D7C0F"}); //green
								} else {
									chrome.action.setBadgeBackgroundColor({color: "#DC2626"}); //red
								}
							} else {
								chrome.action.setBadgeText({ text: "" });
							}
						} else {
							chrome.action.setBadgeText({ text: "" });
						}
					} else if (badgeType.startsWith("lock:")) { 
						var blockId = badgeType.replace("lock:", "");
						if (typeof blockListInfo.blocks[blockId] != "undefined" && typeof blockListInfo.blocks[blockId].lockTimeLeft != "undefined" && blockListInfo.blocks[blockId].lockTimeLeft != "") {
							var minLeft = parseInt(blockListInfo.blocks[blockId].lockTimeLeft, 10);
							if (blockListInfo.blocks[blockId].blockList.length > 0) {
								chrome.action.setBadgeBackgroundColor({color: "#DC2626"}); //red
							} else {
								chrome.action.setBadgeBackgroundColor({color: "#4D7C0F"}); //green
							}
							chrome.action.setBadgeText({ text: formatMinutes(minLeft).toString() });
						} else {
							chrome.action.setBadgeText({ text: "" });
						}
					}
				}

				try { 
					if (Object.values(tabs).some(tab => allowanceTabsUrl[tab.id] || allowanceTabsTitle[tab.id])) {
						if (Object.values(allowanceTabsUrl).some(value => value == true) || Object.values(allowanceTabsTitle).some(value => value == true)) {
							chrome.action.setBadgeBackgroundColor({color: "#e17d15"}); //yellow
						}
					}
				} catch(e) {}
				
			} else {
				chrome.action.setBadgeText({ text: "" });
			}
		});
	});
	try { chrome.action.setBadgeTextColor({color: "#ffffff"}); } catch(e) {} /* Chrome 109 on Windows 7 does not support this */
	
}

function formatMinutes(minutes) {

	const MIN = 60;
	const DAY = 1440;
	const WEEK = DAY * 7;
	const MONTH = DAY * 30;
	const YEAR = DAY * 365;

	if (minutes / YEAR > 99) {
		return "99y";
	}

	let value, unit;
	if (minutes < MIN) {
		value = minutes;
		unit = "m";
	} else if (minutes < DAY) {
		value = minutes / MIN;
		unit = "h";
	} else if (minutes < WEEK) {
		value = minutes / DAY;
		unit = "d";
	} else if (minutes < MONTH) {
		value = minutes / WEEK;
		unit = "w";
	} else if (minutes < YEAR) {
		value = minutes / MONTH;
		unit = "m";
	} else {
		value = minutes / YEAR;
		unit = "y";
	}

	const floorVal = Math.floor(value);
	if (unit !== "m" || value === minutes) { 
		const ceilVal = floorVal + 1;
		if (value > floorVal && value < ceilVal) {
			const plusForm = "+" + floorVal.toString() + unit;
			if (plusForm.length <= 3) {
				return plusForm;
			}
		}
	}

	if (isNaN(floorVal) || floorVal <= 0) {
		return "<1m"
	} else if (unit == "m" && floorVal <= 9) {
		return "+" + floorVal.toString() + "m"
	} else {
		return floorVal.toString() + unit;
	}

}

function openBlocker() {
	port.postMessage('open-blocker');
}

function pause(key) {
	/* FocusOS: remote break/pause via getcoldturkey.com is disabled. A focus
	   session is only ended through FocusOS itself (TOTP-gated), never from
	   inside the browser, so there is no in-browser pause path. */
	sendPauseError("Pausing is disabled. End the session from FocusOS.");
}

function unblockTab(tabId, blockId, blockLock, duration) {
	try {
		var blockShortLock = getShortLock(blockLock);
		var unblockTabId = (duration == "closed") ? tabId : 0;
		if (typeof unblockedTabs[blockId+blockShortLock] == 'undefined') {
			unblockedTabs[blockId+blockShortLock] = [unblockTabId];
		} else {
			unblockedTabs[blockId+blockShortLock].push(unblockTabId);
		}
		if (unblockTabId == 0) {
			chrome.alarms.create("reblockTab:" + blockId+blockShortLock, { delayInMinutes: parseInt(duration) });
			unblockedBadgeInfo[blockId] = dateAdd(new Date(), "minute", parseInt(duration));
		}
		chrome.tabs.reload(tabId).catch(function (e) {});
	} catch (e) {}
}

function unblockBreakTab(tabId, blockId, blockLock, duration) {
	try {
		var blockShortLock = getShortLock(blockLock);
		var unblockTabId = (duration == "closed") ? tabId : 0;
		if (typeof unblockedBreakTabs[blockId+blockShortLock] == 'undefined') {
			unblockedBreakTabs[blockId+blockShortLock] = [unblockTabId];
		} else {
			unblockedBreakTabs[blockId+blockShortLock].push(unblockTabId);
		}
		if (unblockTabId == 0) {
			chrome.alarms.create("reblockBreakTab:" + blockId+blockShortLock, { delayInMinutes: parseInt(duration) });
			unblockedBadgeInfo[blockId] = dateAdd(new Date(), "minute", parseInt(duration));
		}
		chrome.tabs.reload(tabId).catch(function (e) {});
	} catch (e) {}
}

function dateAdd(date, interval, units) {
	if(!(date instanceof Date)) { return undefined; }
	var ret = new Date(date);
	var checkRollover = function() { if(ret.getDate() != date.getDate()) { ret.setDate(0); } };
	switch(String(interval).toLowerCase()) {
		case 'year'   :  ret.setFullYear(ret.getFullYear() + units); checkRollover();  break;
		case 'quarter':  ret.setMonth(ret.getMonth() + 3*units); checkRollover();  break;
		case 'month'  :  ret.setMonth(ret.getMonth() + units); checkRollover();  break;
		case 'week'   :  ret.setDate(ret.getDate() + 7*units);  break;
		case 'day'    :  ret.setDate(ret.getDate() + units);  break;
		case 'hour'   :  ret.setTime(ret.getTime() + units*3600000);  break;
		case 'minute' :  ret.setTime(ret.getTime() + units*60000);  break;
		case 'second' :  ret.setTime(ret.getTime() + units*1000);  break;
		default       :  ret = undefined;  break;
	}
	return ret;
}

function startDelay(tabId, blockId) {

	refreshTabForDelayStart = tabId;
	port.postMessage('start-delay-break@' + blockId.replace(/@/g,'\\@'));

	chrome.notifications.create('startDelay', {
		type: 'basic',
		iconUrl: 'icon128.png',
		title: 'Countdown Started',
		message: 'Your break will start after waiting for the delay countdown.',
		priority: 2
	});

}

function stopDelay(tabId, blockId) {

	refreshTabForDelayBreakCancel = tabId;
	port.postMessage('stop-delay-break@' + blockId.replace(/@/g,'\\@'));

	chrome.notifications.create('stopRandomTextBreak', {
		type: 'basic',
		iconUrl: 'icon128.png',
		title: 'Break Canceled',
		message: 'Your break will be canceled momentarily and this page will refresh automatically.',
		priority: 2
	});

}

function startRandomTextBreak(tabId, blockId) {

	refreshTabForRandomTextBreak = tabId;
	port.postMessage('start-random-text-unblock@' + blockId.replace(/@/g,'\\@'));

	chrome.notifications.create('startRandomTextBreak', {
		type: 'basic',
		iconUrl: 'icon128.png',
		title: 'Break Requested',
		message: 'Your break will start momentarily and this page will refresh automatically.',
		priority: 2
	});

}

function reloadTab(tabId) {
	
	refreshTabForDelayBreak = tabId;

}

function alarmEvent(alarmInfo) {
	
	switch (alarmInfo.name.split(":")[0]) {
		
		case "reblockTab":
			
			var blockIdAndShortLock = alarmInfo.name.replace("reblockTab:","");
			
			if (typeof unblockedTabs[blockIdAndShortLock] != 'undefined') {
				for (var i = 0; i < unblockedTabs[blockIdAndShortLock].length; i++) {
					if (unblockedTabs[blockIdAndShortLock][i] == 0) {
						unblockedTabs[blockIdAndShortLock].splice(i, 1);
					}
				}
			}

			chrome.notifications.create('reblockTab', {
				type: 'basic',
				iconUrl: 'icon128.png',
				title: 'Block Restarted',
				message: 'A block you temporarily unblocked was restarted.',
				priority: 2
			});
			
			break;
			
		case "reblockBreakTab":
			
			var blockIdAndShortLock = alarmInfo.name.replace("reblockBreakTab:","");
			
			if (typeof unblockedBreakTabs[blockIdAndShortLock] != 'undefined') {
				for (var i = 0; i < unblockedBreakTabs[blockIdAndShortLock].length; i++) {
					if (unblockedBreakTabs[blockIdAndShortLock][i] == 0) {
						unblockedBreakTabs[blockIdAndShortLock].splice(i, 1);
					}
				}
			}
			
			chrome.notifications.create('reblockBreakTab', {
				type: 'basic',
				iconUrl: 'icon128.png',
				title: 'Block Restarted',
				message: 'A block you temporarily unblocked was restarted.',
				priority: 2
			});
			
			break;
			
		case "oneMinute":
			
			try {
				
				port.postMessage("port-check");
				
			} catch (ex) {
				
				try { port.disconnect(); } catch (e) { }
				port = null;
				port = chrome.runtime.connectNative('com.focusos.blocker');
				port.onDisconnect.addListener(function(port) {
					handleOnDisconnect(port);
				});
				firstMessage = true;
				port.onMessage.addListener(function(list) {
					handleOnMessage(list);
				});
				
			}
		
	}
	
}

function setBadge(data) {
	chrome.storage.local.set({"badge-data": data });
	updateBadge();
}

function sendPauseError(errorMessage) {
	chrome.notifications.create('pauseError', {
		type: 'basic',
		iconUrl: 'icon128.png',
		title: 'Break Key Invalid',
		message: errorMessage,
		priority: 2
	})
}

function addBlock(blockId, url) {
	chrome.storage.local.set({"last-added": blockId });
	var formattedUrl = decodeURIComponent(url).replace(/@/g,'\\@');
	port.postMessage('add-block@' + blockId.replace(/@/g,'\\@') + '@' + formattedUrl);
}

function getURLs() {
	chrome.tabs.query({active: true, currentWindow: true}, function(allActiveTabs) {
		var allURLs = [];
		for (var i = 0; i < allActiveTabs.length; i++) {
			try {
				var temp = allActiveTabs[i].url.match(/^((http|https|ftp):\/\/)?(www\.)?(.+)\/?/);
				var url = temp[temp.length-1].replace(/\/?$/, '/').replace(/^([^\/]*)\.(?=\/)/, '$1').replace(/\/$/, '').toLowerCase();
				if (!url.includes("//")) {
					var domains = url.split("/")[0];
					if (domains.startsWith("xn--")) {
						try {
							domains = punycode.ToUnicode(domains);
							url = domains + "/" + (typeof url.split("/")[1] == "string" ? url.split("/")[1] : "");
						} catch { }
					}
					var domainsList = domains.split(".");
					if (url.includes('/')) {
						allURLs.push(url);
						if (url.includes('?')) {
							allURLs.push(url.split("?")[0]);
						}
					}
					if (domainsList.length > 2) {
						allURLs.push(domainsList[domainsList.length-2] + '.' + domainsList[domainsList.length-1]);
					}
					allURLs.push(domains);
				} else {
					allURLs.push(url);
				}
			} catch (e) { }
		}
		chrome.runtime.sendMessage({ command: "urls", urls: allURLs }, function(response) { });
	});
}

function listBlocks() {
	
	if (version <= 4) {
		return addableBlocks;
	} else if (version == 5) {
		var blockListNames = [];
		for (let [blockId, block] of Object.entries(blockListInfo.blocks)) {
			blockListNames.push(blockId);
		}
		return blockListNames;
	}
}

function checkBlockUrl(site, countAsBlocked, tabId, embedded, active) {
	
	if (paused == "") {
		if (version >= 5) {
			return checkBlockUrlv5(site, countAsBlocked, tabId, embedded, active);
		} else {
			return (checkBlockUrlv1Tov4(site, countAsBlocked) ? {"action": "block"} : {"action": "none"});
		}
	} else {
		return {"action": "none"};
	}
	
}

function checkBlockUrlv5(site, countAsBlocked, tabId, embedded, active) {
	
	var input = "";
	var domains = "";
	var initUrl = "";
	var result = {"action": "none", "restrictYouTubeControls": restrictYouTubeControls};
	var tabOnAllowance = false;

	if (embedded && !blockEmbedded) {
		return result;
	}

	if (!active && !blockInactive) {
		return result;
	}

	try {
		input = decodeURIComponent(site).toLowerCase();
	} catch(e) {
		input = site.toLowerCase();
	}
	
	if (input.startsWith("https://getcoldturkey.com/blocked/")) {
		return result;
	} else if (input.startsWith("file://") || input.startsWith("chrome://") || input.startsWith("brave://") || input.startsWith("opera://") || input.startsWith("vivaldi://") || input.startsWith("sidekick://") || input.startsWith("arc://") || input.startsWith("chrome-extension://") || input.startsWith("extension://")) {
		initUrl = input;
	} else {
		try {
			var arrInitUrl = input.match(/^(view-source:)?((http|https|ftp):\/\/)?(.+)\/?/);
			initUrl = removeYoutubeAtSign(arrInitUrl[arrInitUrl.length-1].replace(/\/$/, ""));
			domains = initUrl.split("/")[0].replace(/\.$/, "");
			if (domains.startsWith("xn--")) {
				try {
					domains = punycode.ToUnicode(domains);
					initUrl = domains + "/" + (typeof initUrl.split("/")[1] == "string" ? initUrl.split("/")[1] : "");
				} catch { }
			}
		} catch (e) {
			initUrl = input;
		}
	}

	for (let [blockId, block] of Object.entries(blockListInfo.blocks)) {

		var blockLock = ""
		var blockIdShortLock = ""
		if (typeof block.lock != "undefined") {
			blockLock = block.lock;
			blockIdShortLock = blockId + getShortLock(block.lock);
		}
		
		if (typeof unblockedTabs[blockIdShortLock] == "undefined" || (!(unblockedTabs[blockIdShortLock].includes(0)) && !(unblockedTabs[blockIdShortLock].includes(tabId)))) {

			for (var i = 0; i < block.blockList.length; i++) {
				if (block.blockList[i].includes("*")) {
					if (input.startsWith("chrome://") || input.startsWith("brave://") || input.startsWith("opera://") || input.startsWith("vivaldi://") || input.startsWith("sidekick://") || input.startsWith("arc://")) {
						if (block.blockList[i].startsWith("chrome://") || block.blockList[i].startsWith("brave://") || block.blockList[i].startsWith("opera://") || block.blockList[i].startsWith("vivaldi://") || block.blockList[i].startsWith("sidekick://") || block.blockList[i].startsWith("arc://")) {
							if (input.includes("://newtab") || input.includes("://extensions") || input.includes("://welcome")) {
								continue;
							}
						} else {
							continue;
						}	
					} else if (input.startsWith("chrome-extension:") || input.startsWith("extension:")) {
						continue;
					}
				}
				
				var regexBlock = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.blockList[i])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
				if ((domains.match(regexBlock) || initUrl.match(regexBlock)) && !(allowYouTubeConsent && domains.endsWith("consent.youtube.com"))) {
					result = {"action": "block", "type": "url", "url": site, "blockId": blockId, "rule": block.blockList[i], "lock": blockLock, "password": block.password, "randomText": block.randomTextLength};
					break;
				}
			}
			
			for (var j = 0; j < block.exceptionList.length; j++) {
				var regexAllow = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.exceptionList[j])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
				if (domains.match(regexAllow) || initUrl.match(regexAllow)) {
					result = {"action": "allow", "type": "url", "url": site, "blockId": blockId, "rule": block.exceptionList[j], "restrictYouTubeControls": restrictYouTubeControls};
					break;
				}
			}
		}
			
		if (!embedded) {
			if (result.action == "none" && typeof block.allowanceUrlList != "undefined") {
				for (var k = 0; k < block.allowanceUrlList.length; k++) {
					var regexAllowance = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.allowanceUrlList[k])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
					if (domains.match(regexAllowance) || initUrl.match(regexAllowance)) {
						if (blockLock == "break-usage" && (typeof unblockedBreakTabs[blockIdShortLock] == "undefined" || (!(unblockedBreakTabs[blockIdShortLock].includes(0)) && !(unblockedBreakTabs[blockIdShortLock].includes(tabId))))) {
							result = {"action": "block", "type": "url", "url": site, "blockId": blockId, "rule": block.allowanceUrlList[k], "lock": blockLock, "password": "", "randomText": ""};
							break;
						} else {
							tabOnAllowance = true;
							allowanceTabsUrl[tabId] = true;
							break;
						}
					}
				}
			}
		}
		
		if (result.action == "block") {
			allowanceTabsUrl[tabId] = false;
			updateBadge();
			if (countAsBlocked) {
				if (userConsentedDataCollection && statsEnabled && port != null) {
					port.postMessage('blocked@' + initUrl.replace(/@/g,'\\@'));
				}
			}
			if (missingPermissions || input.startsWith("chrome://") || input.startsWith("brave://") || input.startsWith("opera://") || input.startsWith("vivaldi://") || input.startsWith("sidekick://") || input.startsWith("arc://") || input.startsWith("view-source:") || input.startsWith("file://") || input.startsWith("https://chromewebstore.google.com") || input.startsWith("https://addons.opera.com") || input.startsWith("chrome-extension://") || input.startsWith("extension://")) {
				let removing = chrome.tabs.remove(tabId);
				removing.then(
					function() {
						chrome.notifications.create(tabId.toString(), {
							type: 'basic',
							iconUrl: 'icon128.png',
							title: 'Tab Blocked',
							message: 'A tab was blocked because it matched the URL \'' + result.rule + '\' found in your \'' + blockId + '\' block.',
							priority: 2
						});
					}, function() { /* error */ }
				);
				return {"action": "none"};
			}
			return result;
		}
		
	}
	
	if (!tabOnAllowance) {
		allowanceTabsUrl[tabId] = false;
	}
	updateBadge();
	
	return result;
	
}

function checkBlockUrlv1Tov4(site, countAsBlocked) {
	
	var input = decodeURI(site);
	var domains = '';
	var initUrl = '';
	
	if (input.startsWith("chrome") || input.startsWith("brave") || input.startsWith("vivaldi") || input.startsWith("https://chrome.google.com/webstore") || input.startsWith("https://getcoldturkey.com/blocked/")) {
		return false;
	} else if (input.startsWith("file://") || input.startsWith("chrome-extension://") || input.startsWith("moz-extension://") || input.startsWith("extension://")) {
		var lastIndex = input.lastIndexOf("#") > 0 ? input.lastIndexOf("#") : input.length;
		initUrl = input.substring(0, lastIndex).toLowerCase();
	} else {
		try {
			var arrInitUrl = input.match(/^((http|https|ftp):\/\/)?(.+)\/?/);
			initUrl = arrInitUrl[arrInitUrl.length-1].replace(/\/$/, "").toLowerCase();
			domains = initUrl.split("/")[0].replace(/\.$/, "");
		} catch (e) {
			initUrl = input;
		}
	}
	
	for (var i = 0; i < currentBlockList.length; i++) {
		var regexBlock = new RegExp("^(.*\\.)?" + escapeRegExp(currentBlockList[i].replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
		if (domains.match(regexBlock) || initUrl.match(regexBlock)) {
			for (var j = 0; j < currentExceptionList.length; j++) {
				var regexAllow = new RegExp("^(.*\\.)?" + escapeRegExp(currentExceptionList[j].replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
				if (domains.match(regexAllow) || initUrl.match(regexAllow)) {
					return false;
				}
			}
			if (countAsBlocked) {
				if (version == 1) {
					counter++;
				} else {
					if (userConsentedDataCollection && statsEnabled) {
						port.postMessage('blocked@' + initUrl);
					}
				}
			}
			return true;
    	}
	}
	
	return false;
	
}

function checkBlockTitle(title, site, countAsBlocked, tabId, embedded, active) {
	
	if (paused == "") {
		if (version >= 5) {
			return checkBlockTitlev5(title, site, countAsBlocked, tabId, embedded, active);
		} else {
			return (checkBlockTitlev4(title, site, countAsBlocked) ? {"action": "block"} : {"action": "none"});
		}
	} else {
		return {"action": "none"};
	}
	
}

function checkBlockTitlev5(title, site, countAsBlocked, tabId, embedded, active) {
	
	var input = '';
	var domains = '';
	var initUrl = '';
	var result = {"action": "none"};
	var tabOnAllowance = false;
	
	if (embedded && !blockEmbedded) {
		return result;
	}

	if (!active && !blockInactive) {
		return result;
	}

	if (title == "") {
		return result;
	}
	
	try {
		input = decodeURIComponent(site).toLowerCase();
	} catch(e) {
		input = site.toLowerCase();
	}
	
	if (input.startsWith("https://getcoldturkey.com/blocked/")) {
		return result;
	} else if (input.startsWith("file://") || input.startsWith("chrome://") || input.startsWith("brave://") || input.startsWith("opera://") || input.startsWith("vivaldi://") || input.startsWith("sidekick://") || input.startsWith("arc://") || input.startsWith("chrome-extension://") || input.startsWith("extension://")) {
		initUrl = input;
	} else {
		try {
			var arrInitUrl = input.match(/^((http|https|ftp):\/\/)?(.+)\/?/);
			initUrl = removeYoutubeAtSign(arrInitUrl[arrInitUrl.length-1].replace(/\/$/, ""));
			domains = initUrl.split("/")[0].replace(/\.$/, "");
		} catch (e) {
			initUrl = input;
		}
	}
	
	for (let [blockId, block] of Object.entries(blockListInfo.blocks)) {

		var blockLock = ""
		var blockIdShortLock = ""
		if (typeof block.lock != "undefined") {
			blockLock = block.lock;
			blockIdShortLock = blockId + getShortLock(block.lock);
		}
		
		if (typeof unblockedTabs[blockIdShortLock] == "undefined" || (!(unblockedTabs[blockIdShortLock].includes(0)) && !(unblockedTabs[blockIdShortLock].includes(tabId)))) {
			
			for (var i = 0; i < block.titleList.length; i++) {
				if (block.titleList[i].includes("*") && (input.startsWith("chrome://") || input.startsWith("brave://") || input.startsWith("opera://") || input.startsWith("vivaldi://") || input.startsWith("sidekick://") || input.startsWith("arc://"))) {
					continue;
				}
				
				var regexBlock = new RegExp(("^" + escapeRegExp(block.titleList[i]) + "$").toLowerCase());
				if (regexBlock.test(title.toLowerCase())) {
					result = {"action": "block", "type": "title", "url": site, "blockId": blockId, "rule": block.titleList[i], "lock": blockLock, "password": block.password, "randomText": block.randomTextLength};
					break;
				}
			}
			
			for (var j = 0; j < block.exceptionList.length; j++) {
				var regexAllow = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.exceptionList[j])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
				if (domains.match(regexAllow) || initUrl.match(regexAllow)) {
					result = {"action": "allow", "type": "url", "url": site, "blockId": blockId, "rule": block.exceptionList[j]};
					break;
				}
			}
			
		}

		if (!embedded) {
			if (result.action == "none" && typeof block.allowanceTitleList != "undefined") {
				for (var k = 0; k < block.allowanceTitleList.length; k++) {
					var regexAllowance = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.allowanceTitleList[k])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
					if (domains.match(regexAllowance) || initUrl.match(regexAllowance)) {
						if (blockLock == "break-usage" && (typeof unblockedBreakTabs[blockIdShortLock] == "undefined" || (!(unblockedBreakTabs[blockIdShortLock].includes(0)) && !(unblockedBreakTabs[blockIdShortLock].includes(tabId))))) {
							result = {"action": "block", "type": "url", "url": site, "blockId": blockId, "rule": block.allowanceTitleList[k], "lock": blockLock, "password": "", "randomText": ""};
							break;
						} else {
							tabOnAllowance = true;
							allowanceTabsTitle[tabId] = true;
							break;
						}
					}
				}
			}
		}
			
		if (result.action == "block") {
			allowanceTabsTitle[tabId] = false;
			updateBadge();
			if (countAsBlocked) {
			if (userConsentedDataCollection && statsEnabled && port != null) {
					port.postMessage('blocked@' + initUrl.replace(/@/g,'\\@'));
				}
			}
			if (input.startsWith("chrome://") || input.startsWith("brave://") || input.startsWith("opera://") || input.startsWith("vivaldi://") || input.startsWith("sidekick://") || input.startsWith("arc://") || input.startsWith("view-source:") || input.startsWith("file://") || input.startsWith("https://chromewebstore.google.com") || input.startsWith("https://addons.opera.com") || input.startsWith("chrome-extension://") || input.startsWith("extension://")) {
				let removing = chrome.tabs.remove(tabId);
				removing.then(
					function() {
						chrome.notifications.create(tabId.toString(), {
							type: 'basic',
							iconUrl: 'icon128.png',
							title: 'Tab Blocked',
							message: 'A tab was blocked because it matched the title \'' + result.rule + '\' found in your \'' + blockId + '\' block.',
							priority: 2
						});
					}, function() { /* error */ }
				);
				return result = {"action": "none"};
			} else {
				return result;
			}
		}
		
	}
	
	if (!tabOnAllowance) {
		allowanceTabsTitle[tabId] = false;
	}
	updateBadge();
	
	return {"action": "none"};
	
}

function checkBlockTitlev4(title, site, countAsBlocked) {
	
	for (var i = 0; i < currentTitleList.length; i++) {
		var regexBlock = new RegExp(("^" + escapeRegExp(currentTitleList[i]) + "$").toLowerCase());
		if (regexBlock.test(title.toLowerCase())) {
			if (countAsBlocked) {
				if (version == 1) {
					counter++;
				} else {
					if (userConsentedDataCollection && statsEnabled) {
						if (site.startsWith('file://') || site.startsWith('chrome-extension://') || site.startsWith('moz-extension://') || site.startsWith('extension://')) {
							port.postMessage('blocked@' + decodeURIComponent(site).replace(/\#.*$/, "").replace(/@/g,'\\@'));
						} else if (site.startsWith('ftp://') || site.startsWith('http://') || site.startsWith('https://')) {
							var domainInit = decodeURIComponent(site).match(/^((ftp|http|https):\/\/)?(www\.)?(.+)\/?/);
							if (domainInit != null && typeof domainInit[domainInit.length-1] != 'undefined') {
								port.postMessage('blocked@' + domainInit[domainInit.length-1].replace(/\/$/, "").replace(/@/g,'\\@'));
							}
						}
					}
				}
			}
			return true;
    	}
	}
	
	return false;
	
}

function checkBlockYouTube(url, channel, countAsBlocked, tabId, embedded, active) {
	
	var input = "";
	var domains = "";
	var initUrl = "";
	var inputChannel = "";
	var domainsChannel = "";
	var initUrlChannel = "";
	var result = {"action": "none", "restrictYouTubeControls": restrictYouTubeControls};
	var tabOnAllowance = false;
	var stats = [true, channel];
	
	if (paused != "") {
		return result;
	}

	if (embedded && !blockEmbedded) {
		return result;
	}

	if (!active && !blockInactive) {
		return result;
	}
	
	try {
		input = decodeURIComponent(url).toLowerCase();
		inputChannel = decodeURIComponent(channel).toLowerCase();
	} catch(e) {
		input = url.toLowerCase();
		inputChannel = channel.toLowerCase();
	}
	
	try {
		var arrInitUrl = input.match(/^(view-source:)?((http|https|ftp):\/\/)?(.+)\/?/);
		initUrl = removeYoutubeAtSign(arrInitUrl[arrInitUrl.length-1].replace(/\/$/, ""));
		domains = initUrl.split("/")[0].replace(/\.$/, "");
	} catch (e) {
		initUrl = input;
	}

	try {
		var arrInitUrlChannel = inputChannel.match(/^(view-source:)?((http|https|ftp):\/\/)?(.+)\/?/);
		initUrlChannel = removeYoutubeAtSign(arrInitUrlChannel[arrInitUrlChannel.length-1].replace(/\/$/, ""));
		domainsChannel = initUrlChannel.split("/")[0].replace(/\.$/, "");
	} catch (e) {
		initUrlChannel = inputChannel;
	}

	for (let [blockId, block] of Object.entries(blockListInfo.blocks)) {

		var blockLock = ""
		var blockIdShortLock = ""
		if (typeof block.lock != "undefined") {
			blockLock = block.lock;
			blockIdShortLock = blockId + getShortLock(block.lock);
		}
		
		if (typeof unblockedTabs[blockIdShortLock] == "undefined" || (!(unblockedTabs[blockIdShortLock].includes(0)) && !(unblockedTabs[blockIdShortLock].includes(tabId)))) {
			
			for (var i = 0; i < block.blockList.length; i++) {
				
				var skipChannelCheck = false;
				if (domainsChannel == "" || initUrlChannel == "" || !(block.blockList[i]).toLowerCase().startsWith("youtube.com/")) {
					skipChannelCheck = true;
				}
				
				var regexBlock = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.blockList[i])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
				if (domains.match(regexBlock) || initUrl.match(regexBlock)) {
					result = {"action": "block", "type": "url", "url": url, "blockId": blockId, "rule": block.blockList[i], "lock": blockLock, "password": block.password, "randomText": block.randomTextLength};
					stats = [false, channel];
					break;
				} else if (!skipChannelCheck && (domainsChannel.match(regexBlock) || initUrlChannel.match(regexBlock))) {
					result = {"action": "block", "type": "url", "url": url, "blockId": blockId, "rule": block.blockList[i], "lock": blockLock, "password": block.password, "randomText": block.randomTextLength};
					stats = [true, channel];
					break;
				}
			}
			
			for (var j = 0; j < block.exceptionList.length; j++) {
				
				var skipChannelCheck = false;
				if (domainsChannel == "" || initUrlChannel == "" || !(block.exceptionList[j]).toLowerCase().startsWith("youtube.com/")) {
					skipChannelCheck = true;
				}
				
				var regexAllow = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.exceptionList[j])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
				if (domains.match(regexAllow) || initUrl.match(regexAllow)) {
					result = {"action": "allow", "type": "url", "url": url, "blockId": blockId, "rule": block.exceptionList[j], "restrictYouTubeControls": restrictYouTubeControls};
					stats = [false, channel];
					break;
				} else if (!skipChannelCheck && (domainsChannel.match(regexAllow) || initUrlChannel.match(regexAllow))) {
					result = {"action": "allow", "type": "url", "url": url, "blockId": blockId, "rule": block.exceptionList[j], "restrictYouTubeControls": restrictYouTubeControls};
					stats = [true, channel];
					break;
				}
			}
			
		}
		
		if (!embedded) {
			if (result.action == "none" && typeof block.allowanceUrlList != "undefined") {
				for (var k = 0; k < block.allowanceUrlList.length; k++) {
					var foundMatch = false;
					var regexAllowance = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.allowanceUrlList[k])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
					if (domains.match(regexAllowance) || initUrl.match(regexAllowance)) {
						foundMatch = true;
						stats = [false, channel];
					} else if (!skipChannelCheck && (domainsChannel.match(regexAllowance) || initUrlChannel.match(regexAllowance))) {
						foundMatch = true;
						stats = [true, channel];
					}
					if (foundMatch) {
						if (blockLock == "break-usage" && (typeof unblockedBreakTabs[blockIdShortLock] == "undefined" || (!(unblockedBreakTabs[blockIdShortLock].includes(0)) && !(unblockedBreakTabs[blockIdShortLock].includes(tabId))))) {
							result = {"action": "block", "type": "url", "url": url, "blockId": blockId, "rule": block.allowanceUrlList[k], "lock": blockLock, "password": "", "randomText": ""};
							break;
						} else {
						tabOnAllowance = true;
						allowanceTabsUrl[tabId] = true;
						break;
						}
					}
				}
			}
		}
		
		statsYouTube[url] = stats;
		
		if (result.action == "block") {
			allowanceTabsUrl[tabId] = false;
			updateBadge();
			if (countAsBlocked) {
				if (userConsentedDataCollection && statsEnabled && port != null) {
					if (stats[0]) {
						port.postMessage('blocked@' + initUrlChannel.replace(/@/g,'\\@'));
					} else {
						port.postMessage('blocked@' + initUrl.replace(/@/g,'\\@'));
					}
				}
			}
			if (missingPermissions) {
				let removing = chrome.tabs.remove(tabId);
				removing.then(
					function() {
						chrome.notifications.create(tabId.toString(), {
							type: 'basic',
							iconUrl: 'icon128.png',
							title: 'Tab Blocked',
							message: 'A tab was blocked because it matched the URL \'' + result.rule + '\' found in your \'' + blockId + '\' block.',
							priority: 2
						});
					}, function() { /* error */ }
				);
				return {"action": "none"};
			}
			return result;
		}
		
	}
	
	if (!tabOnAllowance) {
		allowanceTabsUrl[tabId] = false;
	}
	updateBadge();
	
	return result;
	
}

function checkBlockAdult(url, countAsBlocked, tabId, embedded, active) {

	var input = "";
	var domains = "";
	var initUrl = "";
	var result = {"action": "none"};

	if (paused != "") {
		return result;
	}

	if (embedded && !blockEmbedded) {
		return result;
	}

	if (!active && !blockInactive) {
		return result;
	}
	
	try {
		input = decodeURIComponent(url).toLowerCase();
	} catch(e) {
		input = url.toLowerCase();
	}
	
	try {
		var arrInitUrl = input.match(/^(view-source:)?((http|https|ftp):\/\/)?(.+)\/?/);
		initUrl = arrInitUrl[arrInitUrl.length-1].replace(/\/$/, "");
		domains = initUrl.split("/")[0].replace(/\.$/, "");
	} catch (e) {
		initUrl = input;
	}
	
	for (let [blockId, block] of Object.entries(blockListInfo.blocks)) {

		var blockLock = ""
		var blockIdShortLock = ""
		if (typeof block.lock != "undefined") {
			blockLock = block.lock;
			blockIdShortLock = blockId + getShortLock(block.lock);
		}
		
		if (typeof unblockedTabs[blockIdShortLock] == "undefined" || (!(unblockedTabs[blockIdShortLock].includes(0)) && !(unblockedTabs[blockIdShortLock].includes(tabId)))) {
			
			for (var i = 0; i < block.blockList.length; i++) {
				if (block.blockList[i] == "*.xxx") {
					result = {"action": "block", "type": "url", "url": url, "blockId": blockId, "rule": block.blockList[i], "lock": blockLock, "password": block.password, "randomText": block.randomTextLength};
					break;
				}
			}
			
			for (var j = 0; j < block.exceptionList.length; j++) {
				var regexAllow = new RegExp("^([^\\/]*\\.)?" + escapeRegExp(removeYoutubeAtSign(removePercentEncoding(block.exceptionList[j])).replace(/\/$/, "").toLowerCase()) + "((\\/|\\?|\\#)(.*)?$|$)");
				if (domains.match(regexAllow) || initUrl.match(regexAllow)) {
					result = {"action": "allow", "type": "url", "url": url, "blockId": blockId, "rule": block.exceptionList[j]};
					break;
				}
			}
		}

		if (!embedded) {
			if (result.action == "none" && typeof block.allowanceUrlList != "undefined") {
				for (var k = 0; k < block.allowanceUrlList.length; k++) {
					if (block.allowanceUrlList[k] == "*.xxx") {
						if (blockLock == "break-usage" && (typeof unblockedBreakTabs[blockIdShortLock] == "undefined" || (!(unblockedBreakTabs[blockIdShortLock].includes(0)) && !(unblockedBreakTabs[blockIdShortLock].includes(tabId))))) {
							result = {"action": "block", "type": "url", "url": url, "blockId": blockId, "rule": block.allowanceUrlList[k], "lock": blockLock, "password": "", "randomText": ""};
							break;
						} else {
							tabOnAllowance = true;
							allowanceTabsUrl[tabId] = true;
							break;
						}
					}
				}
			}
		}
		
		if (result.action == "block") {
			allowanceTabsUrl[tabId] = false;
			updateBadge();
			if (countAsBlocked) {
				if (userConsentedDataCollection && statsEnabled && port != null) {
					port.postMessage('blocked@' + initUrl.replace(/@/g,'\\@'));
				}
			}
			if (missingPermissions) {
				let removing = chrome.tabs.remove(tabId);
				removing.then(
					function() {
						chrome.notifications.create(tabId.toString(), {
							type: 'basic',
							iconUrl: 'icon128.png',
							title: 'Tab Blocked',
							message: 'A tab was blocked because it matched the URL \'' + result.rule + '\' found in your \'' + blockId + '\' block.',
							priority: 2
						});
					}, function() { /* error */ }
				);
				return {"action": "none"};
			}
			return result;
		}

	}
	
	return result;
	
}

function statsCheckv1Tov3() {
	if (userConsentedDataCollection && statsEnabled) {
		chrome.tabs.query({lastFocusedWindow: true, active: true}, function(tabs){
			if (typeof tabs[0] != 'undefined' && typeof tabs[0].url != 'undefined' && !tabs[0].url.startsWith("chrome") && !tabs[0].url.startsWith("edge") && !tabs[0].url.startsWith("brave") && !tabs[0].url.startsWith("opera") && !tabs[0].url.startsWith("vivaldi") && !tabs[0].url.startsWith("file://") && !tabs[0].title.startsWith("Blocked by Cold Turkey")) {	
				try {
					chrome.windows.get(tabs[0].windowId, function(activeWindow){
						if (activeWindow.focused && (!activeWindow.incognito || activeWindow.incognito && statsEnabledIncognito) && (statsActive || activeWindow.state === 'fullscreen')) {
							var domainInit = tabs[0].url.match(/^((ftp|http|https):\/\/)?(www\.)?(.+)\/?/);
							var domains = domainInit[domainInit.length-1].replace(/\/$/, "").split("/")[0];
							port.postMessage('stats@' + domains);
						}
					});
				} catch (e) {
				}
			}
		});		
	}
}

function statsCheckv4Tov5() {
	if (userConsentedDataCollection && statsEnabled) {
		try {
			var streamingRegex = /^https:\/\/(www\.)?(youtube\.com\/watch.*|netflix\.com\/watch.*|disneyplus\.com\/.*|hulu\.com\/.*|hbomax\.com\/.*|tv\.apple\.com\/.*|primevideo\.\/.*|vimeo\.com\/.*|dailymotion\.com\/video.*)/;
			chrome.windows.getLastFocused(function (lastActiveWindow) {
				chrome.tabs.query({lastFocusedWindow: true, active: true}, function (tabs) {
					var activeTabId = -1;
					tabs.forEach(function (tab) {
						let tabUrlLower = tab.url.toLowerCase();
						var statsActiveWithQualifiers = (lastActiveWindow.focused && (statsActive || lastActiveWindow.state == 'fullscreen' || RegExp(streamingRegex).test(tabUrlLower)));
						if ((tab.active && !tab.title.startsWith("Blocked by Cold Turkey")) && (!lastActiveWindow.incognito || (lastActiveWindow.incognito && statsEnabledIncognito)) && (statsStrict || statsActiveWithQualifiers)) {
							
							var logAsStrict = (statsStrict && !statsActiveWithQualifiers);
							activeTabId = tab.id;
							
							try {
								port.postMessage((logAsStrict ? "titleStrictStats@" : "titleStats@") + tab.title.replace(/@/g,'\\@'));
								
								if (tabUrlLower.startsWith('file://') || tabUrlLower.startsWith('chrome-extension://') || tabUrlLower.startsWith('extension://')) {
									var formattedUrl = decodeURIComponent(tabUrlLower).replace(/@/g,'\\@');
									port.postMessage((logAsStrict ? "strictStats@" : "stats@") + formattedUrl);
								} else if (tabUrlLower.startsWith('view-source:') || tabUrlLower.startsWith('ftp://') || tabUrlLower.startsWith('http://') || tabUrlLower.startsWith('https://')) {
									var formattedUrl = removeYoutubeAtSign(tab.url).toLowerCase();
									if ((RegExp(/^((https|http):\/\/)(www.)?youtube\.com\/watch\?.*v=.*/).test(formattedUrl) || RegExp(/^((https|http):\/\/)(www.)?youtube\.com\/playlist\?.*list=.*/).test(formattedUrl)) && statsYouTube[tab.url]) {
										if (statsYouTube[tab.url][0]) {
											formattedUrl = removeYoutubeAtSign(statsYouTube[tab.url][1]).toLowerCase();
										}
									}
									var arrInitUrl = decodeURIComponent(formattedUrl).match(/^(view-source:)?((ftp|http|https):\/\/)?(www\.)?(.+)\/?/);
									if (typeof arrInitUrl[arrInitUrl.length-1] != 'undefined') {
										var initUrl = arrInitUrl[arrInitUrl.length-1].replace(/\/?$/, '/').replace(/^([^\/]*)\.(?=\/)/, '$1').replace(/\/$/, '');
										var domains = initUrl.split("/")[0];
										if (domains.startsWith("xn--")) {
											try {
												domains = punycode.ToUnicode(domains);
												initUrl = domains + "/" + (typeof initUrl.split("/")[1] == "string" ? initUrl.split("/")[1] : "");
											} catch { }
										}
										port.postMessage((logAsStrict ? "strictStats@" : "stats@") + initUrl.replace(/@/g,'\\@'));
									}
								}
							} catch (e) {
								/* Error can occur if port is disconnected by service worker */
							}
							
						}
					});
					if (statsStrict) {
						chrome.tabs.query({discarded: false}, function (allTabs) {
							allTabs.forEach(function (inactiveTab) {
								let tabUrlLower = inactiveTab.url.toLowerCase();
								if (inactiveTab.id != activeTabId && !inactiveTab.title.startsWith("Blocked by Cold Turkey") && (!inactiveTab.incognito || (inactiveTab.incognito && statsEnabledIncognito))) {
									
									try {
										port.postMessage('titleStrictStats@' + inactiveTab.title.replace(/@/g,'\\@'));
										
										if (tabUrlLower.startsWith('file://') || tabUrlLower.startsWith('chrome-extension://') || tabUrlLower.startsWith('extension://')) {
											var formattedUrl = decodeURIComponent(tabUrlLower).replace(/@/g,'\\@');
											port.postMessage('strictStats@' + formattedUrl);
										} else if (tabUrlLower.startsWith('view-source:') || tabUrlLower.startsWith('ftp://') || tabUrlLower.startsWith('http://') || tabUrlLower.startsWith('https://')) {
											var formattedUrl = removeYoutubeAtSign(tab.url).toLowerCase();
											if ((RegExp(/^((https|http):\/\/)(www.)?youtube\.com\/watch\?.*v=.*/).test(formattedUrl) || RegExp(/^((https|http):\/\/)(www.)?youtube\.com\/playlist\?.*list=.*/).test(formattedUrl)) && statsYouTube[tab.url]) {
												if (statsYouTube[tab.url][0]) {
													formattedUrl = removeYoutubeAtSign(statsYouTube[tab.url][1]).toLowerCase();
												}
											}
											var arrInitUrl = decodeURIComponent(formattedUrl).match(/^(view-source:)?((ftp|http|https):\/\/)?(www\.)?(.+)\/?/);
											if (typeof arrInitUrl[arrInitUrl.length-1] != 'undefined') {
												var initUrl = arrInitUrl[arrInitUrl.length-1].replace(/\/?$/, '/').replace(/^([^\/]*)\.(?=\/)/, '$1').replace(/\/$/, '');
												var domains = initUrl.split("/")[0];
												if (domains.startsWith("xn--")) {
													try {
														domains = punycode.ToUnicode(domains);
														initUrl = domains + "/" + (typeof initUrl.split("/")[1] == "string" ? initUrl.split("/")[1] : "");
													} catch { }
												}
												port.postMessage('strictStats@' + initUrl.replace(/@/g,'\\@'));
											}
										}
									} catch (e) {
										/* Error can occur if port is disconnected by service worker */
									}
									
								}
							});
						});
					}
				});
			});
		} catch (e) {}
	}
}

function doubleCheck() {
	
	try {
		
		port.postMessage("port-check");
		checkOpenTabs();
		
	} catch (ex) {
		
		try { port.disconnect(); } catch (e) { }
		port = null;
		port = chrome.runtime.connectNative('com.focusos.blocker');
		port.onDisconnect.addListener(function(port) {
			handleOnDisconnect(port);
		});
		firstMessage = true;
		port.onMessage.addListener(function(list) {
			handleOnMessage(list);
		});
		
	}
	
}

/* Tools */

function getShortLock(str) {
	return str.split(",").slice(0, 3).join(",");
}

function removePercentEncoding(str) {
	try {
		return decodeURIComponent(str);
	} catch(e) {
		return str;
	}
}

function escapeRegExp(str) {
	var initStr = str.replace(/[\-\[\]\/\{\}\(\)\+\?\^\$\|]/g, "\\$&");
	var regexStr = initStr.replace(/\./g, "\\.").replace(/\*/g, ".*");
	return regexStr;
}

function removeYoutubeAtSign(str) {
	if (str.match(/(www.)?youtube\.com\/@.*/)) {
		return str.replace("youtube.com/@", "youtube.com/");
	} else if (str.match(/(www.)?youtube\.com\/user\/.*/)) {
		return str.replace("youtube.com/user/", "youtube.com/");
	} else {
		return str;
	}
}

Array.prototype.diff = function(a) {
    return this.filter(function(i) {return a.indexOf(i) < 0;});
};

var punycode = new function Punycode() {
    this.utf16 = {
        decode:function(input){
            var output = [], i=0, len=input.length,value,extra;
            while (i < len) {
                value = input.charCodeAt(i++);
                if ((value & 0xF800) === 0xD800) {
                    extra = input.charCodeAt(i++);
                    if ( ((value & 0xFC00) !== 0xD800) || ((extra & 0xFC00) !== 0xDC00) ) {
                        throw new RangeError("UTF-16(decode): Illegal UTF-16 sequence");
                    }
                    value = ((value & 0x3FF) << 10) + (extra & 0x3FF) + 0x10000;
                }
                output.push(value);
            }
            return output;
        },
        encode:function(input){
            var output = [], i=0, len=input.length,value;
            while (i < len) {
                value = input[i++];
                if ( (value & 0xF800) === 0xD800 ) {
                    throw new RangeError("UTF-16(encode): Illegal UTF-16 value");
                }
                if (value > 0xFFFF) {
                    value -= 0x10000;
                    output.push(String.fromCharCode(((value >>>10) & 0x3FF) | 0xD800));
                    value = 0xDC00 | (value & 0x3FF);
                }
                output.push(String.fromCharCode(value));
            }
            return output.join("");
        }
    }

    var initial_n = 0x80;
    var initial_bias = 72;
    var delimiter = "\x2D";
    var base = 36;
    var damp = 700;
    var tmin=1;
    var tmax=26;
    var skew=38;
    var maxint = 0x7FFFFFFF;

    function decode_digit(cp) {
        return cp - 48 < 10 ? cp - 22 : cp - 65 < 26 ? cp - 65 : cp - 97 < 26 ? cp - 97 : base;
    }

    function encode_digit(d, flag) {
        return d + 22 + 75 * (d < 26) - ((flag != 0) << 5);
    }

    function adapt(delta, numpoints, firsttime ) {
        var k;
        delta = firsttime ? Math.floor(delta / damp) : (delta >> 1);
        delta += Math.floor(delta / numpoints);

        for (k = 0; delta > (((base - tmin) * tmax) >> 1); k += base) {
                delta = Math.floor(delta / ( base - tmin ));
        }
        return Math.floor(k + (base - tmin + 1) * delta / (delta + skew));
    }

    function encode_basic(bcp, flag) {
        bcp -= (bcp - 97 < 26) << 5;
        return bcp + ((!flag && (bcp - 65 < 26)) << 5);
    }

    this.decode=function(input,preserveCase) {
        var output=[];
        var case_flags=[];
        var input_length = input.length;

        var n, out, i, bias, basic, j, ic, oldi, w, k, digit, t, len;

        n = initial_n;
        i = 0;
        bias = initial_bias;

        basic = input.lastIndexOf(delimiter);
        if (basic < 0) basic = 0;

        for (j = 0; j < basic; ++j) {
            if(preserveCase) case_flags[output.length] = ( input.charCodeAt(j) -65 < 26);
            if ( input.charCodeAt(j) >= 0x80) {
                throw new RangeError("Illegal input >= 0x80");
            }
            output.push( input.charCodeAt(j) );
        }

        for (ic = basic > 0 ? basic + 1 : 0; ic < input_length; ) {

            for (oldi = i, w = 1, k = base; ; k += base) {
                    if (ic >= input_length) {
                        throw RangeError ("punycode_bad_input(1)");
                    }
                    digit = decode_digit(input.charCodeAt(ic++));

                    if (digit >= base) {
                        throw RangeError("punycode_bad_input(2)");
                    }
                    if (digit > Math.floor((maxint - i) / w)) {
                        throw RangeError ("punycode_overflow(1)");
                    }
                    i += digit * w;
                    t = k <= bias ? tmin : k >= bias + tmax ? tmax : k - bias;
                    if (digit < t) { break; }
                    if (w > Math.floor(maxint / (base - t))) {
                        throw RangeError("punycode_overflow(2)");
                    }
                    w *= (base - t);
            }

            out = output.length + 1;
            bias = adapt(i - oldi, out, oldi === 0);

            if ( Math.floor(i / out) > maxint - n) {
                throw RangeError("punycode_overflow(3)");
            }
            n += Math.floor( i / out ) ;
            i %= out;

            if (preserveCase) { case_flags.splice(i, 0, input.charCodeAt(ic -1) -65 < 26);}

            output.splice(i, 0, n);
            i++;
        }
        if (preserveCase) {
            for (i = 0, len = output.length; i < len; i++) {
                if (case_flags[i]) {
                    output[i] = (String.fromCharCode(output[i]).toUpperCase()).charCodeAt(0);
                }
            }
        }
        return this.utf16.encode(output);
    };

    this.encode = function (input,preserveCase) {

        var n, delta, h, b, bias, j, m, q, k, t, ijv, case_flags;

        if (preserveCase) {
            case_flags = this.utf16.decode(input);
        }
        input = this.utf16.decode(input.toLowerCase());

        var input_length = input.length;

        if (preserveCase) {
            for (j=0; j < input_length; j++) {
                case_flags[j] = input[j] != case_flags[j];
            }
        }

        var output=[];

        n = initial_n;
        delta = 0;
        bias = initial_bias;

        for (j = 0; j < input_length; ++j) {
            if ( input[j] < 0x80) {
                output.push(
                    String.fromCharCode(
                        case_flags ? encode_basic(input[j], case_flags[j]) : input[j]
                    )
                );
            }
        }

        h = b = output.length;

        if (b > 0) output.push(delimiter);

        while (h < input_length) {

            for (m = maxint, j = 0; j < input_length; ++j) {
                ijv = input[j];
                if (ijv >= n && ijv < m) m = ijv;
            }

            if (m - n > Math.floor((maxint - delta) / (h + 1))) {
                throw RangeError("punycode_overflow (1)");
            }
            delta += (m - n) * (h + 1);
            n = m;

            for (j = 0; j < input_length; ++j) {
                ijv = input[j];

                if (ijv < n ) {
                    if (++delta > maxint) return Error("punycode_overflow(2)");
                }

                if (ijv == n) {
                    for (q = delta, k = base; ; k += base) {
                        t = k <= bias ? tmin : k >= bias + tmax ? tmax : k - bias;
                        if (q < t) break;
                        output.push( String.fromCharCode(encode_digit(t + (q - t) % (base - t), 0)) );
                        q = Math.floor( (q - t) / (base - t) );
                    }
                    output.push( String.fromCharCode(encode_digit(q, preserveCase && case_flags[j] ? 1:0 )));
                    bias = adapt(delta, h + 1, h == b);
                    delta = 0;
                    ++h;
                }
            }

            ++delta, ++n;
        }
        return output.join("");
    }

    this.ToASCII = function ( domain ) {
        var domain_array = domain.split(".");
        var out = [];
        for (var i=0; i < domain_array.length; ++i) {
            var s = domain_array[i];
            out.push(
                s.match(/[^A-Za-z0-9-]/) ?
                "xn--" + punycode.encode(s) :
                s
            );
        }
        return out.join(".");
    }
    this.ToUnicode = function ( domain ) {
        var domain_array = domain.split(".");
        var out = [];
        for (var i=0; i < domain_array.length; ++i) {
            var s = domain_array[i];
            out.push(
                s.match(/^xn--/) ?
                punycode.decode(s.slice(4)) :
                s
            );
        }
        return out.join(".");
    }
}();
