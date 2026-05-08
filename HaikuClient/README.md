# softKMClient — Haiku-to-Haiku sender

This is the **left-side Haiku** application that mirrors the role of the macOS
`softKM.app` — it captures keyboard and mouse input on the left machine and
forwards them to the softKM server running on the right Haiku machine.

```
┌─────────────────┐                    ┌─────────────────┐
│  Haiku (left)   │                    │  Haiku (right)  │
│                 │    TCP             │                 │
│  Filter add-on  │ ─────────────────► │  NetworkServer  │
│  (capture)      │   Port 31337       │  (receive)      │
│                 │                    │                 │
│  Deskbar Tray   │                    │  Deskbar Tray   │
└─────────────────┘                    └─────────────────┘
```

## How it works

1. **BInputServerFilter add-on** (`SoftKMClientFilter`) is installed in
   `/boot/home/config/non-packaged/add-ons/input_server/filters/`.
   When active it intercepts all keyboard and mouse events and forwards
   them via a named Haiku port to the main app.

2. **SoftKMClient** main app:
   - Watches the mouse position for edge dwell (right edge by default)
   - When the dwell threshold is reached it tells the filter to start
     capturing and sends `CONTROL_SWITCH direction=0` to the right Haiku
   - All captured events are encoded with the same binary protocol as
     macOS softKM and sent over TCP
   - When the right Haiku returns control (left edge dwell there) it
     sends back `CONTROL_SWITCH direction=1` and the filter deactivates

## Build

```bash
cd HaikuClient

# Build the filter add-on first
cd addon
make
sudo cp objects.*/SoftKMClientFilter \
    /boot/home/config/non-packaged/add-ons/input_server/filters/
cd ..

# Build the main app
make
```

The input_server automatically loads new filters — no restart needed.

## Run

```bash
./objects.*/softKMClient
```

The app installs itself in the Deskbar tray.

## Settings

| Setting      | Default              | Description                           |
|--------------|----------------------|---------------------------------------|
| Host         | taurus.microgeni.synology.me | Right Haiku hostname/IP       |
| Port         | 31337                | softKM server port on right Haiku     |
| Switch edge  | Right                | Edge that sends control to right      |
| Return edge  | Left                 | Edge that returns control to left     |
| Dwell time   | 300 ms               | Time to hold at edge before switching |

## Protocol

Identical to the macOS ↔ Haiku protocol (see `HaikuOS/src/network/Protocol.h`).
Haiku key codes are sent **natively** (no macOS→Haiku translation needed).

## Differences from macOS sender

| Feature            | macOS softKM         | HaikuClient                     |
|--------------------|----------------------|---------------------------------|
| Event capture      | CGEvent tap          | BInputServerFilter add-on       |
| Key code format    | macOS virtual keys   | Haiku key codes (native)        |
| Modifier mapping   | macOS CGEventFlags   | Haiku B_*_KEY flags (native)    |
| Cursor lock        | CGAssociateMouseAndMouseCursorPosition | Filter warps cursor back to edge |
| Network            | NWConnection (Swift) | BSD sockets (C++)               |
