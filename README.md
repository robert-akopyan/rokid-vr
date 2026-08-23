# RokidVR

Native Windows 11 SteamVR HMD driver for Rokid Max (`VID 04D2`, `PID 162F`).
It reads the Rokid HID directly and does not use OpenTrack, PhoenixHeadTracker,
TrackIR, FreeTrack, UDP, or mouse emulation.

## Quick Start

1. Connect Rokid Max as an extended Windows display and USB HID.
2. Install SteamVR, then run `install-driver.ps1` once from `build\package`.
3. Start `RokidVRLauncher.exe`; USB and Display must be detected.
4. Keep the glasses still for about three seconds while the IMU calibrates.
5. Click **Start SteamVR**. Use **Ctrl+Alt+R** to recenter.
6. Start a VR application. For War Thunder, click **Start Game** and choose its
   Steam **VR mode using SteamVR** launch option.

## Architecture

```text
Rokid USB HID -> independent reader/fusion thread -> OpenVR HMD pose
                                                     |
SteamVR application -> SteamVR compositor -----------+
                                                     |
                +------------------------------------+----------------+
                |                                                     |
        IVRDisplayComponent                                IVRVirtualDisplay
        extended Rokid output                         shared D3D11 backbuffer
                                                              |
                                                borderless DXGI presenter
                                                              |
                                                   physical Rokid display
```

The default `extended` video path lets SteamVR own the window on the real
desktop-connected display. The fallback `virtual` path opens SteamVR's shared
texture on the Rokid GPU and draws it to a two-buffer flip-model swap chain.
There is no per-frame GPU-to-CPU-to-GPU copy.

## Build

Use an x64 Developer PowerShell or an environment where CMake is on `PATH`:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The configure step downloads the pinned official `openvr_driver.h` and verifies
its SHA-256. Output is placed in:

```text
build\package\RokidVRLauncher.exe
build\package\rokidmax\driver.vrdrivermanifest
build\package\rokidmax\bin\win64\driver_rokidmax.dll
```

## Installation

The scripts use SteamVR's supported external-driver registration and do not
copy into or modify the SteamVR installation:

```powershell
build\package\install-driver.ps1
build\package\uninstall-driver.ps1
```

Restart SteamVR after registration or a driver/config update.

## Configuration

On first use the launcher creates `%LOCALAPPDATA%\RokidVR\config.ini` with safe
defaults. `config.example.ini` documents the same options:

```ini
[video]
path=extended
output_mode=auto
vsync=1
swap_eyes=0
```

- `path=extended`: preferred `IVRDisplayComponent` path.
- `path=virtual`: `IVRVirtualDisplay` and the built-in D3D11 presenter.
- `output_mode=mono_left|mono_right|sbs|auto`: applies to the virtual presenter.
- `auto`: SBS when the active output is at least 3000 pixels wide, otherwise
  mono-left. The code enumerates actual Windows modes and does not switch or
  hard-code a 3840x1080 mode.

The launcher preserves the primary monitor, never enables Clone mode, and
never makes Rokid primary.

## First run without a game

1. Run `RokidVRLauncher.exe --diagnose` and verify the HID path, non-zero packet
   rate, Rokid display rectangle, DXGI adapter/output, and display modes.
2. Start SteamVR. The monitor should show `Rokid Max` as the HMD.
3. Open SteamVR VR View and check right/left, up/down, and roll directions.
4. Turn approximately 30 degrees and press Ctrl+Alt+R. The new direction must
   become forward without restarting SteamVR.
5. Confirm the physical Rokid display shows the compositor. If extended mode
   does not present reliably on the installed SteamVR/NVIDIA version, set
   `path=virtual` and `output_mode=mono_left`, then restart SteamVR.

## War Thunder demo

War Thunder now uses OpenXR. Set SteamVR as the current OpenXR runtime in
**SteamVR Settings > OpenXR**, connect Rokid, and start SteamVR before the game.
The official War Thunder instructions say either enable VR in the standalone
launcher or, on Steam, select the offered **VR mode using SteamVR** launch
option. The launcher's generic default opens Steam app `236390`; it deliberately
does not invent undocumented command-line flags.

Acceptance checklist:

```text
SteamVR HMD: Rokid Max, active
War Thunder: OpenXR VR mode through SteamVR
Physical Rokid display: compositor image visible
Head rotation: SteamVR HMD orientation changes
Ctrl+Alt+R: recenter works
OpenTrack/Phoenix: not running and not installed as dependencies
```

## Diagnostics and logs

```powershell
build\package\RokidVRLauncher.exe --diagnose
```

Logs are written to `%LOCALAPPDATA%\RokidVR\logs\rokidvr.log`. Normal logging
does not include every IMU packet.

## Current verification status

- x64 Release build: verified with MSVC 19.50 and Windows SDK 10.0.26100.
- Unit tests: quaternion normalization/recenter/axis mapping/wrap and projection
  tests pass.
- Driver ABI smoke test: DLL loads and exports the current
  `HmdDriverFactory` provider interface.
- D3D11 presenter: shared GPU texture, shader, flip-model swap chain, frame
  wait, and borderless output were smoke-tested successfully on the physical
  Rokid Max display at 1920x1080@60 Hz on an RTX 4060. Auto, mono-left,
  mono-right, and SBS shader paths all presented successfully.
- Live Rokid HID: verified on `MI_02`, with successful calibration and a
  measured 444-445 packets/s.
- SteamVR 2.16.7 enumeration, physical axis-direction and recenter checks,
  `IVRVirtualDisplay` compositor handoff, and mono output on the Rokid Max were
  tested end to end.
- War Thunder standalone was tested through SteamVR's OpenXR runtime. Head
  tracking and mono video output work; game/dashboard input behavior is still
  being refined.

## Known limitations

- The public Valve `virtual_display` reference is from 2017. This implementation
  uses the current `IVRVirtualDisplay_002` signature.
- Mode switching through the Rokid hardware button remains manual. RokidVR only
  consumes modes Windows reports; it does not send undocumented mode commands.
- The virtual presenter currently needs SteamVR to be restarted after the
  Windows display topology changes (for example, when the primary monitor is
  disconnected).
- A mouse-driven virtual controller is experimental. Positional tracking, room
  scale, native Direct Mode, and a custom OpenXR runtime are out of scope.
- The package uses the current MSVC runtime. Install the Microsoft Visual C++
  x64 Redistributable if SteamVR reports a missing runtime DLL.

## References

- Valve OpenVR driver API and current barebones/tutorial samples:
  https://github.com/ValveSoftware/openvr
- Valve IVRVirtualDisplay reference:
  https://github.com/ValveSoftware/virtual_display
- Monado Rokid protocol reference (BSL-1.0):
  https://monado.pages.freedesktop.org/monado/rokid__hmd_8c.html
- Official War Thunder VR instructions:
  https://wiki.warthunder.com/mechanics/4407-vr-mode-in-war-thunder
