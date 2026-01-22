# softKM

Software keyboard/mouse switch for macOS and Haiku OS. Share a single keyboard and mouse between your Mac and Haiku computer over the network.

## How It Works

Move your mouse to the screen edge to seamlessly switch control between computers. Keyboard and mouse input is captured on macOS and sent to Haiku over the network.

## Requirements

- **macOS**: Accessibility permission required for input capture
- **Haiku OS**: R1 Beta 4 or later

## Installation

### macOS
Download the DMG from [Releases](../../releases), open it, and drag softKM to Applications.

### Haiku
Download the HPKG from [Releases](../../releases) and install via HaikuDepot or `pkgman install <file>`.

## Game Mode

For SDL games (OpenArena, Quake ports, etc.), enable **Game Mode** in softKM Settings:

1. Open softKM Settings on Haiku
2. Check "Game mode (for SDL games)"
3. Save

This adjusts mouse input to work with SDL's relative mouse mode used by games. Minor cursor flickering in game menus is normal.

## License

MIT
