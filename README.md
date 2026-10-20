# Glacier Mint
![alt text](image.png)
A small C/GTK shader wallpaper manager for Linux Mint Cinnamon on X11. It previews Shadertoy-style fragment shaders with the standard `iTime`, `iResolution`, `iBeat`, and audio uniform names, then runs the selected shader in a desktop-type OpenGL window behind the icons. It also mirrors occasional frames into the X11 root wallpaper so Cinnamon’s workspace overview shows the shader instead of the old static image.

## Source layout

The implementation is split by responsibility: `shader.c` handles GLSL and previews, `storage.c` handles saved wallpapers and assets, `network.c` handles Shadertoy API work, `browser.c` handles WebKit integration, `ui.c` builds the GTK interface, `actions.c` contains UI actions, `wallpaper.c` owns the X11 desktop backend, `widgets.c` holds the desktop widget model, settings file and drawing code shared by the editor and the wallpaper, `feeds.c` collects the live data widgets show, and `beat.c` tracks the music's tempo and beat for music-reactive shaders. Shared types and interfaces live in `app.h`; `main.c` only initializes the app and dispatches wallpaper mode.

The UI has six tabs: Window Styling, local wallpapers in a scrollable grid, a Widgets editor with draggable desktop widgets, an embedded Shadertoy browser with an injected import button plus paste-code fallback, GNOME Extensions, and settings. The last loaded shader is restored on startup.

Window Styling previews a sample window and saves border width, corner radius, color, opacity, and enabled state to `~/.local/share/glacier-mint/window-styling.ini`. The Extensions tab installs `Glacier Mint Window Styling` for GNOME Shell and Cinnamon; enable it in the desktop's extension manager and it reads the same file, so changes made in Glacier Mint are applied to real windows.

## Widgets

Add widgets with the **+** button on the Widgets tab, drag them into place, and click one to edit it. Every widget has font, size, scale, bold/italic, text color (solid or gradient), accent and secondary colors, an optional rounded background, letter spacing, stretch and minimum width. The Options row below holds the settings for that kind of widget.

| Widget | What it shows | Notable options |
| --- | --- | --- |
| Time, Date | The original text clock and date | 12/24-hour, long/short date |
| Digital clock | Clock with tabular digits or drawn seven-segment digits | seconds, blinking colon, AM/PM, date line |
| Analog clock | Clock face with hands | diameter, numerals, ticks, ring, second hand, smooth sweep, hand width |
| PC performance | CPU, GPU, RAM, swap, disk, temperatures, network, uptime | bars, graphs or text; which rows to show; bar length |
| Audio EQ | Spectrum of whatever is playing | bars, mirrored, line or dots; bar count, size, gap, sensitivity, smoothing, peak caps |
| Now playing | Title, artist, album art, progress and play state from any MPRIS player (Spotify, Firefox, VLC…) | layout, art size, which lines to show, player filter, hide when idle |

The editor preview and the wallpaper use the same drawing code, so what you see in the Widgets tab is what appears on the desktop; the running wallpaper picks up changes within a quarter second. Widgets are saved to `~/.local/share/glacier-mint/widgets.ini`; an older `widgets.conf` is converted the first time the editor opens.

Data sources: GPU load is read from `gpu_busy_percent` (AMD; other GPUs show n/a), temperatures from hwmon, and the EQ captures the default output's monitor with `parec` (part of `pulseaudio-utils`, which also works on PipeWire), only while an EQ widget exists. The desktop wallpaper sits behind the file manager's icons, so widgets are display-only and the Now playing widget cannot take clicks.

## Build and run

```sh
sudo apt install build-essential libgtk-3-dev libepoxy-dev libjson-glib-dev libcurl4-openssl-dev libx11-dev libxext-dev
make
./glacier-mint
```

The Wallpapers tab starts empty. Import shaders through the embedded browser or paste code; each imported shader is saved in its own folder under `~/.local/share/glacier-mint/wallpapers/`, along with downloaded texture channel files and metadata. The embedded browser works without an API key. For the optional REST search path, create a Shadertoy app key and either enter it in Settings or run:

```sh
export SHADERTOY_API_KEY=your-key
./glacier-mint
```

The API only exposes shaders marked `public + api`; this is a Shadertoy access rule, not a limitation imposed by Glacier Mint. Glacier Mint uses the first `Image` render pass and supports downloaded 2D texture channels. Complex multi-pass, cubemap, video, and audio inputs may not compile or animate identically. Music-reactive shaders can use `iBeat` (a beat counter whose whole numbers land on the music's beats, 60 BPM when nothing is playing), `iBPM`, `iAudioLevel` and `iAudioBass`. When a wallpaper uses any of them, the wallpaper process follows whatever is playing through `parec`; each song's tempo is locked after about 10 s of beat, and a new song is recognised from the media player's track or a silent gap. Previews run them at a steady 60 BPM.

Settings sets the wallpaper's peak frame rate (24 to 240 FPS, plus your monitor's refresh rate) and V-sync, which shows frames in step with the monitor and so caps the wallpaper at its refresh rate. Both are remembered and apply to the running wallpaper immediately. Run a wallpaper with `GLACIER_MINT_DEBUG_FPS=1` to print the frame rate it actually reaches; heavy shaders can be limited by the GPU before the cap.

`./glacier-mint --restore` starts the wallpaper last chosen with **Set as current wallpaper** (recorded in `~/.local/share/glacier-mint/active.ini`; **Stop wallpaper** clears it). Use it to start the wallpaper at login, for example from `~/.config/autostart`.

Wayland sessions are not supported by the desktop-window backend; use an X11 session in Linux Mint.

For the embedded browser, install WebKitGTK development files and rebuild:

```sh
sudo apt install libwebkit2gtk-4.0-dev
make
```

If WebKitGTK is unavailable, the app still builds and shows a button to open Shadertoy in the normal browser.
