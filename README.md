# Glacier Mint
![alt text](image.png)
A small C/GTK shader wallpaper manager for Linux Mint Cinnamon on X11. It previews Shadertoy-style fragment shaders with the standard `iTime`, `iResolution`, `iBeat`, and audio uniform names, then runs the selected shader in a desktop-type OpenGL window behind the icons. It also mirrors occasional frames into the X11 root wallpaper so Cinnamon’s workspace overview shows the shader instead of the old static image.

## Source layout

The implementation is split by responsibility: `shader.c` handles GLSL and previews, `storage.c` handles saved wallpapers and assets, `network.c` handles Shadertoy API work, `browser.c` handles WebKit integration, `ui.c` builds the GTK interface, `actions.c` contains UI actions, and `wallpaper.c` owns the X11 desktop backend. Shared types and interfaces live in `app.h`; `main.c` only initializes the app and dispatches wallpaper mode.

The UI has four tabs: local wallpapers in a scrollable grid, a Widgets editor with draggable time/date overlays, an embedded Shadertoy browser with an injected import button plus paste-code fallback, and settings. The last loaded shader is restored on startup.

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

The API only exposes shaders marked `public + api`; this is a Shadertoy access rule, not a limitation imposed by Glacier Mint. Glacier Mint uses the first `Image` render pass and supports downloaded 2D texture channels. Complex multi-pass, cubemap, video, and audio inputs may not compile or animate identically. The wallpaper process supplies zero audio values but keeps the standard audio uniform names for compatibility.

Wayland sessions are not supported by the desktop-window backend; use an X11 session in Linux Mint.

For the embedded browser, install WebKitGTK development files and rebuild:

```sh
sudo apt install libwebkit2gtk-4.0-dev
make
```

If WebKitGTK is unavailable, the app still builds and shows a button to open Shadertoy in the normal browser.
