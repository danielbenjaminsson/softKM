# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**softKM** is a software keyboard/mouse switch application that shares keyboard and mouse input between macOS and Haiku OS computers over a network, similar to Synergy/Barrier.

## Architecture

```
┌─────────────────┐                    ┌─────────────────┐
│   macOS (mini)  │                    │  Haiku (taurus) │
│                 │    TCP/TLS         │                 │
│  CGEvent Tap    │ ─────────────────► │  NetworkServer  │
│  (capture)      │   Port 24800       │  (receive)      │
│                 │                    │                 │
│  Menu Bar App   │                    │  Deskbar Tray   │
│  (SwiftUI)      │                    │  (BView)        │
└─────────────────┘                    └─────────────────┘
```

**Edge Switching**: Move mouse to right screen edge (configurable) to transfer control to Haiku. Move to left edge to return.

## Build Commands

### macOS (mini)

```bash
cd MacOS
# Open in Xcode
open softKM.xcodeproj

# Or build from command line
xcodebuild -project softKM.xcodeproj -scheme softKM -configuration Release build
```

### Haiku OS (taurus)

```bash
cd HaikuOS
make
# Binary outputs to: objects.*/softKM

# Install and run
make install
./objects.*/softKM
```

## Project Structure

### macOS (`MacOS/softKM/`)

| Path | Purpose |
|------|---------|
| `App/softKMApp.swift` | Main entry point, MenuBarExtra |
| `App/AppDelegate.swift` | Event tap lifecycle, permissions |
| `App/ConnectionManager.swift` | Connection state management |
| `Input/EventCapture.swift` | CGEvent tap for keyboard/mouse capture |
| `Input/EdgeDetector.swift` | Screen edge switching logic |
| `Input/SwitchController.swift` | Capture mode control |
| `Network/Protocol.swift` | Wire protocol encoding |
| `Network/NetworkClient.swift` | TCP client (NWConnection) |
| `Views/StatusBarView.swift` | Menu bar popover |
| `Views/SettingsView.swift` | Settings panel |
| `Settings/SettingsManager.swift` | UserDefaults persistence |

### Haiku OS (`HaikuOS/src/`)

| Path | Purpose |
|------|---------|
| `SoftKMApp.cpp` | BApplication, Deskbar integration |
| `ui/DeskbarReplicant.cpp` | System tray icon (archivable BView) |
| `ui/SettingsWindow.cpp` | Configuration window |
| `network/NetworkServer.cpp` | TCP server listener |
| `network/Protocol.h` | Wire protocol decoding |
| `input/InputInjector.cpp` | Event injection via input_server messages |
| `settings/Settings.cpp` | BFile-based config persistence |

## Wire Protocol

Binary protocol over TCP (little-endian):

```
| Magic (2) | Version (1) | Type (1) | Length (4) | Payload |
| 0x534B    | 0x01        |          |            |         |
```

Event types: `KEY_DOWN` (0x01), `KEY_UP` (0x02), `MOUSE_MOVE` (0x03), `MOUSE_DOWN` (0x04), `MOUSE_UP` (0x05), `MOUSE_WHEEL` (0x06), `CONTROL_SWITCH` (0x10), `HEARTBEAT` (0xF0)

## Configuration

**Default settings:**
- Host: `taurus.microgeni.synology.me`
- Port: `24800`
- Switch edge: Right
- Edge dwell time: 0.3s

## macOS Permissions

The app requires **Accessibility** permission to capture system-wide keyboard/mouse events:
- System Preferences → Privacy & Security → Accessibility → Enable softKM

## CI/CD

Gitea Actions workflow in `.gitea/workflows/build-deploy.yml`:
1. Builds macOS app and installs to `/Applications`
2. SSH to Haiku (taurus), pulls code, builds, and launches

**Manual deployment scripts:**
```bash
./scripts/build-macos.sh --launch    # Build and launch macOS app
./scripts/deploy-haiku.sh            # Deploy to Haiku via SSH
```

**Setup requirements:**
- Gitea runner on macOS (mini) with label `macos`
- SSH key for Haiku access stored as secret `HAIKU_SSH_KEY`
- Nix environment (config at `/Users/daniel/Code/nixos-macos`)

## Development Notes

- macOS uses CGEvent taps (works with Karabiner - remappings applied before capture)
- Haiku event injection uses messages to `application/x-vnd.Be-input_server`
- Keycode translation table maps macOS virtual keycodes to Haiku key codes
- Coordinate systems differ: macOS origin bottom-left (Y up), Haiku origin top-left (Y down)
