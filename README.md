# FUJIFILM MR Option Loader

RP2040 firmware that reads option codes from a microSD card and types them into a USB host as a HID keyboard. It targets the Adafruit Feather RP2040 Adalogger stacked with the 128x64 SH1107 OLED FeatherWing.

## Hardware

The firmware targets the Adafruit Feather RP2040 Adalogger and OLED FeatherWing. The FeatherWing uses the Feather I2C pins, and the Adalogger provides the onboard microSD slot.

### OLED

The 128x64 SH1107 display uses I2C1 at 100 kHz on the Feather's SDA/SCL pins. The firmware probes addresses `0x3C` and `0x3D`. The SH1107's controller memory is rotated relative to the panel, so the firmware maps the 128x64 drawing coordinates into the controller's 64-column, 16-page layout before updating the display.

| FeatherWing | Feather RP2040 Adalogger |
| --- | --- |
| SDA | GP2 |
| SCL | GP3 |
| VCC | 3V |
| GND | GND |

### microSD

The onboard microSD slot is used in SPI mode.

| Adalogger SD signal | GPIO |
| --- | --- |
| SCK | GP18 |
| MOSI | GP19 |
| MISO | GP20 |
| CS | GP23 |

### Buttons

The OLED FeatherWing buttons are active-low and use the Feather's internal pull-ups.

| FeatherWing button | Function | GPIO |
| --- | --- | --- |
| A | Modality | GP9 |
| B | Next digit | GP6 |
| C | Send/select | GP5 |

## microSD files

Format the card as FAT32. Put asset files in the card's root directory, using an uppercase `.CSV` extension and the asset name shown on the display. For example, the default selection `SY629` loads `SY629.CSV`.

Each file must have a header row followed by description/code rows. The firmware skips the header and types the second column, one code per line. Comma-, semicolon-, and tab-separated files are supported, including quoted fields. It loads up to 50 codes per file, with each code limited to 16 characters.

```csv
Description,Code
Example option,ABC123
```

If the OLED reports `No valid codes`, check that the file has a header, has at least two columns, and that the code column contains non-empty values no longer than 16 characters.

## Controls

- On startup, the OLED shows the number of CSV files found. Press and release C to enter asset selection.
- Press B to increment the currently selected digit. Digits wrap from 9 to 0.
- Press C briefly to move the digit selection.
- Button inputs are debounced so one press advances once.
- The asset name is displayed at double size. The selected digit is shown as a reversed-color block and identified as `Digit 1/3`, `Digit 2/3`, or `Digit 3/3`.
- Press A to cycle through modalities `M`, `OV`, `SY`, and `Y`.
- Hold C for at least 800 ms to start sending the selected file. The Feather types one code, then waits for you to check the host before continuing.
- The OLED shows the code being typed and its CSV description. After checking the host (and clearing a rejected entry yourself if needed), briefly press C to send the next code. Descriptions longer than the display line are shortened with an ellipsis.
- After the last code, briefly press C to return to asset selection. While waiting between codes, hold C for 3 seconds to cancel the transfer.

The firmware cannot determine whether the host application accepted a code; use the code shown on the OLED to identify an entry that needs follow-up.

## Update files on the microSD card

1. Disconnect USB power.
2. Hold C (GP5) while reconnecting the Feather over USB.
3. The Feather starts in SD update mode and exposes the card as a USB drive. Edit or copy CSV files on the computer.
4. Safely eject the drive from the computer before rebooting.
5. Safely eject the USB drive and wait for the OLED to show `Drive ejected`. Release C, then hold it for 3 seconds. The Feather reboots into normal keyboard mode. If the OLED shows `Eject drive first`, eject the drive from the computer and try again.

Do not edit the card from the computer while the Feather is running in normal mode. The firmware accesses the same card directly in that mode.

The Feather's onboard LED stays on when the OLED does not respond at either supported I2C address. It turns off when a display responds. The firmware retries OLED detection once per second.

## Build

Install CMake, Ninja, the Arm GNU toolchain, and the Raspberry Pi Pico SDK. This project was developed with Pico SDK 2.3.1.

Set `PICO_SDK_PATH` to the SDK directory, then configure and build:

```sh
cmake --preset default -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/feather-adalogger
```

The UF2 file is generated at `build/feather-adalogger/FUJIFILM_MR_Option_Loader.uf2`. The default CMake preset targets `adafruit_feather_rp2040_adalogger`.

## Flash

1. Hold BOOT while connecting the Feather over USB.
2. Copy `build/feather-adalogger/FUJIFILM_MR_Option_Loader.uf2` to the `RPI-RP2` drive.
3. Reconnect the Feather normally.

In normal firmware mode, the Feather enumerates as a USB HID keyboard named `Pico Keyboard`; in SD update mode, it enumerates as a USB mass-storage device. Neither mode exposes a serial/COM port. `RPI-RP2` is only available in BOOT mode. Before sending codes, select the intended application and input field on the host.

See [HARDWARE_DESIGN.md](./HARDWARE_DESIGN.md) for the hardware pin map and enclosure notes.
