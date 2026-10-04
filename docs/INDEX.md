# Ink & Quill developer docs

The docs are the source of truth for how the code works. A change that makes a doc wrong fixes the doc in the same change.

| Doc | Covers |
|---|---|
| [API_DESIGN.md](API_DESIGN.md) | The framework's design and decisions: what it owns, marked text, costs, sessions, blanks, the C++ API, why no Papyrus API |
| [API.md](API.md) | The C API as built: getting it, the rules (threads, strings, onEnd), starting, Reload, blanks, clients' keys, prompts, reacting to typing (`onChange`), suggestions (`Suggest`), saving, the client list, the `IQ_IsWriting` export |
| [EDITOR.md](EDITOR.md) | The editor as built: writing mode, quill, ink (renamed inkwells) and blood, keys, input (blocking other mods' hotkeys; no IME), strings, starting, suggestions, saving, closing, the quill cursor (development only) |
| [SETTINGS.md](SETTINGS.md) | The INI, other mods' quills and inkwells (`[Materials]`), the MCM and its natives, the list of mods using Ink & Quill |
| [DEVELOPMENT.md](DEVELOPMENT.md) | Building, deploying, the ESP's Spriggit source, Papyrus, the SWF |
