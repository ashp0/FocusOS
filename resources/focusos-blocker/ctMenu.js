document.addEventListener("DOMContentLoaded", function() {
	if (document.getElementById("menu") != null && document.getElementById("urls") != null && document.getElementById("lists-addable") != null) {
		document.getElementById("urls").innerText = "";
		document.getElementById("lists-addable").innerText = "";
		
		document.getElementById("add-block").addEventListener('click', function() {
			chrome.runtime.sendMessage({ command: "add-block", block: document.getElementById("lists-addable").value, url: document.getElementById("urls").value }, function(response) { 
				this.close();
			});
		});
		document.getElementById("badge-data").addEventListener('change', function(element) {
			chrome.runtime.sendMessage({ command: "setBadgeData", badge: element.target.value });
		});
		
		chrome.runtime.sendMessage({ command: "getError" }, function(response) {
			if (response.error) {
				document.getElementById("item-count").innerText = "Blocker isn't installed or can't start\nbecause of a missing registry key or file.\n\nIf the app is installed, please close and reopen\nCold Turkey Blocker and click 'Fix Error' in the\n'Install Extension' dialog that appears.\n\nTo install the app or if clicking 'Fix Error'\ndoesn't work, please run the installer.";
				document.getElementById("open-blocker").innerText = "Download Installer";
				document.getElementById("open-blocker").href = "https://getcoldturkey.com/download/";
				document.getElementById("open-blocker").target = "_blank";
				document.getElementById("add-to-list").style.display = "none";
				document.getElementById("dark-menu").style.display = "none";
			} else {
				document.getElementById("open-blocker").innerText = "Open Cold Turkey Blocker";
				document.getElementById("open-blocker").href = "#";
				document.getElementById("open-blocker").addEventListener('click', function() {
					chrome.runtime.sendMessage({ command: "open-blocker" }, function(response) {
						this.close();
					});
				});
				document.getElementById("add-to-list").style.display = "block";
				document.getElementById("dark-menu").style.display = "block";
				chrome.runtime.sendMessage({ command: "listBlocks" }, function(response) {
					var lists = document.getElementById("lists-addable");
					var badge = document.getElementById("badge-data");
					if (response.paused != "") {
						var pausedSplit = response.paused.split(",");
						var pauseEnd = new Date(pausedSplit[0], pausedSplit[1]-1, pausedSplit[2], pausedSplit[3], pausedSplit[4], pausedSplit[5]);
						var diffMins = Math.round((((pauseEnd - (new Date())) % 86400000) % 3600000) / 60000);
						document.getElementById("item-count").innerText = diffMins.toString() + " min remaining of your pause!";
					} else {
						document.getElementById("item-count").innerText = response.itemCount.toString() + ' item(s) being blocked right now. ';
					}
					response.addableBlocks.sort(function (a, b) {
						return a.toLowerCase().localeCompare(b.toLowerCase());
					}).forEach(function(blockName) {
						var option = document.createElement("option");
						if (blockName.length >= 40) {
							option.text = blockName.substring(0,37) + "...";
						} else {
							option.text = blockName;
						}
						option.value = blockName;
						lists.add(option);
						if (response.version >= 5) {
							var breakOption = document.createElement("option");
							var breakLock = document.createElement("option");
							if (blockName.length >= 40) {
								breakOption.text = blockName.substring(0,31) + "... break";
								breakLock.text = blockName.substring(0,31) + "... lock";
							} else {
								breakOption.text = blockName + " break";
								breakLock.text = blockName + " lock";
							}
							breakOption.value = "break:" + blockName;
							breakLock.value = "lock:" + blockName;
							badge.add(breakOption);
							badge.add(breakLock);
						}
					});
					if (typeof response.lastAdded != "undefined" && response.addableBlocks.indexOf(response.lastAdded) > -1) {
						lists.value = response.lastAdded;
					}
				});
				chrome.runtime.sendMessage({ command: "getBadgeData" }, function(response) {
					if (response.badge == "hidden" || response.badge == "total") {
						document.getElementById("badge-data").value = response.badge;
					} else {
						for (i = 0; i < document.getElementById("badge-data").length; ++i) {
							if (document.getElementById("badge-data").options[i].value == response.badge) {
								document.getElementById("badge-data").value = response.badge;
								return;
							}
						}
						document.getElementById("badge-data").value = "hidden";
					}
				});
				chrome.runtime.onMessage.addListener(function(request, sender, sendResponse) {
					switch (request.command) {
						case "urls":
							const urls = document.getElementById("urls");
							urls.innerHTML = "";
							request.urls.sort((a, b) => b.length - a.length).forEach(url => {
								const option = document.createElement("option");
								option.value = url;
								option.text = url.length >= 40 ? url.slice(0, 37) + "..." : url;
								urls.add(option);
							});
							sendResponse({ response: true });
							break;
					}
					return true;
				});
				chrome.runtime.sendMessage({ command: "getURLs" }, function(response) { });
				
			}
			
		});
	}
});