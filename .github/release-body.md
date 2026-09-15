## Windows: first launch & SmartScreen

`eclipse-ssh-desktop.exe` is an open-source binary built by GitHub Actions from the
public source in this repository. It does not (yet) carry a paid code-signing
certificate, so Windows SmartScreen shows **"Windows protected your PC"** the first
time you launch a freshly downloaded copy. This is a reputation system, not a virus
verdict — unsigned apps always start with zero reputation.

**Option A - run once through the warning**

1. Extract the zip, run `eclipse-ssh-desktop.exe`.
2. When the blue SmartScreen window appears, click **More info**.
3. Click **Run anyway**. Windows remembers this choice for this file.

**Option B - unblock before launching (no warning at all)**

Right-click the downloaded **zip before extracting** -> **Properties** -> tick
**Unblock** -> **OK**, then extract normally. Or from PowerShell inside the
extracted folder:

```powershell
Get-ChildItem -Recurse | Unblock-File
```

**Verify your download (recommended)**

`SHA256SUMS.txt` in the assets below lists the official SHA-256 of every release
file. Check yours matches:

```powershell
Get-FileHash .\eclipse-ssh-desktop-windows-x86_64.zip -Algorithm SHA256
```

**Linux users**: all assets are self-contained; the AppImage needs
`chmod +x` and runs without any warning.

## What's in the box

| Asset | Use |
|---|---|
| `eclipse-ssh-desktop-windows-x86_64.zip` | Portable Windows build (self-contained, ~106 DLLs, no install needed) |
| `Eclipse-SSH-Desktop-*.AppImage` | Universal Linux build (one file, just run it) |
| `eclipse-ssh-desktop-*.tar.xz` | Portable Linux build (extract and run `run.sh`) |
| `eclipse-ssh-desktop_*.deb` | Debian/Ubuntu package (`sudo apt install ./....deb`) |
| `SHA256SUMS.txt` | SHA-256 checksums of all assets above |
