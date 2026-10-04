# Ink & Quill - Writing Framework

**Status: in development.** Nothing is built yet beyond the project skeleton.

An SKSE plugin (SE, AE; one DLL) that lets the player write in books in the book menu: a quill, an inkwell with a limited number of uses, or their own blood. Ink & Quill is a framework: it owns the editor and the writing materials, and other mods give the text a meaning. SkyrimNet Physical Diaries (journals) and Physical Letters (letters) are its first clients; other SKSE plugins can use its C API. It doesn't need SkyrimNet.

**For mod authors:** the C API is one header, [`api/InkAndQuillAPI.h`](api/InkAndQuillAPI.h) (plain C, no dependencies: copy it into your plugin), documented in [docs/API.md](docs/API.md). A mod that only needs to know whether the player is writing calls the export `IQ_IsWriting` ([docs/API.md](docs/API.md#is-the-player-writing)). The design and its reasons: [docs/API_DESIGN.md](docs/API_DESIGN.md).

## Requirements

SKSE, Address Library for SKSE Plugins. Optional: SkyUI, for the MCM (without it, the settings are in `SKSE\Plugins\InkAndQuill.ini`).

## License

GPL-3.0-or-later ([LICENSE.md](LICENSE.md)).
