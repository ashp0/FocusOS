/* FocusOS Blocker — local blocked page.
   Renders why a navigation was blocked. There is intentionally NO unblock,
   break, delay, or password path here: a focus session ends only through
   FocusOS itself. */

function b64url_to_unicode(str) {
	str = str.replace(/-/g, '+').replace(/_/g, '/');
	while (str.length % 4) { str += '='; }
	const binary = atob(str);
	const bytes = Array.from(binary, c => '%' + c.charCodeAt(0).toString(16).padStart(2, '0'));
	return decodeURIComponent(bytes.join(''));
}

try {
	const params = new URLSearchParams(window.location.search);
	const raw = params.get('reason');
	if (raw) {
		const reason = JSON.parse(b64url_to_unicode(raw));
		const siteEl = document.getElementById('site');
		const metaEl = document.getElementById('meta');
		const shown = reason.url || reason.rule || '';
		if (shown) {
			siteEl.textContent = shown;
			siteEl.style.display = 'inline-block';
		}
		if (reason.blockId) {
			metaEl.innerHTML = 'Matched <span class="accent">' +
				(reason.rule ? String(reason.rule).replace(/[<>&]/g, '') : 'the allow-list') +
				'</span> in your <span class="accent">' +
				String(reason.blockId).replace(/[<>&]/g, '') + '</span> routine.';
		}
	}
} catch (e) {
	/* Missing/badly formed reason — the generic message in blocked.html is enough. */
}
