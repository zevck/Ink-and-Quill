# The C API

How a client mod uses Ink & Quill's editor. Header: `include/InkAndQuillAPI.h` (plain C; copy it into the client). Code: `src/API.cpp`, over `Editor` and `WritingTools`. The design and its reasons are in [API_DESIGN.md](API_DESIGN.md); the editor's behaviour in [EDITOR.md](EDITOR.md).

**Status:** version 1, built 2026-10-02, no client uses it yet. Blanks and Papyrus aren't in it.

## Getting it

At `kPostLoad` or later:

```cpp
const IQ_API* iq = nullptr;
if (auto* module = GetModuleHandleA("InkAndQuill.dll")) {
    if (auto get = reinterpret_cast<IQ_GetAPI_t>(GetProcAddress(module, "IQ_GetAPI"))) iq = get(IQ_API_VERSION);
}
```

`IQ_GetAPI(version)` returns `NULL` if the DLL is older than the client's header. A newer DLL serves an older client: structs only grow at the end, and `IQ_API::size` and `IQ_Session::size` say how much there is.

## Rules

- **Main thread only.** Every call is made, and every callback comes, on the game's main thread: where SKSE runs UI tasks, and where the paused book menu's input arrives.
- **Strings are UTF-8.** Ink & Quill copies every string it's given before the call returns; strings it passes to a callback are valid until the callback returns.
- **Answers from callbacks** go through `ReplySave` and `ReplyText`, which copy, so a client can answer with a temporary string.
- **`onEnd` is always called, once, last**, for every session handed to `BeginSession` or `BeginSessionOnOpen`, whether it wrote, was discarded, never started (no quill, writing off, blood declined) or was cut off by a load. That's where a client frees what `user` points to.

## Marked text

What a session edits: the book's text as the client renders it for reading, with what the player can't change between U+E002 and U+E003 (UTF-8 `EE 80 82` and `EE 80 83`). The runs between locks are what the player writes, in order; empty runs count, and text before the first lock or after the last is a run only if there is any. Blood text in a run is between U+E000 and U+E001. Full rules, including what clients must do (strip the markers from stored text, render reading and marked text separately, keep their session's order): [API_DESIGN.md](API_DESIGN.md#marked-text-agreed-2026-10-02).

## Starting

- **The edit key.** `AddOwner(owner, user)` at `kDataLoaded` or later. With a book open and nobody writing, the edit key asks each owner in turn with the book's FormID; the one that owns it calls `BeginSession` and returns true. No owner: the key does nothing.
- **A client's own key.** `BeginSession(&session)` with the book menu open, or `BeginSessionOnOpen(bookFormId, &session)` after opening it (`BookMenu::OpenMenuFromBaseForm`): it begins once the menu has the book's text.
- **`IQ_Session`:** `markedText` and `onSave` are required. `runFont` and `runSize` are the format hint for typed text (Physical Diaries: its content font and size, so paragraph breaks get the page's outer size as its renderer gives them). `caretRun` puts the caret at the end of that run (a new entry); -1 starts on the page being read.
- **False** from either: not started (writing off, already writing, a prompt open, an invalid session), and `onEnd` has run. True: the quill and ink checks and the blood prompt follow, and may still end it.

## While writing

- **`IsWriting`, `InBlood`:** blood is chosen when writing starts, so a client rendering a new heading knows whether to make it red.
- **Changing the structure** (a new entry, a tear-out): `CurrentRuns(visit, user)` gives the runs as the player has them, unsaved text included; the client renders its text again from those and its change, then `Reload(markedText, from, count, caretRun, caretOffset)`. `from[i]` is the run that new run `i` was before (-1 for a new one), `count` the number of new runs: each run keeps the text it was last saved with, so unsaved changes still prompt on close. The caret goes to `caretRun` at `caretOffset` (-1: its end).
- **The remove key** (Ink & Quill's, default F10) on a run: `removePrompt(user, run, reply)`; answer with `ReplyText` (the question) or not at all (nothing to remove there). On **Tear out**: `onRemove(user, run)`, and the client removes it with `CurrentRuns` and `Reload`.

## Saving

`onSave(user, runs, count, reply)` on the edit key or the close prompt's Save, when any run changed. Answer with `ReplySave(reply, accepted, message, readingText)`:

- **Accepted:** one use of ink or the blood cost is taken; the edit key returns to reading with `readingText` (the book's text as the client now renders it; `NULL` or empty closes the book), the prompt's Save closes the book.
- **Refused:** `message` is shown (if any); writing goes on and nothing is charged. No answer counts as a refusal (logged).

`onDiscard` is the close prompt's Discard. Neither ends the session by itself; `onEnd` follows when the book closes.

## Writing materials

`HasQuill`, `HasInk`, `UseInk` (`IQ_INK_NONE`, `IQ_INK_USED`, `IQ_INK_RAN_DRY`), `CanBleed`, `Bleed`: the same inkwells and costs as the editor, for a client with its own writing UI. Game state: main thread.
