# hyprtab

An Alt-Tab style window switcher for [Hyprland](https://hyprland.org/), built with Qt6/QML.
A small overlay panel lists your windows side by side, and selecting one moves focus to it.

## Installation

### From the AUR

```sh
paru -S hyprtab
```

### Using the installer script

`install.sh` checks dependencies, builds the binary, installs it to `~/.local/bin/hyprtab`,
and creates a (disabled by default) systemd user service:

```sh
./install.sh
```

### Building from source

Requirements: CMake, a C++20 compiler, Qt6 (Quick/QML), and Hyprland.

```sh
git clone https://github.com/nagnagi/hyprtab.git
cd hyprtab
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
# build/hyprtab is the resulting binary; put it on your PATH or invoke it by full path
```

## Features

- Alt-Tab style MRU (most-recently-used) window list shown as a compact panel centered on the focused monitor (default 920x212), not a full-screen overlay
- Keyboard-driven: hold Alt, press Tab to cycle, release Alt (or press Enter) to switch
- Mouse support: click a card to select and commit
- Live refresh while open (every 500 ms) without flicker; the selection is preserved by window address
- Auto-cancel: clicking outside the panel or losing focus for over a second closes it without switching
- Window icons resolved from the icon theme, `.desktop` `Icon=` entries, or direct icon-directory lookup
- Self window is hidden from the list, and windows on special workspaces are excluded
- Focus switching via `hyprctl dispatch focuswindow`, with a fallback for the Lua-based `hyprctl` of Hyprland >= 0.57
- IPC over a Unix socket at `$XDG_RUNTIME_DIR/hyprtab.sock`

## Usage

Run the GUI process in the background (no arguments):

```sh
hyprtab &
```

or via the systemd user service:

```sh
systemctl --user enable --now hyprtab.service
```

Send commands from the CLI (`hyprtab <command>`):

| Command | Action |
|---|---|
| `show` | Show the list (second MRU entry preselected) |
| `next` / `prev` | Show if hidden, then move the selection forward/backward |
| `select <index>` | Select the entry at the given position |
| `commit` | Focus the selected window and close |
| `cancel` / `hide` | Close without changing focus |
| `activate <address>` | Focus a window directly, bypassing the GUI |
| `list` | Print the MRU-ordered list to stdout (debug) |
| `refresh` | Re-fetch the window list |
| `ping` | Health check (replies `pong`) |

### Hyprland configuration example

Add the following to `~/.config/hypr/hyprland.conf`:

```conf
# Start the GUI at session login
exec-once = hyprtab

# Open the switcher with Alt+Tab and cycle with Tab
bindr = ALT, Tab, exec, hyprtab next
bindr = ALT, Shift, Tab, exec, hyprtab prev
bind = ALT, Return, exec, hyprtab commit
bind = ALT, Escape, exec, hyprtab cancel
```

While the overlay has focus, releasing the Alt key is detected and commits the current
selection, so a quick Alt+Tab tap switches immediately. Alternatively, use a submap:

```conf
bind = ALT, Tab, submap, hyprtab
submap = hyprtab
bind = , Tab, exec, hyprtab next
bind = Shift, Tab, exec, hyprtab prev
bind = , Return, exec, hyprtab commit
bind = , Escape, exec, hyprtab cancel
bind = , release, exec, hyprtab commit
submap = reset
```

To pin the panel size in addition to float/center (applied automatically on show), add:

```conf
windowrulev2 = float, class:^(hyprtab)$
windowrulev2 = center, class:^(hyprtab)$
windowrulev2 = size 920 212, class:^(hyprtab)$
```

## License

[MIT License](LICENSE)

## Contributing

Issues and pull requests are welcome at <https://github.com/nagnagi/hyprtab>.
Please keep changes minimal and match the existing code style. For bug reports, include
your Hyprland version (`hyprctl version`), Qt version, and the output of `hyprtab list`.
