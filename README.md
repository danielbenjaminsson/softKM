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

## Known Limitations

### Games / SDL Applications

softKM works best with regular Haiku applications. **Fullscreen games using SDL** (like OpenArena, Quake ports, etc.) may have input issues:

- Mouse movement may be laggy or unresponsive
- Some keys may not register correctly

This is because SDL fullscreen games often use low-level input handling that bypasses the normal application message path that softKM uses for input injection.

**Workaround:** Use a local keyboard/mouse connected directly to your Haiku computer when gaming.

## License

MIT
