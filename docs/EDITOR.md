# The editor

The player writes in the open book in the book menu. Code: `Editor`, `WritingMode`, `WritingTools`, `Settings`, `Strings`, and `book.swf` (`swf/book`). Copied from SkyrimNet Physical Diaries' `BookEditor` (its docs/EDITING.md has the history) and made client-neutral: diary journals, SkyrimNet writes and blank journals stay there. The public API ([API_DESIGN.md](API_DESIGN.md)) isn't built yet; clients reach the editor through the internal `Editor::AddOwner` / `Editor::Begin`.

## Writing mode

Writing is on when **Ink & Quill's `book.swf` is the one the game loads**. `WritingMode::Detect` runs at `kPostLoad`, so `IsWritingOn` is right for clients by their `kDataLoaded`:

- It reads `Data\Interface\book.swf`, the file the game sees (under MO2, the winning loose file; a loose file beats every BSA). No file: off.
- The SWF ships **uncompressed** (`FWS`; the build runs `ffdec-cli -decompress` and fails if the output isn't uncompressed with the marker, see [DEVELOPMENT.md](DEVELOPMENT.md)), so `BookMenu.as`'s marker `BOOKMENU_WRITING_INTERFACE=<n>` is plain text in the file. A compressed file or one without the marker is another mod's: off.
- `<n>` only goes up: bump it when a call is added. The plugin needs at least `kMinInterface` (`WritingMode.cpp`, now 4: `SetEditMarked`); an older SWF is logged as an error and writing is off. The marker's name is the one Physical Diaries' SWF uses: its SWF (3) is too old for Ink & Quill, while Physical Diaries accepts Ink & Quill's (it keeps every older call). So while both ship `book.swf`, Ink & Quill's must win the file conflict.

Only with writing on does `Editor::Register` install the input sink, the menu sink and the two book menu hooks. The log says which (`[WritingMode] On` / `Off: <why>`).

## Quill, ink and blood

- **Quill:** any form in `InkAndQuillQuills` (`0x800`: vanilla Quill `0x04C3C8`, `FVDQuill` `0x0C04BB`). Writing doesn't start without one.
- **Ink:** a full inkwell is any form in `InkAndQuillInkwells` (`0x801`: vanilla Inkwell `0x04C3C6`) and lasts `Writing.InkwellUses` saves (10). **A used inkwell is the same item, renamed** with the uses left: "Inkwell (9/10)", the base name from the record (so in the game's language) plus `(n/m)`, m being `Writing.InkwellUses`. The vanilla record is never changed and there are no copy records.
  - The name is an `ExtraTextDisplayData` on the item's extra list (a custom name, as the enchanting menu gives), so the save keeps it, and it should travel with the item (dropped, stored, sold). `UsesLeft` reads back any `(n/m)`, capped at the current maximum ([SETTINGS.md](SETTINGS.md), "Changing `InkwellUses`").
  - `UseInk` takes the **emptiest** inkwell (lowest n), else a full one, plain ones before ones in an extra list (ownership, a favourite). The last use removes it.
  - Renaming a plain inkwell splits it off its stack: a new extra list for one item, added to the inventory entry. **CommonLib has no constructor for an extra list**, so `NewExtraList` lays one out as the engine does: zeroed, its presence bits on the game heap, and on AE 1.6.629+ (where the base list became virtual) the vtable copied from the player's own list.
- **Blood:** with a quill but no ink, writing asks to write in blood. **Ink and blood are charged per save**, for every client: one use of ink, or `Writing.BloodCost` percent of the player's maximum health (10), refused if it would leave them under 1 (`CanBleed`). `Writing.Blood` off: no blood prompt; `Writing.RequireQuillAndInk` off: no quill, ink or blood at all ([SETTINGS.md](SETTINGS.md)). Nothing is charged on discard or when the client refuses the save. What's typed in blood is dark red; the SWF marks it inline between U+E000 and U+E001 (`Editor::kBloodOpen` / `kBloodClose`), in the bodies it hands back and in headings it's given.

## Keys and settings

The edit key (default F3) and the writing settings (quill and ink required, inkwell uses, blood, its cost), are in `SKSE\Plugins\InkAndQuill.ini` and the MCM: [SETTINGS.md](SETTINGS.md).

## Strings

What the player sees is in `Interface\Translations\InkAndQuill_<LANGUAGE>.txt` (UTF-16 LE, `$IQ_<key><tab><text>`, `\n` a line break), read by `Strings::Load` at `kDataLoaded`: English first, then the game's language (`sLanguage:General`) over it. A missing key shows as the key. Only English exists so far.

## Starting

1. **The edit key** while a book is open: each owner (`Editor::AddOwner`) in turn is asked about that book (`BookMenu::GetTargetForm`); the one that owns it calls `Begin` and returns true. None: the key does nothing. A client's own key calls `Editor::Begin`; `Editor::BeginOnOpen` begins once a book it just opened has its text (the `AdvanceMovie` hook waits for the SWF's `EditReady`).
2. An unpaused book menu (Skyrim Souls RE) isn't refused, only logged ([Input](#input)). `Start` checks, in order: unless `Writing.RequireQuillAndInk` is off, **a quill** (`NeedsQuill`), **ink**, or the blood prompt (Write in blood / Put the quill down; with `Writing.Blood` off, `NeedsInk`). `WouldBeInBlood` gives clients that answer before they begin (quill and ink required, blood on, a quill but no ink), for a new entry's heading.
3. The session's marked text goes to the SWF (`SetEditMarked`, [Marked text](#marked-text)), then `EnterEditMode`, text input on, and `EditSetBlood` in blood. The runs as loaded are what saves compare against; `caretRun` puts the caret at a run's end (`EditFocusEntry`).

## The SWF

The editor in `swf/book` is Physical Diaries': one tall input field masked to one page, segments `{locked, body, editable}`, the reading view's pagination, the four engine page slots. It builds its text from the client's marked text only (below). Physical Diaries' original, where the SWF laid out its journal itself (`SetEditContent`, `EditAppendEntry`, `EditRemoveEntry`, `EditSetDates`) and the C++ added entries (`AppendEntry`, the client's `newEntry`), was removed on 2026-10-03, once Physical Diaries sent marked text: a client's new entries and tear-outs are its own, through `CurrentRuns` and `Reload`.

## Marked text

The design is in [API_DESIGN.md](API_DESIGN.md#marked-text-agreed-2026-10-02): the client sends its reading text with locked parts between U+E002 and U+E003, and everything else is editable. Built so far (`Document::marked`, `runFont`, `runSize`):

- **`SetEditMarked(text, font, size)`**, then `EnterEditMode`. `EditBuildMarked` sets the text as `SetBookText` does (wrapped in the page's font size, the reference field's format as default) through `SetText(…, true)`, notes where every lock and blood marker is, takes them out, and makes one segment per lock with the run after it as its body. Runs follow the rule in the design (empty ones count; text before the first or after the last lock only if there is any).
- **Pages:** `EditLayout` breaks at `[pagebreak]` lines as `CalculatePagination` does (the page above ends at the tag's line, the next starts below it; the tag line is on neither, `aEditPageBottoms`), instead of at each segment.
- **Locked `[pagebreak]`s are blanked:** replaced by as many spaces in the tag's own format, their places kept per segment (`breaks`). Reading cuts each page out of the text, so the tag is never drawn; the editor masks one tall field, and the window starts above a page's first line (the text gutter, and glyphs that rise above their line), so the tag's letters showed at the top of the next page (found in game). Spaces keep the line's height, so the pages don't move. `EditCheckLayout` compares the texts with tags and spaces made alike.
- **Typing** takes the format of the character before the caret (after it at a run's start; the hint in an empty run). With the hint (`font`, `size`), `FormatBreaks` also gives each edited run that font and size and its `\r\r` the page's outer size, as Physical Diaries' renderer does. Blood ranges are painted when the text loads (the client marks blood instead of colouring it; until 2026-10-02 old blood showed black until its run was edited) and after every edit, with or without the hint.
- **Saving:** the runs as loaded (`EditGetBodies` right after `EnterEditMode`) are what a save compares against.
- **Reloading** (`Editor::Reload`, the SWF's `EditReload`): the client's text rendered again rebuilds the field (`EditBuildMarked`) in place, and its reading text replaces `sBookText` (what the edit key returns to with nothing to save: without it a tear-out stayed in the book until it was reopened, found in game 2026-10-03), the caret goes to the run and offset asked, and each new run takes the saved text of the run it came from (`from`), so unsaved changes still count. `caretRun` on a session puts the caret at a run's end on entering (`EditFocusEntry`).
- **Session end:** `EndSession` drops the session and calls the client's `onEnd`, once, on every way out (closed, discarded, no quill, blood declined, the menu closed before a `BeginOnOpen` began, a load).
- **Edits stay in one run**: typing inserts at the caret, Backspace and Delete stop at a run's ends, and a selection is never replaced (the caret is its start), so locked text can't be changed through a selection.

## The layout test

Development only, to check marked text in game before the API is built. With `[Debug] LayoutTest = 1` in `InkAndQuill.ini`, `LayoutTest` owns **every book**: the edit key opens its own reading text as marked text: `TESDescription::GetDescription` with no parent, as the book menu asks (Physical Diaries' hook gives any other caller its text without font tags). English text only: for Cyrillic, Physical Diaries' hook returns Win-1251, which the SWF would read as UTF-8.

- **A Physical Diaries journal** (text starting with `[pagebreak]`): everything locked but each entry's text, from after its heading's `\n\n` to its last paragraph's `</font>` (headings on only), with the font and size of its first paragraph as the hint.
- **Any other book:** only its `[pagebreak]` tags locked.
- **On entering**, the log says the run count and the result of `EditCheckLayout`: each editing page's first character against the reading view's (`ok (…)`, or the pages that differ). **On save** each run is logged and the save is refused with a notice: nothing is kept or charged.

Passed on AE (2026-10-02) on a Physical Diaries journal, after two fixes found by it (the heading lock, the page break showing above a page). What it checks: the check says ok; the page being read is the page edited; typing, Enter, Backspace and Delete keep the fonts, sizes and alignment, in a journal entry and a vanilla book; blood text is red; saving and reading again shows the same pages.

Physical Diaries' own edit key (F3) also acts on its journals, so the dev INI moves Ink & Quill's to F4 (`[Keys] Edit = 62`).

## Blanks

An item the client writes into for the first time: Physical Diaries' blank journals, Physical Letters' parchment. `Editor::RegisterBlank(form, onOpen)` (the C API's `RegisterBlank`).

- **Opening:** the book menu opening on a registered blank queues `NoteBlank`. Only from the player's **own inventory**: not in the world (`BookMenu::GetTargetReference`), a container, a shop or the gift menu, and only if they carry one; anywhere else a blank is just an empty book. Once the SWF has the text (`AdvanceMovie`, `EditReady`), `OpenBlank` calls the client's `onOpen`, which begins a session with its starting text as an owner would. The edit key on a blank in the inventory does the same (the player put the quill down and changed their mind).
- **When it becomes the client's book is the client's choice.** In `onOpen` it can replace the blank **now** (`ReplaceOpenBlank`, the C API's `ReplaceBlank`: one blank goes, the menu is pointed at its book and shows the book's text via the SWF's `ReplaceBookText`), and then begin a session in that book or not; or it begins a session in the blank and replaces it **on the first save** (below). `onOpen` is called with or without a quill.
- **No quill, or no ink with `Writing.Blood` off,** in a session begun in a blank: a HUD notice (`NeedsQuill`, `NeedsInk`), not the message box: blanks get read often. No ink with blood on offers blood. With `Writing.RequireQuillAndInk` off there are no such checks.
- **The first accepted save** answered with a book (`Saved::book`, the C API's `ReplySaveAsBook`): the costs are charged, one blank leaves the inventory, and the open menu is pointed at the client's book (`SetBookMenuBook`: the engine's book-menu globals for the base form and the item's extra list; from Physical Diaries, the VR address inferred there and never run). A save ends the session (the edit key reads again, the prompt's Save closes the book), so writing in that book again is an ordinary session of its owner. A save answered without a book leaves the blank (logged).
- **Discard, or putting the quill down,** in a session begun in a blank: nothing is made and the blank stays.

## Input

While the player writes, **every keyboard event is Ink & Quill's**: it reads them for the editor, then takes them out of the game's input, so nothing after it sees them: the book menu (no page turns on the arrows, A and D, no controls), Papyrus hotkeys (`RegisterForKey`: SKSE delivers them from a later input sink), other mods' input sinks, and mods hooked on the engine's input dispatch (Wheeler and the like). Mouse and gamepad events pass (a click turns pages; the gamepad's B reaches the close hook). The pattern is SkyrimNet Prisma Dashboard's (`FilterInput`):

- **Layer 1, a sink** prepended to `BSInputDeviceManager`, first among the sinks.
- **Layer 2, a `write_call` hook on the input dispatch** (`RELOCATION_ID(67315, 68617)` + `0x7B`; VR `0x81`, untested), the call Wheeler hooks too. Wheeler hooks it at load; Ink & Quill hooks it at the first `kPostLoadGame` or `kNewGame` (`InstallInputHook`), so ours is the outer hook and runs first. A mod hooking it later still would come before ours.
- **Not blocked:** mods that poll the keyboard themselves (`GetAsyncKeyState`, DirectInput). The dashboard also patches other DLLs' `GetAsyncKeyState` imports; Ink & Quill doesn't (decided 2026-10-03).
- **Clients' own keys** that act while writing (Physical Diaries' new entry and tear-out) are left in the input when the client registers them (`SetClientKeys`, the C API's `RegisterKeys`: each client's set replaces its last); the editor doesn't read them as text. Keys that type or edit and the edit key are refused (`Keys::Check`, shared with the MCM), and the edit key is checked first, so it always saves.
- **Not writing, a book open:** only the edit key is taken out (it begins writing); the book-menu state comes from the menu sink (`g_bookOpen`), not from asking the UI on the input thread.
- **While a prompt is open** (Ink & Quill's own, or a client's through `Prompt`), nothing is taken: the message box gets its keys. A client's prompt answers through `ClientPromptCallback`, after `EndPrompt`, only to the session that asked (`g_sessionSerial`, bumped as each session ends).

Both layers run on the thread input arrives on: the main thread while a menu pauses the game, the "Poll controls" job during play (Skyrim Souls RE unpauses the book menu). So they only read and unlink events; the work is a **UI task**: typing, caret moves, the edit key's save. SKSE runs UI tasks in `UIManager::ProcessCommands`, right after the UI processes its message queue (SKSE's `Hooks_UI.cpp`), where the book menu gets its messages and the close hook runs: so the SWF calls, the client's callbacks, ink and blood and the session state stay on the UI's thread. Each task checks the session is still writing (the edit key's begin: that the book is still open and nothing began), and catches exceptions (`QueueUI`). Held-key repeat is worked out at once; the characters for a key in the task (`GetKeyboardState`, `ToUnicode`): Windows keeps keyboard state per thread, the window's thread being the one that sees Shift and dead keys.

Keys become text with the active layout (`ToUnicode`, dead keys, held-key repeat). Arrows, Home and End move the caret; Backspace, Delete and Enter edit (an erase only where `EditCanErase` says there's something); Escape asks the menu to close. The edit key saves and reads again.

## Saving

The **edit key** while writing (save, then read again), or **Save** on the close prompt (save, then close).

1. The runs (`EditGetBodies`) are compared with what each was loaded or last saved with. No change: nothing to save.
2. Unless the session is free (`Writing.RequireQuillAndInk` off): in blood, `CanBleed` (`TooWeak`); in ink, `HasInk` (`NeedsInk`). Both were checked when writing began, but health or the inkwell can change since (an unpaused menu, a client's own doing).
3. The client's `onSave(runs)` answers **accepted** or **refused with a message** (shown; writing goes on; nothing charged).
4. Accepted: unless the session is free, one use of ink (`InkRanDry` if that was the last) or blood is taken. The edit key returns to reading with the client's `text` (`ReturnToReading`); with no text the book closes.
5. Nothing changed: no save and no client call; the edit key returns to reading with the text the book already had (the SWF keeps the last `SetBookText` text, `sBookText`). Until 2026-10-02 this closed the book (found in game with Physical Diaries).

The editor's text can't be read: `SaveFailed`, and writing goes on, so nothing is lost while the player can still see it.

## Closing

`BookMenu::ProcessMessage` is hooked (vtable index 4): a close with unsaved changes (`kHide`, or a gamepad's "Cancel") is held back and the prompt opens: **Save / Discard / Keep writing** (the cancel button). Discard calls the client's `onDiscard`. `kForceHide` (loads) passes: the changes are lost. Escape asks the menu to close, so it takes the same path. Details and why: Physical Diaries' EDITING.md, "Closing".

## Not done yet

- **The API**: version 1 is built ([API.md](API.md)); Physical Diaries uses its sessions (AE) and blanks (untested); Papyrus isn't built. Physical Letters doesn't use Ink & Quill yet.
- **Nothing here has run in game.** Above all the renamed inkwells: the hand-made extra list on SE and AE, the name surviving save and load, dropping, containers and merchants, and SkyUI showing it.
- **Translations** beyond English (the editor's strings and the MCM's).
- **Input blocking and unpaused writing** ran on AE (2026-10-03): every mod's hotkeys blocked but those that poll the keyboard. Not tested: VR's dispatch offset, a mod hooking the same dispatch call after Ink & Quill, and clients' registered keys (`RegisterKeys`).
- **Both mods ship `book.swf`** until Physical Diaries drops its copy; MO2's order picks one, and either works (same marker).
