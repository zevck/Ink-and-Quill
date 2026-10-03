# Ink & Quill: API design (draft)

Ink & Quill - Writing Framework lets the player write in books in the book menu. It owns the editor, the writing materials and the shared `book.swf`; other mods (its **clients**) decide what is written, where it's kept and what it means. The first two clients are SkyrimNet Physical Diaries (journals) and Physical Letters (letters).

Status: draft. The editor is copied from Physical Diaries and runs on an internal C++ session ([EDITOR.md](EDITOR.md)); the API below isn't built.

## Scope

**Ink & Quill owns:**

- **`book.swf`** (vanilla and Convenient Reading variants) and the editor in it: fields, caret, typing, pages, blood text.
- **The edit session** on the plugin side: keyboard input (layouts, dead keys, held keys, page-turn suppression), the bridge to the SWF, the close prompt (Save / Discard / Keep writing), the blood prompt, notices ("You need a quill", "Too weak to write in blood").
- **Writing materials:** quills, inkwells and their uses, writing in blood. Items in its ESP: the quill and inkwell lists; a used inkwell is the vanilla item renamed ("Inkwell (9/10)"), no copy records.
- **Blank items' mechanics:** noticing that a registered blank was opened, and showing its client's document in its place.
- **Detection:** whether its `book.swf` is the one the game loads (writing on or off).

**Clients own:** their items (blank journals, parchment), how they're sold and crafted, storage of the text, what saving means (a diary entry in SkyrimNet, a letter in LetterDB), and their own keys and menus beyond the editor (Physical Diaries' new-entry key; how Physical Letters finds a letter's recipient).

**Non-goals:** storing documents (clients store; a Papyrus caller gets the text back and keeps it as it likes), depending on SkyrimNet (nothing in Ink & Quill needs it), layouts (the client renders; [Marked text](#marked-text-agreed-2026-10-02)), rich formatting typed by the player beyond blood text.

## Concepts

- **Marked text:** the document as the client renders it for reading (the book's own HTML), with what the player can't change marked as locked. Ink & Quill knows no layouts.
- **Runs:** the editable text between locks, in order. They're the content, in and out.
- **Costs:** what writing takes: a quill, ink, an item consumed, blood allowed.
- **Session:** one book open for writing, from the first edit to Save or Discard.
- **Blank:** an item that becomes a document when the player first writes in it.

## Marked text (agreed 2026-10-02)

The editor puts no limits on which pages or text can be edited: **everything is editable unless the client locks it.** Confirmed with Physical Diaries' side against its renderer.

- **Locks:** the client renders its reading text with locked parts between U+E002 and U+E003 (`kLockOpen`, `kLockClose`). A journal locks its blank first page, title page, dates, page breaks, each heading with its blank lines, and the breaks after each body; a letter locks nothing (or its "To:" label). The text the book shows for reading has no markers.
- **The editor is built from that text**, laid out as `SetBookText` lays out reading text, so editing matches the page being read with no layout code shared between client and SWF. `[pagebreak]` lines break pages as in reading; a client locks its own.
- **Runs:** every gap between a lock close and the next lock open, **even an empty one** (an empty journal entry is a close straight followed by an open); text before the first lock or after the last only if there is any. A save hands the runs back in order (plain text, `\n` line breaks, blood marked); the client maps them to its own pieces (Physical Diaries: run k is entry k, in session order).
- **Changing the structure** (a new entry, a tear-out, new dates) is the client rendering again with its session's pieces, and the editor reloading with the caret kept in its run. Clients must render from their session state (not from storage that hasn't caught up) and keep the session's order until the save.
- **Typed text** takes the format of the text it's typed into. A client whose renderer formats paragraph breaks apart from the text (Physical Diaries sizes them outside its font tags) passes a font and size hint; without it a newly typed paragraph can sit a few pixels off until the save re-renders it.
- **Client rules** from the review: strip U+E002 and U+E003 out of stored text before rendering (as U+E000 and U+E001 are), or one in an entry breaks the run count; never send marked text through a reading-only conversion (Physical Diaries' Win-1251 step mangles 3-byte characters); render twice (reading, marked) rather than stripping markers from one string, since blood renders differently (colour tags for reading, markers for editing). Sanitizing what the player writes is the client's choice; the editor compares runs against what it loaded, so a client's cleanup doesn't make an untouched run look changed.
- **No fields beyond runs.** A letter's recipient is the client's: a "To:" label locked before an ordinary run, the name matched by the client when the letter is saved (refused with a message if there's no single match). Ink & Quill has no suggestions or required fields (decided 2026-10-02).

Tested in game (AE, 2026-10-02, the layout test, [EDITOR.md](EDITOR.md#the-layout-test)) on a Physical Diaries journal: the pages match reading, locked text stays locked, typing keeps the fonts, ink and blood colours. Open: Cyrillic in the edit field (UTF-8 or Win-1251 through Invoke); SE.

## Costs

Given per session (a blank's registration carries a default):

| Option | Values |
|---|---|
| `quill` | required or not |
| `ink` | one use per save, or none |
| `consume` | an item and count taken on save (parchment), or none |
| `blood` | allowed or not; when chosen, its cost is taken per save |

Ink & Quill checks and charges: it refuses to start without a quill, offers blood when there's no ink (the choice is made when writing starts, so the text shows red while typing), and takes ink, the item and blood only after the client has accepted the save (below). Discarding costs nothing. Ink and blood are charged **per save** for every client; Physical Diaries' per-session ink and per-entry blood go.

## A session

1. **Open.** A book is open in the book menu (pausing the game). The client calls `BeginSession(book, markedText, costs, callbacks)`, from its own key, the edit key (an owner callback) or a blank's registration. Ink & Quill checks the quill and ink (or offers blood), hands the marked text to the SWF and starts edit mode.
2. **Edit.** Keys go to the SWF.
3. **Close.** Closing the book or the edit key asks Save / Discard / Keep writing.
4. **Save.** Ink & Quill reads the runs and calls the client's `OnSave(runs)`. The client validates and stores; it answers **accepted** with its new reading text (the costs are charged; the edit key reads again, the close prompt closes) or **refused with a message** (the session stays open, nothing is charged).
5. **Discard.** `OnDiscard()`; nothing is charged.

A client's own actions while writing (Physical Diaries' new entry and tear-out) end in `Reload(markedText, caretRun)`. The client first reads the runs as the player has them now (`CurrentRuns`, unsaved text included), renders again from those and its change, and hands the result back; the caret stays in its run.

## Blanks

Built ([API.md](API.md#blanks), [EDITOR.md](EDITOR.md#blanks)). `RegisterBlank(form, onOpen)`: reading the blank from the player's inventory (with the menu paused) calls the client, which begins a session with its starting text. On the first accepted save the client makes its item and answers with it (`ReplySaveAsBook`); Ink & Quill charges the costs, removes one blank and shows the new book in the open menu. Read anywhere else (in the world, a container, a shop), a blank is just an empty book. When the blank becomes the client's book is the client's choice (2026-10-02): at once (`ReplaceBlank` in `onOpen`) or on the first save (`ReplySaveAsBook`).

## Limits

None in Ink & Quill: no maximum length, pages or entries. A client imposes its own: it can decline to open a session (Physical Diaries' full journal), and it can refuse a save with a message.

## Content encoding

UTF-8 text, `\n` line breaks. Blood text between U+E000 and U+E001 (Physical Diaries' markers), locks between U+E002 and U+E003 (marked text only).

## C++ API

Built as version 1: [API.md](API.md) and `include/InkAndQuillAPI.h` are the reference; the sketch below is the design it came from.


C ABI, so clients built with another compiler or CRT work: `InkAndQuill.dll` exports `IQ_GetAPI(version)` returning a struct of function pointers; strings are `const char*` (UTF-8), callbacks are plain function pointers with a `void* user` argument, and every struct starts with its size so later versions can grow it. Clients resolve it at `kPostLoad` (`GetModuleHandle` + `GetProcAddress`, as SkyrimNet's API). Calls are game-thread only unless noted.

Sketch:

```cpp
struct IQ_Costs { std::uint32_t size; bool quill; IQ_Ink ink; std::uint32_t consumeForm; int consumeCount; IQ_Blood blood; };
struct IQ_Callbacks {
    std::uint32_t size; void* user;
    IQ_SaveResult (*onSave)(void* user, const char* const* runs, int count);  // result carries the new reading text
    void (*onDiscard)(void* user);
};
struct IQ_API {
    std::uint32_t size, version;
    bool (*IsWritingOn)();
    bool (*BeginSession)(std::uint32_t bookForm, const char* markedText, const IQ_Costs*, const IQ_Callbacks*);
    int (*CurrentRuns)(IQ_RunsReply reply);  // while writing: the runs as the player has them
    bool (*Reload)(const char* markedText, int caretRun);
    bool (*RegisterBlank)(std::uint32_t form, const IQ_Costs*, const IQ_BlankCallbacks*);
    bool (*HasQuill)(); bool (*HasInk)(); IQ_InkResult (*UseInk)(); bool (*Bleed)(float share);
};
```

The materials calls are public so a client with its own UI can still share the inkwells.

## Papyrus API

For mods without a DLL: ask the player to write, get the text back.

```papyrus
; Opens akBook (in the player's inventory) for writing a single body, with asTitle shown above it.
; Returns a request id, or 0 if writing is off or the player can't write (no quill).
int Function RequestText(Form akBook, string asTitle, string asInitialText = "", int aiInk = 1, bool abBloodAllowed = true) global native

; Sent when the request ends: the text on Save, "" and abSaved false on Discard.
; Event InkAndQuill_Written(int aiRequestId, string asText, bool abSaved)  ; an SKSE mod event
```

Papyrus requests are one unlocked run under a locked title, ink per save, no consumed item; anything richer needs the C++ API.

## Settings and detection

- **Writing on/off:** detected from `book.swf` as Physical Diaries does now (the marker `BOOKMENU_WRITING_INTERFACE=<n>` in the uncompressed file); `IsWritingOn()` tells clients. Ink & Quill is the only mod shipping `book.swf`.
- **Keys:** the edit key (start writing in the open book, if a client or blank owns it; on any other book it does nothing) is Ink & Quill's, in its MCM. Clients' own keys (Physical Diaries' new-entry key) stay theirs; they call `BeginSession`.
- **Strings:** the editor's prompts and notices are Ink & Quill's, in its translation files. Messages a client returns (a refused save) are the client's.

## Moving from Physical Diaries

- To Ink & Quill: `swf/book`, `WritingMode`, `WritingTools`, the session and input parts of `BookEditor`, the blood prompt and markers, the quill and inkwell records, the blank-journal book-menu hooks (generalized as blanks), the editor's strings.
- Stays in Physical Diaries: journals (`EditorJournals`), writes to SkyrimNet (`EditorWrites`), blank journal items and their distribution, the new-entry key, diary rendering.
- Physical Letters drops its copies of the quill and ink code and records (just built) and uses the API.
- Saves: Physical Diaries' partly used inkwells (`SNPD_Inkwell1`-`9`) have no equivalent here. Nothing has shipped; development saves lose them (or get a one-time swap to renamed inkwells at load, if wanted).

## Decided

- Ink and blood per save, for every client (2026-10-02).
- No limits in the framework; clients impose their own.
- The edit key does nothing on a book no client owns.
- VR isn't a target: the editor needs a keyboard; it may work there, untested and unsupported.
- Layout belongs to the client: marked text, everything editable unless locked (2026-10-02; [Marked text](#marked-text-agreed-2026-10-02)).
- No "To:" field or suggestions: a letter's recipient is the client's to resolve (2026-10-02).
