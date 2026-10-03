# The C API

How a client mod uses Ink & Quill's editor. Header: `include/InkAndQuillAPI.h` (plain C; copy it into the client). Code: `src/API.cpp`, over `Editor` and `WritingTools`. The design and its reasons are in [API_DESIGN.md](API_DESIGN.md); the editor's behaviour in [EDITOR.md](EDITOR.md).

**Status:** version 1, unreleased: its layout still changes, and a client rebuilds with the current header. Physical Diaries uses it. There's no Papyrus API ([API_DESIGN.md](API_DESIGN.md#papyrus-api)).

## Getting it

At `kPostLoad` or later:

```cpp
const IQ_API* iq = nullptr;
if (auto* module = GetModuleHandleA("InkAndQuill.dll")) {
    if (auto get = reinterpret_cast<IQ_GetAPI_t>(GetProcAddress(module, "IQ_GetAPI"))) iq = get(IQ_API_VERSION);
}
```

`IQ_GetAPI(version)` returns `NULL` if the DLL is older than the client's header. After the first release a newer DLL serves older clients: structs only grow at the end, and `IQ_API::size` and `IQ_Session::size` say how much there is.

## Rules

- **Threads.** Every callback comes where SKSE runs UI tasks (`AddUITask`): with the book menu, paused or not, Ink & Quill queues its input work there ([EDITOR.md](EDITOR.md#input)). Session calls (`BeginSession`, `CurrentRuns`, `Reload`, `CaretRun`, `Prompt`, the replies) are made there too: from a callback, or a client's own key handler queuing a UI task first. Registration calls (`AddOwner`, `RegisterBlank`, `RegisterKeys`, `SetClientName`) are fine from SKSE's messaging (`kDataLoaded`) as well.
- **Strings are UTF-8.** Ink & Quill copies every string it's given before the call returns; strings it passes to a callback are valid until the callback returns.
- **The answer to a save** goes through `ReplySave` or `ReplySaveAsBook`, which copy, so a client can answer with a temporary string.
- **`onEnd` is always called, once, last**, for every session handed to `BeginSession` or `BeginSessionOnOpen`, whether it wrote, was discarded, never started (no quill, writing off, blood declined) or was cut off by a load. That's where a client frees what `user` points to.

## Marked text

What a session edits: the book's text as the client renders it for reading, with what the player can't change between U+E002 and U+E003 (UTF-8 `EE 80 82` and `EE 80 83`). The runs between locks are what the player writes, in order; empty runs count, and text before the first lock or after the last is a run only if there is any. Blood text in a run is between U+E000 and U+E001. Full rules, including what clients must do (strip the markers from stored text, render reading and marked text separately, keep their session's order): [API_DESIGN.md](API_DESIGN.md#marked-text-agreed-2026-10-02).

## Starting

- **The edit key.** `AddOwner(owner, user)` at `kDataLoaded` or later. With a book open and nobody writing, the edit key asks each owner in turn with the book's FormID; the one that owns it calls `BeginSession` and returns true. No owner: the key does nothing.
- **A client's own key.** `BeginSession(&session)` with the book menu open, or `BeginSessionOnOpen(bookFormId, &session)` after opening it (`BookMenu::OpenMenuFromBaseForm`): it begins once the menu has the book's text.
- **`IQ_Session`:** `markedText` and `onSave` are required. `runFont` and `runSize` are the format hint for typed text (Physical Diaries: its content font and size, so paragraph breaks get the page's outer size as its renderer gives them). `caretRun` puts the caret at the end of that run (a new entry); -1 starts on the page being read.
- **False** from either: not started (writing off, already writing, a prompt open, an invalid session), and `onEnd` has run. True: the quill and ink checks and the blood prompt follow, and may still end it.

## While writing

- **`IsWriting`, `InBlood`:** blood is chosen when writing starts, so a client rendering a new heading knows whether to make it red. Before a session begins, `WouldBeInBlood` says whether it would be (quill and ink required, blood on, a quill but no ink), unless the player declines the prompt.
- **Changing the structure** (a client's own action, e.g. Physical Diaries' new entry or tear-out, on its own keys): `CurrentRuns(visit, user)` gives the runs as the player has them, unsaved text included; the client renders its text again from those and its change, then `Reload(markedText, readingText, from, count, caretRun, caretOffset)`. `readingText` is the book's text as it now reads (as for a save's reply): a structure change alone (a tear-out) changes no run's text, so the edit key returns to reading without a save, and shows this. `from[i]` is the run that new run `i` was before (-1 for a new one), `count` the number of new runs: each run keeps the text it was last saved with, so unsaved changes still prompt on close. The caret goes to `caretRun` at `caretOffset` (-1: its end).

## Blanks

`RegisterBlank(blankFormId, onOpen, user)` at `kDataLoaded` or later: an item the client writes into for the first time (a blank journal, parchment).

- Reading it from the player's **own inventory** (not in the world, a container, a shop or the gift menu), or the edit key on it there, calls `onOpen(user, blankFormId)` once the menu has its text, with or without a quill.
- **The client chooses when the blank becomes its book:**
  - **Now:** `ReplaceBlank(bookFormId, readingText)` in `onOpen` removes one blank and shows the book from its first page; a session begun after it (or none) is that book's.
  - **On the first save:** `BeginSession` in the blank, then answer the first accepted save with `ReplySaveAsBook(reply, bookFormId, readingText)`: Ink & Quill charges the costs, removes one blank and shows `bookFormId` in the open menu. Without a quill such a session ends with a HUD notice, not a message box. The save ends the session; writing in that book again is an ordinary session (its owner's). `ReplySave` (no book) leaves the blank.
- Discard or putting the quill down: nothing is made, the blank stays.

## Clients' keys

While the player writes, Ink & Quill takes every keyboard event from the game, other mods' hotkeys included ([EDITOR.md](EDITOR.md#input)). A client whose own keys act while writing (Physical Diaries' new entry and tear-out) registers them with `RegisterKeys(codes, count)` (DirectX scan codes) at `kDataLoaded` or later, and again with its whole set whenever its settings change them: each call **replaces** that client's keys (clients are told apart by their DLL, as in the [mod list](SETTINGS.md#the-mod-list)), so a key it stops using is typed again. Those keys reach the game's input as usual and aren't typed. **Refused** (logged; the return is how many were kept): keys that type or edit (the same check as the MCM's edit key: letters, digits, Space, Enter, Backspace, the arrows, the modifiers…) and the edit key, which always wins while writing.

## Asking the player

A client action while writing that needs the caret or a question (Physical Diaries' tear-out on its own key):

- **`CaretRun()`:** the run the caret is in, or -1 (not writing, or the caret in no run).
- **`Prompt(text, buttons, count, cancelButton, done, user)`:** a message box over the book, as Ink & Quill's own prompts: while it's open the keys are the box's (Ink & Quill takes none, text input is off), Escape picks `cancelButton`, the strings are copied. `done(user, button)` comes once, on the UI's thread, after the box has closed and the keys are the editor's again, so it can call `CurrentRuns` and `Reload`. **If the session ends first** (a load, the book closed), `done` is never called: `onEnd` is the signal, and may have freed `user`. False: not writing, a prompt already open, or no buttons.

## Saving

`onSave(user, runs, count, reply)` on the edit key or the close prompt's Save, when any run changed. Answer with `ReplySave(reply, accepted, message, readingText)`:

- **Accepted:** one use of ink or the blood cost is taken; the edit key returns to reading with `readingText` (the book's text as the client now renders it; `NULL` or empty closes the book), the prompt's Save closes the book. With nothing changed there's no save at all, and the edit key returns to reading with the book's current text.
- **Refused:** `message` is shown (if any); writing goes on and nothing is charged. No answer counts as a refusal (logged).

`onDiscard` is the close prompt's Discard. Neither ends the session by itself; `onEnd` follows when the book closes.

## Clients

Every DLL that calls the API is listed in Ink & Quill's MCM, found by the address it called from: nothing to register. `SetClientName(name)` (optional, any time) gives the name it's listed under; otherwise it's the DLL's file name. See [SETTINGS.md](SETTINGS.md#the-mod-list).

## Headings

A line starting `# ` (or `## `) is shown as a heading, in the editor and when the book is read ([EDITOR.md](EDITOR.md#headings)). Clients keep the `# ` in the text they store and render, and don't escape `#`; their reading text gets the same treatment from Ink & Quill's SWF.

## Writing materials

`HasQuill`, `HasInk`, `UseInk` (`IQ_INK_NONE`, `IQ_INK_USED`, `IQ_INK_RAN_DRY`), `CanBleed`, `Bleed`: the same inkwells and costs as the editor (the player's settings: uses per inkwell, blood cost; they don't check `Writing.RequireQuillAndInk` or `Writing.Blood`, which are the editor's), for a client with its own writing UI. Game state: on the game's thread, as everything that touches the inventory (a UI task or an SKSE task).
