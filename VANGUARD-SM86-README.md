# OptiScaler v0.8.4-SM86-OptiFG

Purpose: use OptiScaler's own `FGInput=upscaler` path in games that have DLSS upscaling but no native frame-generation menu, while keeping `FGOutput=dlssg` and `FGNvngxReplacement=None`.

This branch is based on upstream v0.8.4 commit `8802b2b470db0462fa1ed03a125e793a7c06d735`.

## Vanguard test layout

Keep the normal OptiScaler installation. Put the SM86 proxy you already own here:

```
OptiScaler/
  plugins/
    version.dll
    dlssg_sm86.ini
```

The loader also accepts:

```
OptiScaler/
  dlssg_sm86/
    version.dll
    dlssg_sm86.ini
```

Do not rename the SM86 proxy to `nvngx_dlssg.dll`.

Keep the DLSS-G runtime required by OptiScaler's DLSSG output in its normal OptiScaler location/Streamline setup.

The supplied `OptiScaler.ini` is preconfigured for the first validation:

```ini
[FrameGen]
Enabled=true
FGInput=upscaler
FGOutput=dlssg
FGNvngxReplacement=None

[DLSSG]
AmpereMfgUnlock=true
AmpereMfgMaxFrames=1
InterpolationCount=1
```

Start with 2X. Only increase the generated-frame count after 2X works.

## What is different from newer SM86 integration

The newer fork integration forces External FG ownership. This branch intentionally does **not** do that. The SM86 proxy is sideloaded from the OptiScaler plugin tree, while OptiFG remains the source and OptiScaler's DLSSG output remains active.

## Diagnostics

Enable file logging in `OptiScaler.ini` if needed. The SM86 proxy's own `dlssg_sm86.ini` is preserved if present. If it is missing, OptiScaler creates a conservative diagnostic INI beside `version.dll`.

Use this only in offline/single-player testing. Do not use injected DLL mods in multiplayer/anti-cheat sessions.
