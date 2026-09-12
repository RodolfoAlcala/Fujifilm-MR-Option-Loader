# FUJIFILM MR Option Loader

Minimal C/C++ Raspberry Pi Pico project using the official Pico SDK.

## Build

Open this folder in VS Code. CMake Tools should configure with Ninja and the installed SDK. Build with **CMake: Build** or run:

```powershell
cmake --preset default
cmake --build build
```

If no CMake preset is available, configure directly:

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

The build produces `build/FUJIFILM_MR_Option_Loader.uf2`. Hold **BOOTSEL** while connecting the Pico, then copy the UF2 file to the `RPI-RP2` drive. At startup, the OLED shows how many `.CSV` asset files are on the SD card; press the SEND/SELECT button to enter file selection.

## Adafruit SDIO MicroSD Breakout

This project uses the breakout in SPI mode. Connect the 3.3 V Pico to the breakout as follows:

| Breakout | Pico |
| --- | --- |
| 3V | 3V3(OUT) |
| GND | GND |
| CLK | GP18 |
| CMD | GP19 |
| DAT0 | GP16 |
| CS | GP17 |

Leave DAT1-DAT3 unconnected. Format the card as FAT32 and add a `SY629.CSV` file in the card root. Include a header row with the description in column one and the code in column two. The firmware skips the header and types every second-column code from `SY629.CSV`, one per line, into the USB host.

Connect the SEND/SELECT pushbutton between GP15 and GND, the next-character button between GP13 and GND, and the modality button between GP14 and GND. The firmware enables the internal pull-ups, so pressing a button pulls its input low.

Connect the Adafruit 128x32 OLED as follows: `SDA` to GP4, `SCL` to GP5, `VCC` to 3V3(OUT), and `GND` to GND. The firmware supports the usual OLED I2C addresses `0x3C` and `0x3D`.

For adjustable OLED contrast, connect a 10 kOhm potentiometer between 3V3(OUT) and GND, with its wiper connected to GP26/ADC0. Turn the potentiometer while the Pico is running.

```text
Description,Code
Front panel,TEH3R240LNCSV8JW
Rear panel,V0RGU824TWXCOY96
```

The LCD shows the selected asset. The modality button cycles through `M`, `OV`, `SY`, and `Y`. The next-character button increments the active digit from 0 through 9. A short SEND/SELECT press advances to the next digit; a long press sends every code from the selected file, one per line, into the USB host. For example, `SY629` can be changed to `SY014` and opens `SY014.CSV`.

