# Settings and the MCM

Code: `Settings` (the INI), `Papyrus` (the MCM's natives), `Source/Scripts/InkAndQuill_MCM.psc` (the SkyUI menu, on `InkAndQuillMCMQuest`, `0x802`), `Clients` (the mod list).

## The INI

`SKSE\Plugins\InkAndQuill.ini`. Every setting is an integer row in `Settings::kAll` (section, key, default, range), read at `kDataLoaded` and clamped. A missing file or key is its default; nothing ships an INI. The MCM changes a setting through `SetSetting`, which writes that one key back at once (`WritePrivateProfileStringA`, so the rest of the file is kept) and takes effect immediately: every reader asks `Settings::Get` when it needs the value.

| Setting | Default | Range | What |
|---|---|---|---|
| `Keys.Edit` | 61 (F3) | 1–255 | With a book open: start writing, if a client owns the book or it's a blank. While writing: save and read again |
| `Writing.RequireQuillAndInk` | 1 | 0–1 | 0: writing needs no quill and no ink, offers no blood and costs nothing (`Editor`'s `g_free`, decided when writing starts) |
| `Writing.InkwellUses` | 10 | 1–100 | Saves a full inkwell lasts |
| `Writing.Blood` | 1 | 0–1 | With a quill but no ink, offer to write in blood. 0: the `NeedsInk` notice instead (a HUD notice on a blank) |
| `Writing.BloodCost` | 10 | 1–100 | Percent of maximum health each save in blood costs (never below 1 health left) |
| `Debug.LayoutTest` | 0 | 0–1 | Development only, not in the MCM ([EDITOR.md](EDITOR.md#the-layout-test)) |

The key is a DirectX scan code, not one that types (while writing it couldn't also type). The MCM refuses, with a message, a code that isn't a keyboard key (SkyUI also offers mouse and gamepad buttons, 256 and up) or a key that types or edits (`GetKeyProblem`, i.e. `Keys::Check`, also used for clients' keys: Escape, Backspace, Enter, Delete, the arrows, Home, End, the modifiers, and anything that gives a character with the player's layout). It doesn't check conflicts: it only acts while a book is open, where game controls don't apply. Clients' own keys (Physical Diaries' new entry and tear-out) are theirs, in their own settings.

**Changing `InkwellUses`:** an inkwell already used keeps its name ("Inkwell (7/10)") until its next use, which reads its own count back (`UsesLeft`, any `(n/m)`), takes one off, and names it against the new maximum, never above it: with 20, "(6/20)"; with 5, "(4/5)".

## The MCM

One page. Left: the edit key, then writing (the quill-and-ink toggle, inkwell uses, blood, blood cost). Inkwell uses and blood are greyed out while quill and ink aren't required, and the blood cost while blood is off. Right: **the mods using Ink & Quill**.

The natives (`InkAndQuill_MCM`, global): `GetSetting`, `SetSetting`, `GetSettingDefault`, `GetSettingMin`, `GetSettingMax` by `"Section.Key"` (any case; an unknown name reads 0, logged), `GetKeyProblem`, and `GetClientCount`, `GetClientName`.

## The mod list

Every DLL that calls the C API (`IQ_GetAPI`, `AddOwner`, `RegisterBlank`, `SetClientName`) is listed, found by the address it called from (`_ReturnAddress`, `GetModuleHandleExW`), so a client doesn't have to register to appear. It shows under the name it gave `SetClientName`, else its DLL's file name without `.dll`. Only DLL clients are listed; there's no Papyrus API ([API_DESIGN.md](API_DESIGN.md#papyrus-api)).

Strings: the `$IQ_…` MCM keys in `Interface\Translations\InkAndQuill_ENGLISH.txt` (SkyUI reads the file by the plugin's name). English only so far.
