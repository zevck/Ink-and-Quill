# Development

## Build

`.\Build_Local.ps1` (PowerShell 7), modeled on Physical Letters' and SkyrimNet Physical Diaries':

1. **The plugin**, `InkAndQuill.dll`: configures with the `vs2022-windows` preset on the first run (or `-fresh`), then builds only the plugin target, incrementally. Never `/t:Rebuild`: it rebuilds all of CommonLib.
2. **Papyrus** with Pyro (`skyrimse.ppj`, gitignored because it holds the Creation Kit's script path), when `Source\Scripts` has scripts.
3. **The SWF**: each `swf\<name>\` (a JPEXS XML base movie plus ActionScript in `scripts\`) into `Interface\<name>.swf` with JPEXS `ffdec-cli`, when its sources changed. Variants (`<name>.<variant>.xml`, e.g. the Convenient Reading one) build into `build\variants\`. One `BookMenu.as` serves every base: what a variant needs from its own mod sits in `//@variant <name>` ... `//@end` blocks (vanilla-only code in `//@variant vanilla`), and each base is compiled from a copy with only its blocks (`build\swf\<base>_src`). The shipped `Interface\book.swf` is vanilla's class plus Ink & Quill's; the build fails if it contains "Convenient Reading".
4. **The ESP**, `InkAndQuill.esp` (ESL-flagged), from its Spriggit YAML in `spriggit\InkAndQuill`, when that changed. Edited as text; the `.esp` is never committed. After an edit in the CK or xEdit, run `.\utilities\esp_to_spriggit.ps1` first.
5. **Deploy** to `$defaultOutputPath` and every `$additionalOutputPaths` entry: one MO2 mod folder per test instance. A DLL or ESP locked by a running game is reported; the rest is still copied.

Options: `-noDeploy`, `-skipScripts`, `-skipSwf`, `-skipEsp`, `-fresh`. The outcome also goes to `%TEMP%\iq-build-result.json`.

## Releases

`.\Build_Release.ps1` makes the archive players install, `build\release\<name> <version>.zip`, the name being `fomod\info.xml`'s `<Name>` with "&" written "and" ("Ink and Quill - A Writing Framework": MO2 guesses the mod's name from the file name and stops at "&", leaving "Ink") (a FOMOD; zip so no extra tool is needed to open it):

1. It refuses uncommitted changes (a release is a commit); `-allowDirty` makes a test release, marked `-dirty`.
2. It builds everything with `Build_Local.ps1 -noDeploy`; `-skipBuild` packs the last build.
3. It stages `00 Core` (the DLL, the ESP, `Scripts`, `Source\Scripts`, `Interface\Translations`), `01 Vanilla` and `02 Convenient Reading` (one `book.swf` each: `Interface\book.swf` and the variant's) and `fomod\`, whose version it sets from `CMakeLists.txt` (the one place the version lives).
4. It checks what it staged: both SWFs uncompressed with the writing marker, every translation with English's keys, `ModuleConfig.xml` valid with every folder it installs present.
5. It zips the stage. The outcome is printed and written to `%TEMP%\iq-release-result.json`.

The installer picks one book menu; Convenient Reading is recommended when `Convenient Reading.esp` is active. That option needs Convenient Reading installed (its `bookmenu.swf`, meshes and `Convenient Reading.ini` stay its own) and Ink & Quill winning the conflict over `book.swf`.

## Machine settings

`Build_Config_Local.ps1` (gitignored; copy `Build_Config_Local.template.ps1` to start one): `$defaultOutputPath`, `$additionalOutputPaths`, `$ckPath` (Pyro's game path), `$pyroPath` (optional; default the VS Code papyrus-lang extension's), `$ffdecPath` (JPEXS), `$swfVariant` (optional), `$spriggitPath` (optional; default `external\`, downloaded and hash-checked once), `$defaultThreads`.

## First clone

`git submodule update --init --recursive` (CommonLibSSE-NG, with its nested `openvr`). vcpkg via `VCPKG_ROOT`; dependencies in `vcpkg.json`, linked statically.
