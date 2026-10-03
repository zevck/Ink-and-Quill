# Ink & Quill - Writing Framework

**Status: in development.** Nothing is built yet beyond the project skeleton.

An SKSE plugin (SE, AE; one DLL) that lets the player write in books in the book menu: a quill, an inkwell with a limited number of uses, or their own blood. Ink & Quill is a framework: it owns the editor and the writing materials, and other mods give the text a meaning. SkyrimNet Physical Diaries (journals) and Physical Letters (letters) are its first clients; any mod can ask the player to write something through its Papyrus API. It doesn't need SkyrimNet.

The planned API: [docs/API_DESIGN.md](docs/API_DESIGN.md).

## Requirements

SKSE, Address Library for SKSE Plugins. Optional: SkyUI, for the MCM (without it, the settings are in `SKSE\Plugins\InkAndQuill.ini`).

## License

GPL-3.0-or-later ([LICENSE.md](LICENSE.md)).
