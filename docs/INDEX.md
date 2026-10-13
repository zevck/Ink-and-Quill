# Ink & Quill developer docs

The docs are the source of truth for how the code works. A change that makes a doc wrong fixes the doc in the same change.

| Doc | Covers |
|---|---|
| [API_DESIGN.md](API_DESIGN.md) | The framework's design and decisions: what it owns, marked text, costs, sessions, blanks, the C++ API, why no Papyrus API |
| [API.md](API.md) | The C API as built: getting it, versions, the rules (threads, strings, onEnd), starting, Reload, blanks, clients' keys, prompts, reacting to typing (`onChange`), suggestions (`Suggest`), saving, the client list, bookmarks (tags in the reading text), Special Ink (the session's `way`, `onBegin`), the `IQ_IsWriting` export |
| [EDITOR.md](EDITOR.md) | The editor as built: writing mode, quill, ink (renamed inkwells) and blood, merchants selling them, keys, input (blocking other mods' hotkeys; no IME), strings, starting, suggestions, saving, closing, bookmarks (the bookmark list), the writing sound, the quill cursor (the game's quill as the caret), Special Ink (the Quill of Gemination, Enthir, Maven, Snazzy's versions) |
| [SETTINGS.md](SETTINGS.md) | The INI (the keys: edit, bookmarks), other mods' quills and inkwells (`[Materials]`), the MCM and its natives, the list of mods using Ink & Quill |
| [DEVELOPMENT.md](DEVELOPMENT.md) | Building, deploying, releases (the FOMOD zip), the ESP's Spriggit source, Papyrus, the SWF |
