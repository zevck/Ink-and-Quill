# Template for Build_Config_Local.ps1: machine-specific settings for Build_Local.ps1.
# Copy it to Build_Config_Local.ps1 (gitignored) and fill it in.  See docs/DEVELOPMENT.md.

# Primary output: the MO2 mod folder the build deploys into.  Empty: nothing is deployed.
# Example: "D:\MO2\mods\Ink and Quill - Dev"
$defaultOutputPath = ""

# Other MO2 instances that get the same dev build (one per runtime you test: SE, AE, VR).
# meta.ini is never touched, so each instance keeps its own MO2 metadata.
$additionalOutputPaths = @(
)

# Creation Kit install: Pyro's --game-path (compiler + vanilla/SKSE/SkyUI imports).
# Needed only once there are Papyrus sources.  Example: "D:\Steam\steamapps\common\Skyrim Special Edition"
$ckPath = ""

# Optional: pin a pyro.exe.  Default is the VS Code papyrus-lang extension's copy.
# $pyroPath = ""

# JPEXS Free Flash Decompiler's command-line tool: builds swf\<name>\ into Interface\<name>.swf.
# Example: "D:\Tools\JPEXS\ffdec-cli.exe"
$ffdecPath = ""

# Optional: deploy this SWF variant instead of the default vanilla-based one.
# $swfVariant = "convenient-reading"

# Optional: build parallelism (default 16).
# $defaultThreads = 16

# Optional: Spriggit CLI (the ESP build).  Unset: external\SpriggitCLI-<version>, downloaded and hash-checked once.
# $spriggitPath = ""
