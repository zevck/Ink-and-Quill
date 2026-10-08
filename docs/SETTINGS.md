# Settings and the MCM

Code: `Settings` (the INI), `Papyrus` (the MCM's natives), `Source/Scripts/InkAndQuill_MCM.psc` (the SkyUI menu, on `InkAndQuillMCMQuest`, `0x802`), `Clients` (the mod list), `WritingTools` (`[Materials]`, renaming used inkwells).

## The INI

`SKSE\Plugins\InkAndQuill.ini`. Every setting is an integer row in `Settings::kAll` (`[Materials]` aside: lists read by `WritingTools`, also from other mods' own files, [below](#other-mods-quills-and-inkwells)) (section, key, default, range), read at `kDataLoaded` and clamped. A missing file or key is its default; nothing ships an INI. Besides the settings, the INI holds a quill pose saved in adjust mode (`[QuillPose]`, [EDITOR.md](EDITOR.md#quill-cursor)). The MCM changes a setting through `SetSetting`, which writes that one key back at once (`WritePrivateProfileStringA`, so the rest of the file is kept) and takes effect immediately: every reader asks `Settings::Get` when it needs the value.

| Setting | Default | Range | What |
|---|---|---|---|
| `Keys.Edit` | 61 (F3) | 1–255 | With a book open: start writing, if a client owns the book or it's a blank. While writing: save and read again |
| `Writing.RequireQuillAndInk` | 1 | 0–1 | 0: writing needs no quill and no ink, offers no blood and costs nothing (`Editor`'s `g_free`, decided when writing starts) |
| `Writing.InkwellUses` | 10 | 0–100 | Saves a full inkwell lasts; 0: inkwells never run dry (none is used or renamed; the MCM shows "Infinite") |
| `Writing.Blood` | 1 | 0–1 | With a quill but no ink, offer to write in blood. 0: the `NeedsInk` notice instead (a HUD notice on a blank) |
| `Writing.BloodCost` | 10 | 1–100 | Percent of maximum health each save in blood costs (never below 1 health left) |
| `Writing.QuillCursor` | 1 | 0–1 | While writing, the game's quill sits at the caret, in place of the blinking caret. 0: the plain caret ([EDITOR.md](EDITOR.md#quill-cursor)) |
| `Debug.QuillAdjust` | 0 | 0–1 | MCM "Quill adjust mode": shows the quill whatever `Writing.QuillCursor` says, keeps the real caret beside it, and lets the numpad pose it ([EDITOR.md](EDITOR.md#quill-cursor)) |
| `Debug.Logging` | 0 | 0–1 | MCM "Debug logging": `InkAndQuill.log` at debug level (the quill's per-frame lines: the page's vertices and sheets, the camera, where things land on screen), set at once and at load |

The key is a DirectX scan code, not one that types (while writing it couldn't also type). The MCM refuses, with a message, a code that isn't a keyboard key (SkyUI also offers mouse and gamepad buttons, 256 and up) or a key that types or edits (`GetKeyProblem`, i.e. `Keys::Check`, also used for clients' keys: Escape, Backspace, Enter, Delete, the arrows, Home, End, the modifiers, and anything that gives a character with the player's layout). It doesn't check conflicts: it only acts while a book is open, where game controls don't apply. Clients' own keys (Physical Diaries' new entry and tear-out) are theirs, in their own settings.

**Changing `InkwellUses`:** used inkwells are renamed against the new maximum, keeping their uses left but never above it ("Inkwell (7/10)": with 20, "(7/20)"; with 5, "(5/5)"), when the inventory, a container, a shop or the gift menu opens: the player's own, and the opened container's (not a shop's stock, which is in the merchant's chest). Any other is renamed on its next use, which reads its own count back (`UsesLeft`, any `(n/m)`) the same way. With 0 (never run dry) names are left alone.

## Other mods' quills and inkwells

Items from other mods count as quills or inkwells when an INI names them, in a `[Materials]` section: `Quills` and `Inkwells`, each a comma-separated list of `0xFormID~Plugin.esp` (the form ID as in the plugin; a load-order byte copied with it is ignored; `.esl` and `.esm` too; spaces around an entry are trimmed, a plugin name keeps its own).

```ini
[Materials]
Quills = 0x000D62~FancyQuills.esp, 0x000D63~FancyQuills.esp
Inkwells = 0x000801~ScribesInk.esl
```

- **Where:** `SKSE/Plugins/InkAndQuill.ini`, and any `.ini` in `SKSE/Plugins/InkAndQuill/`: a mod (or a patch) ships its own file there instead of editing Ink & Quill's. All are read at `kDataLoaded`; a change needs a restart.
- **Any item** the player can carry; an inkwell is renamed and used up like the vanilla one. An entry that isn't an item in a loaded plugin is skipped and logged.
- **Not in the form lists:** the items are kept by Ink & Quill, not added to `InkAndQuillQuills` or `InkAndQuillInkwells` (an added form would stay in the player's save once it's taken out of the INI). Editing the lists in the ESP (a patch plugin) works too.

## The MCM

One page. Left: the edit key, then writing (the quill-and-ink toggle, inkwell uses, blood, blood cost). Inkwell uses and blood are greyed out while quill and ink aren't required, and the blood cost while blood is off. Right: **the mods using Ink & Quill**.

The natives (`InkAndQuill_MCM`, global): `GetSetting`, `SetSetting`, `GetSettingDefault`, `GetSettingMin`, `GetSettingMax` by `"Section.Key"` (any case; an unknown name reads 0, logged), `GetKeyProblem`, and `GetClientCount`, `GetClientName`.

## The mod list

Every DLL that calls the C API (`IQ_GetAPI`, `AddOwner`, `RegisterBlank`, `SetClientName`) is listed, found by the address it called from (`_ReturnAddress`, `GetModuleHandleExW`), so a client doesn't have to register to appear. It shows under the name it gave `SetClientName`, else its DLL's file name without `.dll`. Only DLL clients are listed; there's no Papyrus API ([API_DESIGN.md](API_DESIGN.md#papyrus-api)).

Strings: the `$IQ_…` MCM keys in `Interface\Translations\InkAndQuill_ENGLISH.txt` (SkyUI reads the file by the plugin's name). All nine of Skyrim's languages ([EDITOR.md](EDITOR.md#strings)).
