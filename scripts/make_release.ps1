# make_release.ps1 - publishes GitHub Release v0.1 with aegis.exe attached
$gh = "C:\Program Files\GitHub CLI\gh.exe"
$notes = @"
**What is this?**

Aegis watches your folders (Desktop, Documents, Downloads) and quarantines
files that show ransomware behavior: suspicious extensions (.locked, .enc...),
high entropy of new files (sign of encryption), mass file modification.

**How to run?**

1. Download aegis.exe from this release
2. Put it into any folder
3. Run in terminal: aegis.exe watch
   (or create a start_aegis.bat launcher and double-click it)

Full source code is available right here in the repository.
"@
Set-Content -Path "$env:TEMP\release_notes.md" -Value $notes -Encoding ASCII
& $gh release create v0.1 "build\aegis.exe" `
    --title "Aegis v0.1 - The First Guardian" `
    --notes-file "$env:TEMP\release_notes.md"
