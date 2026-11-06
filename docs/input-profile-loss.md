# Input actions changed after an r11 update

The user reported that an inner left thumb key appeared to trigger failure and
Backspace actions no longer matched their usual positions. Both typing and
lighting were affected. Turning Splash off did not restore the key actions.

Read-only Focus queries on the installed `v1.5.0-splash11` image showed:

- Splash was already disabled before the explicit off command was sent.
- Only layer 0 was active; this was not an observed stuck higher layer.
- `keymap.onlyCustom` was 0, selecting the built-in layers.
- All 800 custom key entries were 65535, the erased/transparent value.
- Settings validity was false, although the stored/calculated slice CRC matched.

The current keyboard therefore lacked the saved user layout. The built-in
fallback has different thumb-key actions. This directly explains changed actions
without requiring the matrix decoder to have swapped hands. Compiled decoder,
keymap and storage baseline checks alone cannot detect missing live user settings.

Bazecor's local update log contained reconnect failures, including “Keyboard not
found after update,” followed by cancellation of the update dialog. The latest
two pre-update backups had identical keymaps, superkeys and macros. These facts
are consistent with an incomplete post-flash restore. They do not establish the
exact instant the configuration was lost or prove that a thumb press erased it.
No before/after keypress configuration trace was available for that claim.

The latest pre-update profile was restored via existing settings commands.
Before writing, every affected component was saved locally, and the current
keymap was checked again to ensure it was still erased. Restoring superkeys and
macros preceded the keymap, with custom-only mode and default layer applied last.
All 31 restored settings were read back and matched the backup. Settings validity
then returned true. Splash remained disabled. No firmware was flashed, and pairing,
macro execution, upgrade/reset commands, or backed-up Splash runtime controls
were not replayed. Physical typing confirmation is separate from these readbacks.

Snapshots, actual mappings, macros and detailed update logs remain in ignored
local verification files. They are not published in this repository.

## Checking a profile after future updates

Restore the saved Bazecor profile, then check the usual keys before turning on
Splash. The helper supports a read-only comparison before enabling it:

```bash
python tools/splash.py on --verify-backup /path/to/Bazecor-backup.json
```

The five compared components are `keymap.custom`, `keymap.onlyCustom`,
`settings.defaultLayer`, `superkeys.map`, and `macros.map`. On a mismatch, this
invocation sends no Splash enable command. The helper does not restore settings.
Use `status --verify-backup ...` to compare without changing Splash state.

The pinned Flash storage adapter inherits a reported length of zero, so the
stock `eeprom.contents` command returned an empty result. That result must not
be treated as a successful raw EEPROM backup; the recovery used semantic
component snapshots and readback verification instead.
