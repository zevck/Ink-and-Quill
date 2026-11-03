# Ink & Quill - A Writing Framework
**Ink & Quill** is a framework for Skyrim Special Edition and does nothing on its own. It allows mods to immersively write books, notes, or letters using a modified book.swf and API. It features a minimal user interface where the user writes directly onto the page. A cursor in the form of an animated quill, 3D rendered in the book menu, marks the users position in the editor.
> [!IMPORTANT]
> **Ink & Quill** is still early in development. If you are interested in using it for one of your mods, please reach out to me first. Anyone is welcome to use it, but breaking changes may be made until version 1.0 is released.

<div align="center"><h2>🪶 Mods Using Ink & Quill 🪶</h2></div>

<div align="center"><a href="https://github.com/zevck/SkyrimNet-Physical-Diaries">Physical Diaries</a> - <a href="https://github.com/zevck/Physical-Letters">Physical Letters</a></div>

## 📋 Requirements
> [!NOTE]
> **Ink & Quill** supports all versions of Skyrim.
- Skyrim Special Edition
- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=description)
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) for the MCM

### Recommended Mods
[Convenient Reading UI](https://www.nexusmods.com/skyrimspecialedition/mods/50202) - An excellent quality of life improvement for the book menu by uranreactor. **Ink & Quill's** FOMOD includes an option for compatibility which combines the two mod's features.

[HFs - Diverse Inkwell and Quill](https://www.nexusmods.com/skyrimspecialedition/mods/132023) - A beautiful model and texture replacer by Halffaces to make your quill look nicer when writing. Choose the Model Swapper version for **Ink & Quill** to pick it up in the book menu. Alternatively, any other model or texture replacer is recommended to be used for the vanilla quill.

[Snazzy Riften AIO](https://www.nexusmods.com/skyrimspecialedition/mods/147594) - An overhaul of some Riften interiors, notably Black-Briar Manor, by gutmaw. After completing Vald's Debt, the Quill of Gemination and the special ink recipe will placed inside, available to steal. [Snazzy Interiors - Riften Black-Briar Manor](https://www.nexusmods.com/skyrimspecialedition/mods/92068) and [Snazzy Location Resources](https://www.nexusmods.com/skyrimspecialedition/mods/147670) also work.

## 🔑 License
**Ink & Quill** is released under the GNU General Public License v3.0 or later (GPL-3.0-or-later) as required by [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG). See [LICENSE](LICENSE) for the full text.

### Client API Exception
As an additional permission under GPLv3 section 7, you may copy, include, compile against, dynamically load, link to, and communicate with Ink & Quill through the public C API declared in `api/InkAndQuillAPI.h`, and you may distribute such client mods under terms of your choice.

This exception applies only to use of the public API. It does not permit copying from or deriving from other Ink & Quill source files except under the GPL.

The public API header (`api/InkAndQuillAPI.h`) is licensed under the MIT License.
