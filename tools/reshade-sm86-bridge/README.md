# ReShade SM86 Bridge

This bridge is intended for the case where ReShade owns `dxgi.dll` and loading
`SM86\version.dll` directly through ReShade's `ADDON.LoadFromDllMain` fails with
Win32 error 126.

The bridge itself is loaded early by ReShade, then it loads the original SM86
`version.dll` from a subdirectory using the Windows native loader without
ReShade's `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` flags.

## Layout

```
<game>\
  dxgi.dll
  ReShade.ini
  renodx-dlss.addon64
  SM86Bridge\
    SM86Bridge.dll
  SM86\
    version.dll
    dlssg_sm86.ini
```

## ReShade.ini

```ini
[ADDON]
DisabledAddons=Generic Depth,Effect Runtime Sync
LoadFromDllMain=SM86Bridge\SM86Bridge.dll
LoadFromDllMain=renodx-dlss.addon64
```

Remove the older `SM86Loader.addon64` and any renamed `dlssg_sm86.asi`.

## Logs

The bridge writes:

```
<game>\SM86Bridge.log
```

SM86 should continue to write its own logs according to `dlssg_sm86.ini`.

The bridge tries:
1. `ntdll!LdrLoadDll` with the full path.
2. Plain `LoadLibraryW(full path)` as fallback.

It verifies the actual loaded module path so a system `version.dll` cannot be
mistaken for the SM86 proxy.
