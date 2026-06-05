# Running FocusOS on macOS (Apple Silicon, no Developer account)

This is the **research / personal-machine** setup. It trades away some of your
Mac's built-in security to get FocusOS's full enforcement without paying Apple's
$99/yr Developer Program fee. Read [`../../docs/macos.md`](../../docs/macos.md) first for the
capability background; this file is the hands-on procedure.

> **Tested target:** Apple Silicon (M-series), macOS 26. Intel Macs use a
> different SIP/boot-args flow — see the note at the end.

---

## What needs what

Not everything requires disabling SIP. Know what you're paying for:

| Feature | Needs root (`sudo`) | Needs SIP off | Needs AMFI relaxed |
| --- | :---: | :---: | :---: |
| Fullscreen kiosk (hide Dock/menu bar, block ⌘-Tab / Force-Quit) | – | – | – |
| Quit other apps on engage | – | – | – |
| **Network lock (pf firewall)** | ✅ | – | – |
| Display sleep / inhibit, watchdog | – | – | – |
| **Aqua shell lockdown (Dock/Mission Control/Finder/Spotlight off)** | ✅ | ✅ | – |
| **App-launch blocker (Endpoint Security `AUTH_EXEC`)** | ✅ | ✅ | maybe |

**Takeaway:** the kiosk, the pf firewall, app-quitting, and the watchdog all work
on a stock Mac (SIP on) — you just run FocusOS with `sudo` for the firewall. The
SIP-off is what makes the strongest macOS posture possible: FocusOS can persistently
disable the user's launchd-managed Aqua shell agents while a strict routine runs,
then restore exactly those labels on routine end/admin unlock. Endpoint Security
is still the pre-emptive app-launch blocker; without it, FocusOS falls back to
terminating newly launched Dock-visible GUI apps immediately after launch.

---

## Step 0 — Build and sign (no reboot, safe to do now)

```bash
packaging/macos/build-and-sign.sh
```

This builds `build/focusos.app` and ad-hoc-signs it. **The script checks
`csrutil status` and only embeds the Endpoint Security entitlement once SIP is
off.** That detail matters: `com.apple.developer.endpoint-security.client` is a
*restricted* entitlement, and if it's present while SIP is **on**, the kernel
SIGKILLs the app the instant it launches — you'd see exactly this:

```
zsh: killed     sudo build/focusos.app/Contents/MacOS/focusos
```

So while SIP is on the script signs **without** the entitlement, leaving a binary
that launches fine. Try it with `sudo` right away to confirm the firewall + kiosk
work:

```bash
sudo build/focusos.app/Contents/MacOS/focusos
```

You should get the fullscreen shell; starting a routine with a network allowlist
loads the pf rules. The pre-emptive Endpoint Security blocker is simply off in
this build (it falls back to quit-after-launch) — it switches on after Step 1.

---

## Step 1 — Disable SIP (one-time, in Recovery)

This is the part you haven't done before. Take it slowly; nothing here is
irreversible (Step 4 puts it all back).

1. **Shut the Mac down completely.**  menu → **Shut Down…**  (not Restart).
2. **Boot into Recovery.** Press and **hold** the **power button** — keep holding
   for ~10 seconds, past the Apple logo, until you see **“Loading startup
   options”**. (A quick tap won't do it; you must hold.)
3. Click **Options**, then **Continue**. If asked, pick your admin user and enter
   its password.
4. In the menu bar at the top: **Utilities → Terminal**.
5. In that Terminal, type exactly:
   ```
   csrutil disable
   ```
   Press Return. When it asks you to confirm, type **`y`** and Return, then enter
   your admin account name/password if prompted.
   You should see *“System Integrity Protection is off.”*
6. Reboot:  menu → **Restart**.

Back in normal macOS, verify in a Terminal:

```bash
csrutil status      # should say: System Integrity Protection status: disabled.
```

> You do **not** need `csrutil authenticated-root disable` — FocusOS never
> modifies the sealed system volume. Leave that one alone.

---

## Step 2 — Re-sign (now with the entitlement) and run

Now that SIP is off, **re-run the build/sign script** — it will detect the change
and embed the Endpoint Security entitlement this time:

```bash
packaging/macos/build-and-sign.sh        # prints "SIP is disabled … signing WITH …"
sudo build/focusos.app/Contents/MacOS/focusos
```

Start a routine. If the app-launch blocker is working, trying to open a blocked
app (say, Messages) is **denied before it opens** — not opened-then-quit.
With SIP off, a strict routine also disables the Aqua shell surface:
`com.apple.Dock.agent`, Finder, Spotlight, SystemUIServer, Control Center,
Notification Center, Siri, and `talagent`. FocusOS records the labels it changed
in `~/.focusos/macos-ui-lockdown.state` and writes a helper restore script at
`~/.focusos/restore-macos-ui.sh`.

Two possible failure modes mean AMFI is still rejecting the self-signed
entitlement — both call for Step 3:

- The app is **SIGKILLed at launch again** (`zsh: killed` / exit 137) — AMFI still
  refuses to *load* a binary carrying the restricted entitlement, or
- the app launches but the log shows `es_new_client … code 7`
  (`ES_NEW_CLIENT_RESULT_ERR_NOT_ENTITLED`).

---

## Step 3 — (Only if Step 2 failed) Relax AMFI

AMFI is the subsystem that validates entitlements. With SIP off you can tell it to
stop second-guessing self-signed binaries via a boot-arg:

```bash
sudo nvram boot-args="amfi_get_out_of_my_way=0x1"
```

Reboot, then re-run Step 2. Check the arg stuck with `nvram boot-args`.

**Honest caveat:** `amfi_get_out_of_my_way` has historically worked on Intel and
older Apple Silicon, but Apple keeps tightening AMFI on newer Macs/OS versions, and
on the very latest Apple Silicon + macOS 26 it may be partially or fully ignored.
If ES *still* won't start after this:

- The firewall, kiosk, app-quitting, and watchdog **all still work** — you lose
  only the pre-emptive launch blocker, which degrades to the same "quit it after it
  opens" behavior the cross-platform code already has.
- The only guaranteed way to get ES on a locked-down newer Mac is the Apple-granted
  `com.apple.developer.endpoint-security.client` entitlement (a real Developer
  account), which is the $99 you were avoiding. That's the genuine trade-off.

---

## Step 4 — Reverse everything (when you're done experimenting)

Put your Mac back to stock security:

```bash
sudo nvram -d boot-args          # remove the AMFI boot-arg (if you set one)
```

Then re-enable SIP from Recovery (same boot steps as Step 1, Utilities → Terminal):

```
csrutil enable
```

Restart. Confirm with `csrutil status` → *enabled*. Your Mac is back to normal.

To remove the pf firewall rules if a routine left them loaded:

```bash
sudo pfctl -f /etc/pf.conf       # restore Apple's default ruleset
sudo pfctl -d                    # disable pf
```

To restore macOS UI agents if FocusOS is killed or the machine reboots mid-routine:

```bash
scripts/focusos-restore-macos-ui.sh
# or, if FocusOS generated it during a routine:
~/.focusos/restore-macos-ui.sh
```

---

## Notes & gotchas

- **Full Disk Access.** Some Endpoint Security event access also wants the binary
  in **System Settings → Privacy & Security → Full Disk Access**. If ES starts but
  behaves oddly, add `build/focusos.app` (or the `focusos` binary) there.
- **Running a GUI app as root.** Launching with `sudo` from your own Terminal works
  because it inherits your login `WindowServer` session. If the window doesn't
  appear, make sure you launched it from a Terminal you're logged into (not over
  plain SSH).
- **pf is shared, global state.** FocusOS's `dropNetworkPolicy` restores
  `/etc/pf.conf` and disables pf. If you run your *own* pf rules, re-apply them
  after a routine ends.
- **This build is not distributable.** An ad-hoc signature + SIP-off only runs on a
  machine you've personally unlocked. Shipping to anyone else needs Developer-ID
  signing + notarization (and, for ES/Network-Extension, Apple-granted entitlements).

## Intel Macs (for reference)

The flow differs: reboot holding **⌘-R** to enter Recovery, then `csrutil disable`.
Boot-args are set the same way (`sudo nvram boot-args=…`). A firmware password, if
set, must be entered to reach Recovery. Everything else above applies.
