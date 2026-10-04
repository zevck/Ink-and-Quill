# The editor

The player writes in the open book in the book menu. Code: `Editor`, `Input` (the keyboard: reading and blocking it, clients' keys), `Keys` (the editor's keys, held-key repeat, which keys may be bound), `Clipboard` (paste and copy), `Suggestions` (inline completion), `QuillCursor`, `WritingMode`, `WritingTools`, `Settings`, `Strings`, and `book.swf` (`swf/book`). Copied from SkyrimNet Physical Diaries' `BookEditor` (its docs/EDITING.md has the history) and made client-neutral: diary journals, SkyrimNet writes and blank journals stay there. Clients reach it through the C API ([API.md](API.md)), built on the internal `Editor` calls.

## Writing mode

Writing is on when **Ink & Quill's `book.swf` is the one the game loads**. `WritingMode::Detect` runs at `kPostLoad`, so `IsWritingOn` is right for clients by their `kDataLoaded`:

- It reads `Data\Interface\book.swf`, the file the game sees (under MO2, the winning loose file; a loose file beats every BSA). No file: off.
- The SWF ships **uncompressed** (`FWS`; the build runs `ffdec-cli -decompress` and fails if the output isn't uncompressed with the marker, see [DEVELOPMENT.md](DEVELOPMENT.md)), so `BookMenu.as`'s marker `BOOKMENU_WRITING_INTERFACE=<n>` is plain text in the file. A compressed file or one without the marker is another mod's: off.
- `<n>` only goes up: bump it when a call is added. The plugin needs at least `kMinInterface` (`WritingMode.cpp`, now 4: `SetEditMarked`); an older SWF is logged as an error and writing is off. The marker's name is the one Physical Diaries' SWF used, before Ink & Quill took the editor over; Ink & Quill is now the only mod shipping `book.swf`. An older copy left over from Physical Diaries (interface 3) is refused as too old.

Only with writing on does `Editor::Register` install the input sink, the menu sink and the two book menu hooks. The log says which (`[WritingMode] On` / `Off: <why>`).

## Quill, ink and blood

- **Quill:** any form in `InkAndQuillQuills` (`0x800`: vanilla Quill `0x04C3C8`, `FVDQuill` `0x0C04BB`), or one an INI names ([SETTINGS.md](SETTINGS.md#other-mods-quills-and-inkwells)). Writing doesn't start without one.
- **Ink:** a full inkwell is any form in `InkAndQuillInkwells` (`0x801`: vanilla Inkwell `0x04C3C6`) or one an INI names and lasts `Writing.InkwellUses` saves (10; 0: forever, an inkwell is needed but never used up or renamed, and inkwells already used keep their names until the setting comes back). **A used inkwell is the same item, renamed** with the uses left: "Inkwell (9/10)", the base name from the record (so in the game's language) plus `(n/m)`, m being `Writing.InkwellUses`. The vanilla record is never changed and there are no copy records.
  - The name is an `ExtraTextDisplayData` on the item's extra list (a custom name, as the enchanting menu gives), so the save keeps it, and it should travel with the item (dropped, stored, sold). `UsesLeft` reads back any `(n/m)`, capped at the current maximum ([SETTINGS.md](SETTINGS.md), "Changing `InkwellUses`").
  - `UseInk` takes the **emptiest** inkwell (lowest n), else a full one, plain ones before ones in an extra list (ownership, a favourite). The last use removes it.
  - Renaming a plain inkwell splits it off its stack: a new extra list for one item, added to the inventory entry. **CommonLib has no constructor for an extra list**, so `NewExtraList` lays one out as the engine does: zeroed, its presence bits on the game heap, and on AE 1.6.629+ (where the base list became virtual) the vtable copied from the player's own list.
- **Blood:** with a quill but no ink, writing asks to write in blood. **Ink and blood are charged per save**, for every client: one use of ink, or `Writing.BloodCost` percent of the player's maximum health (10), refused if it would leave them under 1 (`CanBleed`). `Writing.Blood` off: no blood prompt; `Writing.RequireQuillAndInk` off: no quill, ink or blood at all ([SETTINGS.md](SETTINGS.md)). Nothing is charged on discard or when the client refuses the save. What's typed in blood is dark red; the SWF marks it inline between U+E000 and U+E001 (`Editor::kBloodOpen` / `kBloodClose`), in the bodies it hands back and in headings it's given.

## Keys and settings

The edit key (default F3) and the writing settings (quill and ink required, inkwell uses, blood, its cost), are in `SKSE\Plugins\InkAndQuill.ini` and the MCM: [SETTINGS.md](SETTINGS.md).

## Strings

What the player sees is in `Interface\Translations\InkAndQuill_<LANGUAGE>.txt` (UTF-16 LE, `$IQ_<key><tab><text>`, `\n` a line break), read by `Strings::Load` at `kDataLoaded`: English first, then the game's language (`sLanguage:General`) over it. A missing key shows as the key. All nine of Skyrim SE's languages ship (English, French, German, Italian, Spanish, Polish, Russian, Japanese, Chinese: Physical Diaries' set and wording, German formal), one file holding the editor's strings and the MCM's; the translations were written without a native speaker's review (2026-10-04) and stay until one corrects them. A new key goes into all nine.

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
- **Locked `[pagebreak]`s are blanked:** replaced by as many spaces in the tag's own format, their places kept per segment (`breaks`). Reading cuts each page out of the text, so the tag is never drawn; the editor masks one tall field, and the window starts above a page's first line (the text gutter, and glyphs that rise above their line), so the tag's letters showed at the top of the next page (found in game). Spaces keep the line's height, so the pages don't move.
- **Typing** takes the format of the character before the caret (after it at a run's start; the hint in an empty run). With the hint (`font`, `size`), `FormatBreaks` also gives each edited run that font and size and its `\r\r` the page's outer size, as Physical Diaries' renderer does. Blood ranges are painted when the text loads (the client marks blood instead of colouring it; until 2026-10-02 old blood showed black until its run was edited) and after every edit, with or without the hint.
- **Saving:** the runs as loaded (`EditGetBodies` right after `EnterEditMode`) are what a save compares against.
- **Reloading** (`Editor::Reload`, the SWF's `EditReload`): the client's text rendered again rebuilds the field (`EditBuildMarked`) in place, and its reading text replaces `sBookText` (what the edit key returns to with nothing to save: without it a tear-out stayed in the book until it was reopened, found in game 2026-10-03), the caret goes to the run and offset asked, and each new run takes the saved text of the run it came from (`from`), so unsaved changes still count; a `from` of `-2 - k` takes run k's text as the session began (`g_started`: a save ends the session, so it never moves), so a run a Reload dropped and brought back unchanged is no change ([API.md](API.md#while-writing)). `caretRun` on a session puts the caret at a run's end on entering (`EditFocusEntry`).
- **Session end:** `EndSession` drops the session and calls the client's `onEnd`, once, on every way out (closed, discarded, no quill, blood declined, the menu closed before a `BeginOnOpen` began, a load).
- **Edits stay in one run**: typing inserts at the caret, Backspace and Delete stop at a run's ends, and a selection is never replaced (the caret is its start), so locked text can't be changed through a selection.

## Headings

A line that starts with `# ` is a heading, `## ` a smaller one (no small text: `###` is a heading in markdown, as SkyrimNet's dashboard shows diaries, so it stays plain text here): no markup, no markers, the text keeps the `# ` as typed (a client stores it as it is: plain markdown). The SWF applies it to **every book**, for every client, with no opt-in or opt-out (decided 2026-10-03: nothing needs a literal `# ` line; add a session opt-out if a client ever does):

- **Reading** (`SetBookText`, so also `ReturnToReading` and `ReplaceBookText`): once the HTML is in the field and before pagination, `ReadHeadings` takes each line's mark out and enlarges the line by `HEADING_SCALE` (1.5 for `#`, 1.25 for `##`) of the size it had, from the last line up. It works on the laid-out text, so it doesn't matter what tags the line sits in; a vanilla book with a line starting `# ` gets a heading too.
- **Writing** (`StyleHeadings`, from `EditBuildMarked` and after every edit through `FormatBreaks`): the mark stays in the field (it's text the player can delete) at size 1, and the rest of the line is the run's base size (the client's hint, else the run's first character as loaded) times the same scale. Without a hint, the run's other lines are set back to its base size on each pass (with one, `FormatBreaks` resets them). A run starting mid-line, after locked text, has no line start there. Locked text's headings are styled once as loaded (`StyleLockedHeadings`, each from its own size), so the editor shows what reading shows. Typing `# ` at a line's start makes it a heading at once; deleting the mark (two Backspaces: the caret doesn't skip it yet) makes it plain again.

Checked in game (AE, 2026-10-03): headings appear while typing and when reading, and deleting the mark returns the line to its normal size (with a client's format hint).

## Blanks

An item the client writes into for the first time: Physical Diaries' blank journals, Physical Letters' parchment. `Editor::RegisterBlank(form, onOpen)` (the C API's `RegisterBlank`).

- **Opening:** the book menu opening on a registered blank queues `NoteBlank`. Only from the player's **own inventory**: not in the world (`BookMenu::GetTargetReference`), a container, a shop or the gift menu, and only if they carry one; anywhere else a blank is just an empty book. Once the SWF has the text (`AdvanceMovie`, `EditReady`), `OpenBlank` calls the client's `onOpen`, which begins a session with its starting text as an owner would. The edit key on a blank in the inventory does the same (the player put the quill down and changed their mind).
- **When it becomes the client's book is the client's choice.** In `onOpen` it can replace the blank **now** (`ReplaceOpenBlank`, the C API's `ReplaceBlank`: one blank goes, the menu is pointed at its book and shows the book's text via the SWF's `ReplaceBookText`), and then begin a session in that book or not; or it begins a session in the blank and replaces it **on the first save** (below). `onOpen` is called with or without a quill.
- **No quill, or no ink with `Writing.Blood` off,** in a session begun in a blank: a HUD notice (`NeedsQuill`, `NeedsInk`), not the message box: blanks get read often. No ink with blood on offers blood. With `Writing.RequireQuillAndInk` off there are no such checks.
- **The first accepted save** answered with a book (`Saved::book`, the C API's `ReplySaveAsBook`): the costs are charged, one blank leaves the inventory, and the open menu is pointed at the client's book (`SetBookMenuBook`: the engine's book-menu globals for the base form and the item's extra list; from Physical Diaries, the VR address inferred there and never run). A save ends the session (the edit key reads again, the prompt's Save closes the book), so writing in that book again is an ordinary session of its owner. A save answered without a book leaves the blank (logged).
- **Discard, or putting the quill down,** in a session begun in a blank: nothing is made and the blank stays.

## Input

**IME input** (Japanese, Chinese, Korean) isn't supported: on its own no IME works in the game, and typing is read from the keys. Players paste text written elsewhere (Ctrl+V). The findings and the plan are under [Not done yet](#not-done-yet).

While the player writes, **every keyboard event is Ink & Quill's**: it reads them for the editor, then takes them out of the game's input, so nothing after it sees them: the book menu (no page turns on the arrows, A and D, no controls), Papyrus hotkeys (`RegisterForKey`: SKSE delivers them from a later input sink), other mods' input sinks, and mods hooked on the engine's input dispatch (Wheeler and the like). Mouse and gamepad events pass (a click turns pages; the gamepad's B reaches the close hook). The pattern is SkyrimNet Prisma Dashboard's (`Input`, `FilterInput`; it queues the editor's work with `Editor::OnEditKey` and `OnKey`):

- **Layer 1, a sink** prepended to `BSInputDeviceManager`, first among the sinks.
- **Layer 2, a `write_call` hook on the input dispatch** (`RELOCATION_ID(67315, 68617)` + `0x7B`; VR `0x81`, untested), the call Wheeler hooks too. Wheeler hooks it at load; Ink & Quill hooks it at the first `kPostLoadGame` or `kNewGame` (`Input::InstallHook`), so ours is the outer hook and runs first. A mod hooking it later still would come before ours.
- **Not blocked:** mods that poll the keyboard themselves (`GetAsyncKeyState`, DirectInput). The dashboard also patches other DLLs' `GetAsyncKeyState` imports; Ink & Quill doesn't (decided 2026-10-03).
- **Clients' own keys** that act while writing (Physical Diaries' new entry and tear-out) are left in the input when the client registers them (`SetClientKeys`, the C API's `RegisterKeys`: each client's set replaces its last); the editor doesn't read them as text. Keys that type or edit and the edit key are refused (`Keys::Check`, shared with the MCM), and the edit key is checked first, so it always saves.
- **Not writing, a book open:** only the edit key is taken out (it begins writing); the book-menu state comes from the menu sink (`g_bookOpen`), not from asking the UI on the input thread.
- **While a prompt is open** (Ink & Quill's own, or a client's through `Prompt`), nothing is taken: the message box gets its keys. A client's prompt answers through `ClientPromptCallback`, after `EndPrompt`, only to the session that asked (`g_sessionSerial`, bumped as each session ends).

Both layers run on the thread input arrives on: the main thread while a menu pauses the game, the "Poll controls" job during play (Skyrim Souls RE unpauses the book menu). So they only read and unlink events; the work is a **UI task**: typing, caret moves, the edit key's save. SKSE runs UI tasks in `UIManager::ProcessCommands`, right after the UI processes its message queue (SKSE's `Hooks_UI.cpp`), where the book menu gets its messages and the close hook runs: so the SWF calls, the client's callbacks, ink and blood and the session state stay on the UI's thread. Each task checks the session is still writing (the edit key's begin: that the book is still open and nothing began), and catches exceptions (`QueueUI`). Held-key repeat is worked out at once (`Keys::ShouldRepeat`: 400 ms, then every 50 ms); the characters for a key in the task (`GetKeyboardState`, `ToUnicode`): Windows keeps keyboard state per thread, the window's thread being the one that sees Shift and dead keys.

Keys become text with the active layout (`ToUnicode`, dead keys, held-key repeat). Arrows, Home and End move the caret; Backspace, Delete and Enter edit (an erase only where `EditCanErase` says there's something); Escape asks the menu to close. The edit key saves and reads again. **Ctrl+V** pastes the Windows clipboard's text at the caret, as if typed (`Clipboard::ReadForTyping`: line breaks kept, tabs as spaces, other control characters and every private-use character dropped, so a paste can't carry blood or lock markers; at most 20000 characters). **Ctrl+C** copies the whole run the caret is in (`Clipboard::Write`, without markers; `Copied` notice): a client's entry, or the whole text where it locks nothing. There's no selection, so no copying part of a run, and no cut. Paste and copy ran on AE (2026-10-03), headings included. Ctrl with Alt (AltGr) types as usual.

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

## Suggestions

Inline completion for clients (`Suggest`, [API.md](API.md#suggestions)). Code: `Suggestions` and book.swf's `EditSuggest*`.

- **Drawn** in its own text field on the edit clip, under the same mask (`ShowSuggestion`): at the caret's left edge on its line, in the font and size of the character before the caret, in `SUGGEST_COLOR` (`#2A2520`: lighter faded inks were hard to read on the vanilla page; the Convenient Reading variant uses the same colour). One line, no wrapping: a suggestion too long for the rest of the line is cut at the field's right edge with "...".
- **State is the SWF's:** the list, the one shown, and the caret position it was given for. `EditSuggesting` is false once the caret has moved, so a stale suggestion takes no keys. `EditReload`, `EditGoToPage` (page turns) and `ExitEditMode` clear it; the plugin clears it before a prompt and on every key that isn't the suggestion's (`Suggestions::HandleKey`, first in `HandleKey`).
- **Accepting** takes the text from the SWF and types it through `AppendEditChar`, as a paste: the same format, blood and `onChange` as typing.

## Quill cursor

**Deferred past 1.0** (2026-10-03): shown only with `Debug.QuillAdjust=1`. Open: the caret-to-page mapping strays (calibrated tracking narrowed it; not saved or confirmed), the right page of a spread, books' own pose. While writing, the game's own quill (the model of Skyrim.esm's Quill, `04C3C8`, so a replacer's when one is installed) sits on the open book: it's a node in the book menu's 3D scene, a child of `BookMenu`'s `bookModel`, not an image in the SWF. Nothing of the model is shipped. Code: `QuillCursor`.

- The model database's copy is shared and comes with its root hidden: the book gets a clone with `kHidden` cleared and its collision dropped (with it, every update put the quill back where its physics body was). Its world transform is set from the book's and pushed down (`Update` alone left it where it loaded).
- The book's space (AE, vanilla journal): the camera looks down -y at the book, about 195 units away; the text quad `PageText` faces the camera.
- **Following the caret:** book.swf gives the caret's point on the stage (`EditCaretPoint`: the caret's left edge at its line's bottom). `PageText`'s vertices (read from its CPU vertex data once per book) give the quad the page texture lands on, by UV; the stage point within the visible stage (`GetVisibleFrameRect`: wider than the 300 by 475 stage) is a UV, and the quad turns it into a point on the book (the page shows the texture turned half round: both directions run against the UVs, seen in game). The quill's pose is in the quad's space (an offset from that point, a rotation and a scale), so it sits the same on any book model: one pose for books, one for notes. Worked out every frame. Not checked yet: that the stage maps to the quad's UVs one to one, and the right page of a spread.

**Adjust mode** (development, `Debug.QuillAdjust=1`): while writing, the numpad moves the quill along the camera's axes (4/6 across, 2/8 up and down, 7/9 toward and away), `/` switches to turning about the same axes, `+`/`-` scale, `*` cycles the step size (0.02 to 2 units, 0.5° to 15°; it starts at 0.1 and 1°), `0` resets it onto the caret (a third of the page's size, lifted toward the camera), `1` calibrates tracking (below), and `5` logs a sample: its pose (an offset from the caret's point, in the page's space) and the caret (`side,page,x,y,gx,gy`). The configured poses are kept in `SKSE/Plugins/InkAndQuill_QuillPose.ini` (`[Book]` and `[Note]`: `Pose = x y z scale` then the rotation's rows; one with no pose uses the other's): read when the quill appears, saved with numpad Enter, restored with `.`; adjusting doesn't save, and it can be edited by hand. The HUD text is development-only, not translated.

**Calibrated tracking:** the UV mapping alone lets the quill stray more the further the caret goes from where the pose was set (seen in game: the line's start mapped past the quad's UV edge, and a nib above the page moves faster on screen than the text). Line the nib up on the caret, press numpad `1`, move the caret well right and down, line it up again, `1`: the quill then moves by `kx`, `ky` page units per stage unit from the first point (`Track = gx gy kx ky` beside the pose, saved with it). Move it only across the page between the two points, not toward the camera.

## Not done yet

- **The quill cursor** is deferred past 1.0 ([Quill cursor](#quill-cursor)).
- **The API**: version 1 is built ([API.md](API.md)); Physical Diaries uses its sessions, blanks, keys and prompts (AE). Physical Letters' letter editor is a client (its recipient preview uses `onChange`). No Papyrus API is planned ([API_DESIGN.md](API_DESIGN.md#papyrus-api)).
- **Renamed inkwells** ran on AE: the hand-made extra list works and the name survives save and load (2026-10-03). Not tested: dropping and picking up, containers, selling and buying back, SkyUI showing the name, SE.
- **IME input** (decided 2026-10-04: wait until it's asked for). Built once and removed after review, untested: the key filtering could block normal typing. Findings and design in Zev-Tools `notes/skyrim-text-input-re.md`:
  - No IME works in vanilla: the window has no usable IME context. SkyrimInputMethod (makes one, draws its own panel) and PrismaUI 1.4.1 (its own context while a view has focus, SkyrimNet's chat draws the overlay) each make one work. SkyrimInputMethod alone loses the text in the book menu: it sends committed text as Scaleform character events to the top menu, which reach `bookmenu.swf`, not `book.swf`.
  - Committed text must be read from the IME in Unicode (`ImmGetCompositionStringW`, `GCS_RESULTSTR`) in a window subclass, the message passed on without it: the game's message loop is ANSI, so its character messages turn Japanese into "?" on a non-Japanese system.
  - Key handling, from the review: keys belong to the IME only in its native conversion mode, never Ctrl combinations; re-read the IME state when its context changes (another mod's) and when writing starts; skip Enter, Backspace, Escape and arrows until released after a composition ends (SkyrimInputMethod waits 150 ms); find the window with `RE::Main::wnd`.
  - Without another mod: our own IME context while writing, the composition and candidates drawn at the caret (as a suggestion is), Windows' IME windows hidden; none of it when SkyrimInputMethod is loaded.
  - Testing needs a working Microsoft IME (the dev machine's stayed on half-width alphanumeric).
- **Translations** reviewed by native speakers: all nine languages ship, unreviewed.
- **Input blocking and unpaused writing** ran on AE (2026-10-03): every mod's hotkeys blocked but those that poll the keyboard. Not tested: VR's dispatch offset, a mod hooking the same dispatch call after Ink & Quill, and clients' registered keys (`RegisterKeys`).
