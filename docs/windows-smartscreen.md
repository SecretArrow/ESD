# Windows SmartScreen & first-run guide

This guide explains the **"Windows protected your PC"** message that appears when
you first run a freshly downloaded `eclipse-ssh-desktop.exe`, why it appears, how
to get past it safely, and how to verify that the binary you downloaded is exactly
the one this project's CI produced.

## Why the warning appears

Eclipse SSH Desktop is free open-source software. Its Windows builds are produced
by GitHub Actions directly from the public source code of this repository, and
every build log is public and inspectable. However, the project does not yet carry
a paid code-signing certificate, so the executable is *unsigned*.

Windows SmartScreen is a **reputation system**, not a verdict about the content of
a file. When you download a file through a browser, Windows attaches a hidden
"mark of the web" to it. On first launch, SmartScreen asks: *has this exact binary
(a hash of it) been seen and run by many people before, and was it signed by a
publisher with good standing?* A brand-new release of an unsigned application
always answers "no" to both — regardless of what the file actually contains — so
SmartScreen shows its standard warning. Every unsigned open-source Windows
application goes through this phase; reputation builds automatically as more
people run the app.

## How to launch anyway (one-time)

1. Extract the zip and double-click `eclipse-ssh-desktop.exe`.
2. When the blue **Windows protected your PC** window appears, do **not** press
   "Don't run". Instead, click the small **More info** link.
3. The window expands and shows a **Run anyway** button plus the app name and
   publisher metadata. Click **Run anyway**.
4. Windows remembers this decision for this exact file — the warning will not
   appear again for the same binary.

## How to avoid the warning completely

Windows blocks files that carry the "mark of the web" (an `Zone.Identifier`
attached by the browser). Removing that mark before launching means SmartScreen
is never consulted:

- **Unblock the archive before extracting** — right-click the downloaded
  `.zip` in Explorer, choose **Properties**, tick **Unblock** at the bottom of the
  *General* tab, click **OK**, then extract the zip normally. Everything extracted
  from an unblocked archive is unblocked too.
- **Unblock after extracting** — open PowerShell in the extracted folder and run:

  ```powershell
  Get-ChildItem -Recurse | Unblock-File
  ```

- **Single file** — right-click `eclipse-ssh-desktop.exe` -> **Properties** ->
  tick **Unblock** -> **OK**.

All three approaches are equivalent and local to your machine; they do not modify
the program.

## Verify your download (recommended)

Every release publishes a `SHA256SUMS.txt` file listing the official SHA-256
hash of each asset. The hashes are produced by the same CI run that built the
binaries, so comparing against them proves the file you received is untouched:

```powershell
# inside your download folder
Get-FileHash .\eclipse-ssh-desktop-windows-x86_64.zip -Algorithm SHA256
```

Compare the output with the matching line in `SHA256SUMS.txt` from the release
page. On Linux, use `sha256sum -c SHA256SUMS.txt`.

## What the project embeds to help

Even unsigned, the binary is not anonymous. The build embeds a Windows version
resource and icon into `eclipse-ssh-desktop.exe`:

- **File description** — *Eclipse SSH Desktop - SSH/SFTP client* (this is the name
  SmartScreen and Explorer display).
- **Product name, version and publisher** — visible in Explorer properties and
  the taskbar tooltip, kept in sync with the release version automatically.
- **Multi-size application icon** — used in Explorer, taskbar, shortcuts and
  Alt-Tab; also embedded at runtime for the window and tray icon.

Blank metadata is one of the strongest false-positive signals for antivirus
heuristics, so a fully described binary meaningfully reduces the chance of
Windows Defender flagging the file.

## If Windows Defender still quarantines the file

Rarely, an antivirus may flag an unsigned binary as *PUA* (potentially unwanted
application) — a generic heuristic, not a detection. If that happens:

1. Check the quarantined item's SHA-256 against `SHA256SUMS.txt`. If it matches
   the official hash, the alert is a false positive by definition.
2. Restore the file from Defender's protection history and add an exclusion for
   the application folder if needed.
3. Please [open an issue](https://github.com/SecretArrow/ESD/issues) so we can
   track it and submit the binary to Microsoft for whitelisting.

## Roadmap: real code signing

The permanent fix is signing. The plan, in order of likelihood:

- **Azure Trusted Signing** — Microsoft's managed signing service (individual
  certificate tier); gives SmartScreen-accepted signatures for open-source
  projects at low cost, with public build provenance.
- **Open Source Developer certificate** — a paid OV certificate removes the
  initial "unknown publisher" state and accumulates reputation per publisher
  rather than per binary.

Both require account/identity verification that cannot be automated in CI today,
which is the only reason the current releases are unsigned.
