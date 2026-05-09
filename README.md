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

softKM automatically detects when you're playing SDL games (OpenArena, Quake ports, etc.) and switches to game mode. This adjusts mouse input to work with SDL's relative mouse mode.

**Note**: Some Quake-engine games require `freelook 1` in the console (press `~`) for vertical mouse look to work.

## License

MIT
