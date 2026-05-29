document.addEventListener("DOMContentLoaded", function() {
	document.getElementById("request-permissions-button").addEventListener("click", function() {
		const permissionsRequired = {
			origins: ["<all_urls>"]
		}
		chrome.permissions.request(permissionsRequired).then((result) => {
			if (!result) {
				chrome.runtime.sendMessage({command: "checkPermissions"});
			} else {
				chrome.extension.isAllowedIncognitoAccess(function(isAllowedAccess) {
					if (!isAllowedAccess) {
						chrome.runtime.sendMessage({command: "checkPermissions"});
					} else {
						chrome.notifications.create('permissionGranted', {
							type: 'basic',
							iconUrl: 'icon128.png',
							title: 'Permissions Granted',
							message: 'Thank you for enabling these permissions. You can close this tab now.',
							priority: 2
						});
						window.close();
					}
				});
			}
		});
	});
	document.getElementById("deny-permissions-button").addEventListener("click", function() {
		chrome.management.uninstallSelf();
	});
});