# Ink & Quill

SKSE plugin (CommonLibSSE-NG, C++23; one DLL for SE and AE): "Ink & Quill - Writing Framework". The player writes in books in the book menu; Ink & Quill owns `book.swf`, the editor, quills, inkwells and writing in blood, and its **clients** (SkyrimNet Physical Diaries, Physical Letters, Papyrus mods) store the text and give it meaning. It must not depend on SkyrimNet. VR isn't a target.

**Developer docs: [docs/INDEX.md](docs/INDEX.md).** The planned API is [docs/API_DESIGN.md](docs/API_DESIGN.md). The docs are the source of truth for how the code works. Keep them current: a change that makes a doc wrong fixes the doc in the same change.

## Ground rules

- Clients own their items, storage and meaning; Ink & Quill never stores a client's documents.
- The editor was copied from SkyrimNet Physical Diaries (its `swf/book`, `WritingMode`, `WritingTools` and the session parts of `BookEditor`), see [docs/EDITOR.md](docs/EDITOR.md); until Physical Diaries uses Ink & Quill, its copy is the reference for the editor's history.
- Game state only on the game thread (`SKSE::GetTaskInterface()->AddTask`); the editor's input work as UI tasks (`AddUITask`, see docs/EDITOR.md#input). Every task catches exceptions.
- Text the player sees is translatable (the translation files), never hard-coded English.

## Build

`.\Build_Local.ps1`: incremental plugin build, Pyro, the SWF and the ESP, and deploy to the `Ink and Quill - Dev` mod folder in each test instance (paths in the gitignored `Build_Config_Local.ps1`, made from `Build_Config_Local.template.ps1`). PASS/FAIL also goes to `%TEMP%\iq-build-result.json`. Never `/t:Rebuild`: it rebuilds all of CommonLib. After a fresh clone: `git submodule update --init --recursive` (CommonLib has a nested `openvr` submodule). See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

Ask before committing.
