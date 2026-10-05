# FUJIFILM MR Option Loader

RP2040 firmware that reads option codes from a microSD card and types them into a USB host as a HID keyboard. It uses a 128x32 SSD1306-compatible I2C OLED for status and three buttons for selecting and sending codes.

## Hardware

The firmware targets the original Raspberry Pi Pico. Connect the peripherals to the Pico as follows.

### OLED

Use a 128x32 SSD1306-compatible I2C display. The firmware uses I2C0 and probes addresses `0x3C` and `0x3D`.

| Display | Pico |
| --- | --- |
| SDA | GP4 |
| SCL/SCK | GP5 |
| VCC | 3V3(OUT) |
| GND | GND |

### microSD breakout

The breakout is used in SPI mode. Leave DAT1-DAT3 unconnected.

| Breakout | Pico |
| --- | --- |
| 3V | 3V3(OUT) |
| GND | GND |
| CLK | GP18 |
| CMD | GP19 |
| DAT0 | GP16 |
| CS | GP17 |

### Buttons

Connect one side of each normally-open button to its GPIO and the other side to GND. The firmware enables internal pull-ups, so a pressed button reads low.

| Button | Pico |
| --- | --- |
| SEND/SELECT | GP28 (physical pin 34) |
| Next-character | GP22 |
| Modality | GP20 |

## microSD files

Format the card as FAT32. Put asset files in the card's root directory, using an uppercase `.CSV` extension and the asset name shown on the display. For example, the default selection `SY629` loads `SY629.CSV`.

Each file must have a header row followed by `description,code` rows. The firmware skips the header and types the second column, one code per line. It loads up to 50 codes per file, with each code limited to 16 characters.

```csv
Description,Code
Example option,ABC123
```

## Controls

- On startup, the OLED shows the number of CSV files found. Press and release SEND/SELECT to enter asset selection.
- Press Next-character to increment the currently selected digit. Digits wrap from 9 to 0.
- Press SEND/SELECT briefly to move the digit selection.
- Press Modality to cycle through `M`, `OV`, `SY`, and `Y`.
- Hold SEND/SELECT for at least 800 ms to type all loaded codes, one per line.

The onboard LED stays on when the OLED does not respond at either supported I2C address. It turns off when a display responds. The firmware retries OLED detection once per second.

## Build

Install CMake, Ninja, the Arm GNU toolchain, and the Raspberry Pi Pico SDK. This project was developed with Pico SDK 2.3.1.

Set `PICO_SDK_PATH` to the SDK directory, then configure and build:

```sh
cmake --preset default -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

The UF2 file is generated at `build/FUJIFILM_MR_Option_Loader.uf2`.

## Flash

1. Hold BOOTSEL while connecting the Pico over USB.
2. Copy `build/FUJIFILM_MR_Option_Loader.uf2` to the `RPI-RP2` drive.
3. Reconnect the Pico normally.

In normal firmware mode, the Pico enumerates as a USB HID keyboard named `Pico Keyboard`. It does not appear as a serial/COM port or mass-storage drive; `RPI-RP2` is only available in BOOTSEL mode. The firmware sends keystrokes to the USB host, so ensure the intended application and input field are selected before holding SEND/SELECT.

See [HARDWARE_DESIGN.md](./HARDWARE_DESIGN.md) for the carrier-board and enclosure design notes.
