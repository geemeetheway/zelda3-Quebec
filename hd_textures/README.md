# HD texture packs

The in-game pack manager automatically discovers safe subfolders containing a
valid `manifest.ini`. `hd_textures/catalog.ini` is optional; entries there pin
listed packs to the top in the order shown:

```ini
[Catalog]
Pack1 = hyrule_hd
Pack2 = dark_world
```

Each listed folder needs a `manifest.ini`:

```ini
[Pack]
Name = Hyrule HD
Description = Crisp replacement tiles for Hyrule Field
Preview = preview.png

[Mode7]
Tile17 = mode7/0017.png
Tile42 = mode7/0042.png
```

`Tile0` through `Tile255` refer to Mode 7 character tiles. Each referenced PNG
must be exactly **32 × 32 pixels** (4× the original 8 × 8 tile) and may use
transparency. Transparent pixels keep the original game pixel. Tile images use
nearest-neighbor sampling; no smoothing or screen-wide filter is applied.

`Description` and `Preview` are optional. Descriptions are shown in the manager.
If supplied, `Preview` must refer to a **96 × 54 PNG** in the pack folder; the
manager displays it without filtering. The preview path follows the same
relative-path safety rules as tile paths.

Example layout:

```text
hd_textures/
  catalog.ini
  hyrule_hd/
    manifest.ini
    preview.png
    mode7/
      0017.png
      0042.png
```

This first pack format replaces enhanced Mode 7 tiles only. It does not replace
ordinary background or character sprites, and the repository does not include
replacement artwork. Packs are optional; missing, malformed, or incorrectly
sized PNGs are skipped safely. Select **DESACTIVE** in the manager to return to
the original Mode 7 tiles.

If a valid pack is selected while the enhanced renderer or enhanced Mode 7 is
off, the manager stores that selection and asks for a restart. On the next
launch, the selected pack enables both settings before the renderer is created,
so its 4× replacement tiles fit the high-resolution render target. The setting
is saved by the pack folder name, so discovery order changes do not select a
different pack. If the saved folder is missing, startup reports it and keeps
the original tiles active.
