# Development

## Build

`.\Build_Local.ps1` (PowerShell 7), modeled on Physical Letters' and SkyrimNet Physical Diaries':

1. **The plugin**, `InkAndQuill.dll`: configures with the `vs2022-windows` preset on the first run (or `-fresh`), then builds only the plugin target, incrementally. Never `/t:Rebuild`: it rebuilds all of CommonLib.
2. **Papyrus** with Pyro (`skyrimse.ppj`, gitignored because it holds the Creation Kit's script path), when `Source\Scripts` has scripts.
3. **The SWF**: each `swf\<name>\` (a JPEXS XML base movie plus ActionScript in `scripts\`) into `Interface\<name>.swf` with JPEXS `ffdec-cli`, when its sources changed. Variants (`<name>.<variant>.xml`, e.g. the Convenient Reading one) build into `build\variants\`.
4. **The ESP**, `InkAndQuill.esp` (ESL-flagged), from its Spriggit YAML in `spriggit\InkAndQuill`, when that changed. Edited as text; the `.esp` is never committed. After an edit in the CK or xEdit, run `.\utilities\esp_to_spriggit.ps1` first.
5. **Deploy** to `$defaultOutputPath` and every `$additionalOutputPaths` entry: one MO2 mod folder per test instance. A DLL or ESP locked by a running game is reported; the rest is still copied.

Options: `-noDeploy`, `-skipScripts`, `-skipSwf`, `-skipEsp`, `-fresh`. The outcome also goes to `%TEMP%\iq-build-result.json`.

## Machine settings

`Build_Config_Local.ps1` (gitignored; copy `Build_Config_Local.template.ps1` to start one): `$defaultOutputPath`, `$additionalOutputPaths`, `$ckPath` (Pyro's game path), `$pyroPath` (optional; default the VS Code papyrus-lang extension's), `$ffdecPath` (JPEXS), `$swfVariant` (optional), `$spriggitPath` (optional; default `external\`, downloaded and hash-checked once), `$defaultThreads`.

## First clone

`git submodule update --init --recursive` (CommonLibSSE-NG, with its nested `openvr`). vcpkg via `VCPKG_ROOT`; dependencies in `vcpkg.json`, linked statically.
