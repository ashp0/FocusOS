# FocusOS Product Questions

The clear implementation gaps from the workflow audit were addressed in code.
These remaining gaps need product-level choices before implementation.

## Calendar And Meetings

Problem: students, remote workers, executives, and caregivers need external
calendar context, but FocusOS currently has no account or sync model.

Approach A: local-first `.ics` import. Users point FocusOS at exported calendar
files. Low trust burden, offline-friendly, limited live updates.

Approach B: provider sync for Google/Apple/Outlook calendars. Better day view
and meeting awareness, but requires OAuth, token storage, privacy rules, and
network exceptions.

## Overnight Shift Day Boundaries

Problem: shift workers may want a "day" to run from 19:00 to 07:00 instead of
midnight to midnight.

Approach A: global custom day start hour in settings. Simple and predictable.

Approach B: per-routine day attribution. More precise for mixed schedules, but
harder to explain in telemetry and timeline views.

## Artifact Capture

Problem: artists, researchers, developers, and executives need evidence of
outputs, not only minutes and notes.

Approach A: attach file paths or URLs manually to the finished-session prompt.
Simple, explicit, no filesystem watcher.

Approach B: watch configured project folders during a routine and suggest
changed files as artifacts. More useful, but needs privacy controls and noise
filtering.

## Citation And Research Library Integration

Problem: researchers need paper/library context in the mission log.

Approach A: add plain BibTeX/CSL file import and attach selected citations to a
session.

Approach B: integrate with Zotero over its local API. Better workflow, but
requires dependency detection and a failure mode when Zotero is closed.

## Process Supervision Boundary

Problem: a permitted app can spawn helper processes, browsers, or shells that
FocusOS only catches by deny-list today.

Approach A: launch routine apps in a systemd user scope/cgroup and inspect child
processes during lockdown. Practical on Linux, moderate implementation cost.

Approach B: use per-routine sandboxing through bubblewrap/AppArmor/Landlock.
Stronger, but app compatibility and profile authoring become product concerns.

## Emergency Bypass Policy

Problem: caregivers and shared-device users may need urgent access without
destroying the discipline model.

Approach A: keep TOTP as the only emergency bypass and document that households
should share the code with trusted adults.

Approach B: add a separate emergency unlock with mandatory reflection and audit
trail. More humane for real emergencies, but easy to overuse unless carefully
designed.
