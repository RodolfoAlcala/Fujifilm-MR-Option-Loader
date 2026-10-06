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
| SEND/SELECT | GP22 |
| Next-character | GP28 (physical pin 34) |
| Modality | GP20 |

## microSD files

Format the card as FAT32. Put asset files in the card's root directory, using an uppercase `.CSV` extension and the asset name shown on the display. For example, the default selection `SY629` loads `SY629.CSV`.

Each file must have a header row followed by description/code rows. The firmware skips the header and types the second column, one code per line. Comma-, semicolon-, and tab-separated files are supported, including quoted fields. It loads up to 50 codes per file, with each code limited to 16 characters.

```csv
Description,Code
Example option,ABC123
```

If the OLED reports `No valid codes`, check that the file has a header, has at least two columns, and that the code column contains non-empty values no longer than 16 characters.

## Controls

- On startup, the OLED shows the number of CSV files found. Press and release SEND/SELECT to enter asset selection.
- Press Next-character to increment the currently selected digit. Digits wrap from 9 to 0.
- Press SEND/SELECT briefly to move the digit selection.
- Button inputs are debounced so one press advances once.
- The asset name is displayed at double size. The selected digit is shown as a reversed-color block and identified as `Digit 1/3`, `Digit 2/3`, or `Digit 3/3`.
- Press Modality to cycle through `M`, `OV`, `SY`, and `Y`.
- Hold SEND/SELECT for at least 800 ms to start sending the selected file. The Pico types one code, then waits for you to check the host before continuing.
- The OLED shows the code being typed and its CSV description instead of a `SEND next` prompt. After checking the host (and clearing a rejected entry yourself if needed), briefly press SEND/SELECT to send the next code. Descriptions longer than the display line are shortened with an ellipsis.
- After the last code, briefly press SEND/SELECT to return to asset selection. While waiting between codes, hold SEND/SELECT for 3 seconds to cancel the transfer.

The Pico cannot determine whether the host application accepted a code; use the code shown on the OLED to identify an entry that needs follow-up.

## Update files on the microSD card

1. Disconnect USB power.
2. Hold SEND/SELECT (GP22) while reconnecting the Pico over USB.
3. The Pico starts in SD update mode and exposes the card as a USB drive. Edit or copy CSV files on the computer.
4. Safely eject the drive from the computer before rebooting.
5. Safely eject the USB drive and wait for the OLED to show `Drive ejected`. Release SEND/SELECT, then hold it for 3 seconds. The Pico reboots into normal keyboard mode. If the OLED shows `Eject drive first`, eject the drive from the computer and try again.

Do not edit the card from the computer while the Pico is running in normal mode. The firmware accesses the same card directly in that mode.

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

In normal firmware mode, the Pico enumerates as a USB HID keyboard named `Pico Keyboard`; in SD update mode, it enumerates as a USB mass-storage device. Neither mode exposes a serial/COM port. `RPI-RP2` is only available in BOOTSEL mode. Before sending codes, select the intended application and input field on the host.

See [HARDWARE_DESIGN.md](./HARDWARE_DESIGN.md) for the carrier-board and enclosure design notes.
