# The editor

The player writes in the open book in the book menu. Code: `Editor`, `WritingMode`, `WritingTools`, `Settings`, `Strings`, and `book.swf` (`swf/book`). Copied from SkyrimNet Physical Diaries' `BookEditor` (its docs/EDITING.md has the history) and made client-neutral: diary journals, SkyrimNet writes and blank journals stay there. The public API ([API_DESIGN.md](API_DESIGN.md)) isn't built yet; clients reach the editor through the internal `Editor::AddOwner` / `Editor::Begin`.

## Writing mode

Writing is on when **Ink & Quill's `book.swf` is the one the game loads**. `WritingMode::Detect` runs at `kDataLoaded`:

- It reads `Data\Interface\book.swf`, the file the game sees (under MO2, the winning loose file; a loose file beats every BSA). No file: off.
- The SWF ships **uncompressed** (`FWS`; the build runs `ffdec-cli -decompress` and fails if the output isn't uncompressed with the marker, see [DEVELOPMENT.md](DEVELOPMENT.md)), so `BookMenu.as`'s marker `BOOKMENU_WRITING_INTERFACE=<n>` is plain text in the file. A compressed file or one without the marker is another mod's: off.
- `<n>` only goes up: bump it when a call is added. The plugin needs at least `kMinInterface` (`WritingMode.cpp`, now 4: `SetEditMarked`); an older SWF is logged as an error and writing is off. The marker's name is the one Physical Diaries' SWF uses: its SWF (3) is too old for Ink & Quill, while Physical Diaries accepts Ink & Quill's (it keeps every older call). So while both ship `book.swf`, Ink & Quill's must win the file conflict.

Only with writing on does `Editor::Register` install the input sink, the menu sink and the two book menu hooks. The log says which (`[WritingMode] On` / `Off: <why>`).

## Quill, ink and blood

- **Quill:** any form in `InkAndQuillQuills` (`0x800`: vanilla Quill `0x04C3C8`, `FVDQuill` `0x0C04BB`). Writing doesn't start without one.
- **Ink:** a full inkwell is any form in `InkAndQuillInkwells` (`0x801`: vanilla Inkwell `0x04C3C6`) and lasts 10 saves. **A used inkwell is the same item, renamed** with the uses left: "Inkwell (9/10)", the base name from the record (so in the game's language) plus `(n/10)`. The vanilla record is never changed and there are no copy records.
  - The name is an `ExtraTextDisplayData` on the item's extra list (a custom name, as the enchanting menu gives), so the save keeps it, and it should travel with the item (dropped, stored, sold). `UsesLeft` reads it back by the `(n/10)` suffix.
  - `UseInk` takes the **emptiest** inkwell (lowest n), else a full one, plain ones before ones in an extra list (ownership, a favourite). The last use removes it.
  - Renaming a plain inkwell splits it off its stack: a new extra list for one item, added to the inventory entry. **CommonLib has no constructor for an extra list**, so `NewExtraList` lays one out as the engine does: zeroed, its presence bits on the game heap, and on AE 1.6.629+ (where the base list became virtual) the vtable copied from the player's own list.
- **Blood:** with a quill but no ink, writing asks to write in blood. **Ink and blood are charged per save**, for every client: one use of ink, or 10% of the player's maximum health (`kBloodCost`), refused if it would leave them under 1 (`CanBleed`). Nothing is charged on discard or when the client refuses the save. What's typed in blood is dark red; the SWF marks it inline between U+E000 and U+E001 (`Editor::kBloodOpen` / `kBloodClose`), in the bodies it hands back and in headings it's given.

## Keys

`SKSE\Plugins\InkAndQuill.ini`, read once at `kDataLoaded`; a missing file or key is the default (no MCM yet):

```ini
[Keys]
Edit = 61    ; F3: start writing in the open book; while writing, save and read again
Remove = 68  ; F10: tear out the entry under the caret

[Debug]
LayoutTest = 0  ; 1: every book can be written in, nothing kept (The layout test)
```

DirectX scan codes; neither may be a key that types.

## Strings

What the player sees is in `Interface\Translations\InkAndQuill_<LANGUAGE>.txt` (UTF-16 LE, `$IQ_<key><tab><text>`, `\n` a line break), read by `Strings::Load` at `kDataLoaded`: English first, then the game's language (`sLanguage:General`) over it. A missing key shows as the key. Only English exists so far.

## Starting

1. **The edit key** while a book is open: each owner (`Editor::AddOwner`) in turn is asked about that book (`BookMenu::GetTargetForm`); the one that owns it calls `Begin` and returns true. None: the key does nothing. A client's own key calls `Editor::Begin`; `Editor::BeginOnOpen` begins once a book it just opened has its text (the `AdvanceMovie` hook waits for the SWF's `EditReady`).
2. `Start` checks, in order: **the book menu pauses the game** (`NeedsPause`; key handling relies on the paused menu's input arriving on the main thread), **a quill** (`NeedsQuill`), **ink**, or the blood prompt (Write in blood / Put the quill down).
3. The session's document goes to the SWF: `SetEditContent(font, title size, small size, date size, content size, title, dates, entries)`, entries as `heading \x1F body` joined by `\x1E`. Then `EnterEditMode`, text input on, and `EditSetBlood` in blood. With no entries, or `startNewEntry`, a new entry is added (the client's `newEntry`).

## The SWF

The editor in `swf/book` is Physical Diaries': one tall input field masked to one page, segments `{locked, body, editable}`, the reading view's pagination, the four engine page slots. It builds its text one of two ways:

- **Marked text** (`SetEditMarked`, the way forward, below).
- **Entries** (`SetEditContent(font, sizes, title, dates, entries)`, Physical Diaries' original): the SWF lays out Physical Diaries' journal itself (`EditBuildContent`, copying `FormatDiaryEntries`: a blank first page, a title page, each entry on a new page), with `EditAppendEntry`, `EditRemoveEntry` and `EditSetDates`. Kept while Physical Diaries still uses it; it goes once clients send marked text.

## Marked text

The design is in [API_DESIGN.md](API_DESIGN.md#marked-text-agreed-2026-10-02): the client sends its reading text with locked parts between U+E002 and U+E003, and everything else is editable. Built so far (`Document::marked`, `runFont`, `runSize`):

- **`SetEditMarked(text, font, size)`**, then `EnterEditMode`. `EditBuildMarked` sets the text as `SetBookText` does (wrapped in the page's font size, the reference field's format as default) through `SetText(…, true)`, notes where every lock and blood marker is, takes them out, and makes one segment per lock with the run after it as its body. Runs follow the rule in the design (empty ones count; text before the first or after the last lock only if there is any).
- **Pages:** `EditLayout` breaks at `[pagebreak]` lines as `CalculatePagination` does (the page above ends at the tag's line, the next starts below it; the tag line is on neither, `aEditPageBottoms`), instead of at each segment.
- **Locked `[pagebreak]`s are blanked:** replaced by as many spaces in the tag's own format, their places kept per segment (`breaks`). Reading cuts each page out of the text, so the tag is never drawn; the editor masks one tall field, and the window starts above a page's first line (the text gutter, and glyphs that rise above their line), so the tag's letters showed at the top of the next page (found in game). Spaces keep the line's height, so the pages don't move. `EditCheckLayout` compares the texts with tags and spaces made alike.
- **Typing** takes the format of the character before the caret (after it at a run's start; the hint in an empty run). With the hint (`font`, `size`), `FormatBreaks` also gives each edited run that font and size and its `\r\r` the page's outer size, as Physical Diaries' renderer does. Blood ranges are painted with or without it.
- **Saving:** the runs as loaded (`EditGetBodies` right after `EnterEditMode`) are what a save compares against.
- **Reloading** (`Editor::Reload`, the SWF's `EditReload`): the client's text rendered again rebuilds the field (`EditBuildMarked`) in place, the caret goes to the run and offset asked, and each new run takes the saved text of the run it came from (`from`), so unsaved changes still count. The remove key in marked text only asks (`removePrompt`) and hands the run to the client (`onRemove`), which reloads. `caretRun` on a session puts the caret at a run's end on entering (`EditFocusEntry`).
- **Session end:** `EndSession` drops the session and calls the client's `onEnd`, once, on every way out (closed, discarded, no quill, blood declined, the menu closed before a `BeginOnOpen` began, a load).
- **Edits stay in one run**: typing inserts at the caret, Backspace and Delete stop at a run's ends, and a selection is never replaced (the caret is its start), so locked text can't be changed through a selection.

## The layout test

Development only, to check marked text in game before the API is built. With `[Debug] LayoutTest = 1` in `InkAndQuill.ini`, `LayoutTest` owns **every book**: the edit key opens its own reading text as marked text: `TESDescription::GetDescription` with no parent, as the book menu asks (Physical Diaries' hook gives any other caller its text without font tags). English text only: for Cyrillic, Physical Diaries' hook returns Win-1251, which the SWF would read as UTF-8.

- **A Physical Diaries journal** (text starting with `[pagebreak]`): everything locked but each entry's text, from after its heading's `\n\n` to its last paragraph's `</font>` (headings on only), with the font and size of its first paragraph as the hint.
- **Any other book:** only its `[pagebreak]` tags locked.
- **On entering**, the log says the run count and the result of `EditCheckLayout`: each editing page's first character against the reading view's (`ok (…)`, or the pages that differ). **On save** each run is logged and the save is refused with a notice: nothing is kept or charged.

Passed on AE (2026-10-02) on a Physical Diaries journal, after two fixes found by it (the heading lock, the page break showing above a page). What it checks: the check says ok; the page being read is the page edited; typing, Enter, Backspace and Delete keep the fonts, sizes and alignment, in a journal entry and a vanilla book; blood text is red; saving and reading again shows the same pages.

Physical Diaries' own edit key (F3) also acts on its journals, so the dev INI moves Ink & Quill's to F4 (`[Keys] Edit = 62`).

## Input

The input sink is **prepended** to `BSInputDeviceManager`, ahead of the menu. While writing, every keyboard event's user event is blanked (no key acts as a control) and calls `EditSuppressTurn` (the menu turns pages by key code). Keys become text with the active layout (`ToUnicode`, dead keys, held-key repeat). Arrows, Home and End move the caret; Backspace, Delete and Enter edit (an erase only where `EditCanErase` says there's something). The edit key saves and reads again; the remove key asks to tear out the entry under the caret. While a prompt is open, keys are its own. Other sinks still see the key codes, so a client's own key (a new-entry key) can call `Editor::AppendEntry`.

## Saving

The **edit key** while writing (save, then read again), or **Save** on the close prompt (save, then close).

1. The bodies (`EditGetBodies`) are compared with what each entry was given or last saved (line breaks normalized to `\n`). No change: nothing to save.
2. In blood, `CanBleed`; in ink, `HasInk` (only health can change while the menu pauses).
3. The client's `onSave(bodies)` answers **accepted** or **refused with a message** (shown; writing goes on; nothing charged).
4. Accepted: one use of ink (`InkRanDry` if that was the last) or blood is taken. The edit key returns to reading with the client's `text` (`ReturnToReading`); with no text the book closes.

The editor's text can't be read: `SaveFailed`, and writing goes on, so nothing is lost while the player can still see it.

## Tearing out an entry

The remove key, if the client set `removePrompt`: the client's question, **Tear out** or **Keep it**. Tear out removes the entry from the SWF (`EditRemoveEntry`) at once, calls the client's `onRemove(i)`, and shows the title page's dates it returns.

## New entries

`Editor::AppendEntry` (a client's key, or writing in an empty document): the client's `newEntry()` gives the heading (nullopt: no room; the client says why). One at a time: if an entry added since the last save is still there, the caret goes back to it. In blood, the heading is marked red.

## Closing

`BookMenu::ProcessMessage` is hooked (vtable index 4): a close with unsaved changes (`kHide`, or a gamepad's "Cancel") is held back and the prompt opens: **Save / Discard / Keep writing** (the cancel button). Discard calls the client's `onDiscard`. `kForceHide` (loads) passes: the changes are lost. Escape asks the menu to close, so it takes the same path. Details and why: Physical Diaries' EDITING.md, "Closing".

## Not done yet

- **The API**: version 1 is built ([API.md](API.md)) and untested; blanks and Papyrus aren't. Physical Diaries and Physical Letters don't use Ink & Quill yet.
- **Nothing here has run in game.** Above all the renamed inkwells: the hand-made extra list on SE and AE, the name surviving save and load, dropping, containers and merchants, and SkyUI showing it.
- **Translations** beyond English, and an MCM for the keys.
- **Both mods ship `book.swf`** until Physical Diaries drops its copy; MO2's order picks one, and either works (same marker).
