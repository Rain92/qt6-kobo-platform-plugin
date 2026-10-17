# qt5-kobo-platform-plugin
A Qt platform backend plugin for Kobo E-Ink devices, for Qt 5.15 and Qt 6. \
Supports most of the Kobo platform functons including E-Ink screen refresh control,
input management for buttons and touchscreen, frontlight control, WiFi management and more. \
At runtime an app using this plugin can be configurend with the environment variable QT_QPA_PLATFORM.

```
export QT_QPA_PLATFORM=kobo:debug:logicaldpitarget=108
./myapp
```

The following parameters are supported:

| parameter | |
|---|---|
| `debug` | additional debug output (`QT_LOGGING_RULES` still applies) |
| `logicaldpitarget=N` | scale the UI as if the screen had N dpi |
| `bpp=N` | framebuffer bitdepth, default 8 (some MTK devices only support 32) |
| `touchscreen_device=/dev/input/eventX` | use this touchscreen instead of auto detection |
| `button_devices=/dev/input/eventX,...` | use these button devices instead of auto detection |
| `touchscreen_protocol=std\|snow\|phoenix\|legacy` | override the touch protocol picked for the device |
| `touchscreen_swap_xy=0\|1`, `touchscreen_invert_x=0\|1`, `touchscreen_invert_y=0\|1` | override the touch panel orientation quirks |
| `touchscreen_max_range_x=N`, `touchscreen_max_range_y=N` | scale touch coordinates from this panel range instead of treating them as pixels |
| `nograb` | don't grab the button devices (Nickel will see them too) |

## Device support
The device, its screen and its touch panel quirks are identified by [FBInk](https://github.com/NiLuJe/FBInk),
so updating the FBInk submodule is usually all a new Kobo model needs.
Frontlight and battery details are in `src/kobodevicedescriptor.cpp`.

Input devices are found by probing their capabilities (`fbink_input_scan`), so it doesn't matter
which `/dev/input/eventX` they end up as. Touch panels come in four flavours, which are picked per model
(see `KoboInputManager::protocolForDevice`), following what [KOReader](https://github.com/koreader/koreader)
and [Plato](https://github.com/baskerville/plato) do:

| protocol | devices | lift is signaled by |
|---|---|---|
| std | Elipsa, Sage, Libra 2, Clara 2E, Elipsa 2E, Libra Colour | `ABS_MT_TRACKING_ID -1` |
| snow | Aura H2O², Clara HD, Forma, Libra H2O, Clara BW/Colour | pressure 0, contact missing from the frame or `BTN_TOUCH 0` |
| phoenix | Aura, Aura H2O, Aura SE, Glo HD, Touch 2.0, Aura One, Nia | `ABS_MT_TOUCH_MAJOR 0` or contact missing from the frame |
| legacy | Touch A/B/C, Mini, Glo, Aura HD | `ABS_PRESSURE 0` / `BTN_TOUCH 0` |

If touch doesn't work right on a device, run with `kobo:debug` and look at the `kobotouch:` lines:
they show the detected protocol, the panel ranges and every raw event.
The parameters above allow fixing things up without rebuilding.

## Building
The plugin builds FBInk itself (`make pic` in the FBInk submodule), using the compiler of the Qt kit.

Qt 5 and Qt 6 with qmake:
```
/path/to/qt-kobo/bin/qmake koboplatformplugin.pro && make
```

Qt 6 with CMake:
```
/path/to/qt-6-kobo/bin/qt-cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Apps that use `koboplatformfunctions.h` have to be rebuilt when `kobodevicedescriptor.h` changes,
since the device descriptor is passed by value.

## Cross-compile for Kobo
See https://github.com/Rain92/kobo-qt-setup-script
