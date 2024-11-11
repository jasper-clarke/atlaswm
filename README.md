# 🗺️ AtlasWM

<img src="atlaswm.png" alt="AtlasWM Banner" width="30%">

AtlasWM is a dynamic, customizable X11 window manager written in C, designed with flexibility and efficiency in mind. It provides a modern approach to window management while maintaining a lightweight footprint. **(Not even 10MB of RAM and less than 0.1% of CPU!)**

## Features

- **Dynamic Workspace Management**: Support for multiple workspaces with customizable names
- **Flexible Layouts**: Multiple built-in layouts including:
  - Dwindle with gaps (default)
  - Floating
  - Full/Monocle
- **Multi-Monitor Support**: Full Xinerama support with intuitive monitor management
- **Customizable Gaps**: Both inner and outer gaps support through configuration
- **Hot Reloading**: Configuration can be reloaded without restarting the window manager
- **IPC Support**: Control AtlasWM through external commands
- **Startup Programs**: Automatically launch applications on startup
- **Comprehensive Logging**: Detailed logging system for debugging and monitoring

## Dependencies

- Xlib (X11)
- Xft
- Xinerama (optional)

For Debian/Ubuntu:

```bash
sudo apt install libx11-dev libxft-dev libxinerama-dev libfontconfig1-dev
```

For Arch Linux:

```bash
sudo pacman -S libx11 libxft libxinerama fontconfig
```

For NixOS:

**Work in progress**
See [flake.nix](flake.nix) for Nix flake configuration.

## Installation

1. Clone the repository:

```bash
git clone https://github.com/jasper-clarke/atlaswm.git
cd atlaswm
```

2. Build and install:

```bash
make
sudo make install
```

## Configuration

AtlasWM uses TOML for configuration. Create a config file at `~/.config/atlaswm/config.toml`:

See the [example config](config.example.toml) file for a complete list of available options.

## Key Bindings

Default key bindings can be customized in the config file. Available actions include:

- `spawn`: Launch applications
- `killclient`: Close the focused window
- `reload`: Reload AtlasWM configuration
- `cyclefocus`: Cycle through windows
- `togglefloating`: Toggle floating mode for windows
- `focusmonitor`: Focus different monitors
- `movetomonitor`: Move windows between monitors
- `viewworkspace`: View a specific workspace
- `movetoworkspace`: Move a window to a specific workspace
- `duplicatetoworkspace`: Duplicate a window to a specific workspace, this means you can have a single window on two workspaces (on the same monitor) at the same time!
- `toggleworkspace`: Toggle a workspace, this pulls the chosen workspace's windows onto the current workspace in a preview fashion (the windows are only visually there but still belong to the original workspace they came from)
- `quit`: Exit AtlasWM

## Usage

1. Add to your `.xinitrc`:

```bash
exec atlaswm
```

2. Start X server:

```bash
startx
```

3. To reload configuration:

```bash
atlaswm reload
```

## Debugging

AtlasWM maintains logs at `~/.atlaslogs`. The log level can be configured in development, and logs include:

- INFO: General operation information
- WARNING: Non-critical issues
- ERROR: Critical issues that don't prevent operation
- DEBUG: Detailed debugging information

## Contributing

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## License

This project is licensed under the GPL-3.0 License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

AtlasWM draws inspiration from various window managers in the X11 ecosystem while introducing modern features and configuration options. Special thanks to the X.org community and all contributors.

## Status

AtlasWM is currently in active development. While it's stable for daily use, you may encounter occasional bugs. Please report any issues you find through the GitHub issue tracker.
