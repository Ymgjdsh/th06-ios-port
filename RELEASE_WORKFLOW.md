# Source version workflow

The Git repository is the authoritative source history. Reused iOS bundle
version numbers and local ZIP names are not version identifiers.

For every tested source release:

1. Keep generated builds, archives, logs, and comparison snapshots out of Git.
2. Record the behavioral change and the actual verification performed in the
   commit message. Do not describe a desktop build as an iOS device test.
3. Publish with `scripts/publish_github_version.ps1` so source preflight and
   GitHub file-size checks run before the commit is created.
4. Use a unique annotated Git tag when a source snapshot is given to testers.

Example:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\publish_github_version.ps1 `
  -Message "Fix LAN repeated-frame pacing and presentation" `
  -Tag "v1.2.5-netplay-pacing-fix1"
```
