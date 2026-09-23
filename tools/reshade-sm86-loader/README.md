# SM86 Loader for ReShade

A tiny x64 ReShade add-on that loads `dlssg_sm86.asi` without using a second proxy DLL.

## Install

Put the built file beside ReShade's DLL / in ReShade's add-on search path:

```
SM86Loader.addon64
```

Then place `dlssg_sm86.asi` in either:

```
<game>\plugins\dlssg_sm86.asi
```

or:

```
<game>\dlssg_sm86.asi
```

Keep `dlssg_sm86.ini` where your SM86 build expects it (normally beside the rendering EXE).

The loader writes:

```
<game>\SM86Loader.log
```

and also sends messages to the ReShade log when the logging export is available.

## Important timing note

Standard external ReShade add-ons are loaded when ReShade initializes the graphics device. That is later than a native proxy such as `version.dll`. Therefore this loader can load the ASI without proxy conflicts, but a game that performs its DLSSG/Streamline architecture gate before ReShade loads external add-ons may still be too early for SM86's native-unlock path.

## Behavior

- Does not replace `dxgi.dll`, `version.dll`, `winmm.dll`, etc.
- Does not unload `dlssg_sm86.asi` before process exit.
- Searches `plugins\dlssg_sm86.asi` first, then the game root.
- Remains loaded even if the ASI is absent, so `SM86Loader.log` explains the failure.
