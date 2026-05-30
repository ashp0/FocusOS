# FocusOS Workflow Extensibility Audit

This audit checks whether FocusOS supports a wider range of daily workflows
without weakening the mission-control posture.

## Profiles Reviewed

1. Student
   - Supported: study routines, min-time floors, allowed research URLs, mission
     notes, historical debrief timeline.
   - Implemented gap: break cadence controls are exposed in the routine editor
     so study blocks can include planned rest windows.
   - Open gap: class schedule import and assignment due dates need product
     decisions. Tracked in `questions.md`.

2. Remote Knowledge Worker
   - Supported: always-allowed work apps, timeboxed access, browser allowlists,
     daily target telemetry.
   - Implemented gap: right-sidebar tabs now separate routine telemetry, notes,
     and timeline so current work is not buried under history.
   - Open gap: calendar/meeting integration is non-trivial. Tracked in
     `questions.md`.

3. Shift Worker
   - Supported: configurable routine lengths, access windows, persistent logs.
   - Implemented gap: day timeline can navigate to any previous date rather
     than assuming only "today" matters.
   - Open gap: custom day-boundary support for overnight shifts needs a product
     choice. Tracked in `questions.md`.

4. Creative / Artist
   - Supported: long offline routines, local audio, notes as a mission log,
     always-allowed creative tools.
   - Implemented gap: notes are now a full readable/editable sidebar panel, not
     a tiny drawer.
   - Open gap: artifact capture for images, drafts, exports, and references
     needs a storage model. Tracked in `questions.md`.

5. Researcher
   - Supported: allowlisted research domains, mission notes, day debriefs,
     historical session lookup.
   - Implemented gap: timeline entries include routine result, duration, and
     note text by date.
   - Open gap: citation/library integrations need product choices. Tracked in
     `questions.md`.

6. Developer
   - Supported: routine apps with command arguments, IDE/project launchers,
     network allowlists, optional allowed terminal commands.
   - Implemented gap: terminal emulators are now killed unless explicitly part
     of the routine, preserving developer workflows without leaving a generic
     shell escape open.
   - Open gap: process supervision/cgroups for app trees is still a deeper Linux
     boundary. Tracked in `questions.md`.

7. Executive / Manager
   - Supported: focused planning windows, access windows, daily/weekly telemetry
     through the production graph.
   - Implemented gap: timeline tab gives a quick day-level debrief without
     pushing history into the primary routine screen.
   - Open gap: agenda, delegation, and contact-aware workflows need calendar and
     task integrations. Tracked in `questions.md`.

8. Athlete / Trainer
   - Supported: timed blocks, break cadence controls, stay-awake display mode,
     daily targets.
   - Implemented gap: per-routine display-awake preference is now preserved by
     the settings editor instead of being dropped on save.
   - Open gap: workout interval templates and wearable import need product
     decisions. Tracked in `questions.md`.

9. Caregiver / Household Operator
   - Supported: simple routines, persistent logs, always-allowed communication
     apps if configured.
   - Implemented gap: notes and timeline make interruptions visible without
     requiring a separate app.
   - Open gap: emergency bypass policy and shared-device handoff need explicit
     product rules. Tracked in `questions.md`.

## Implemented During This Audit

- Right sidebar split into tabs: Routines, Notes, Timeline.
- Notes rebuilt into a full sidebar editor plus historical notes/log viewing.
- Calendar-style debrief timeline added for any date backed by archived session
  notes.
- Routine editor now preserves `keep_display_awake`.
- Routine editor exposes break frequency and break duration controls.
- Active mission view shows the next rest check/rest window when a routine has
  break cadence configured.
- Terminal lockdown now allows terminals only when explicitly listed for a
  routine or always-allowed app.
