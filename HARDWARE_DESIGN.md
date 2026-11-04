# FUJIFILM MR Option Loader Hardware

## Current hardware

The firmware targets an Adafruit Feather RP2040 Adalogger with an Adafruit 128x64 SH1107 OLED FeatherWing. The boards stack on the standard Feather headers; the Adalogger provides the RP2040, USB connection, and microSD slot, while the FeatherWing provides the display and A/B/C controls.

| Component | Connection or role |
| --- | --- |
| Adafruit Feather RP2040 Adalogger | RP2040 controller, USB, microSD, status LED |
| Adafruit OLED FeatherWing (SH1107) | 128x64 I2C OLED and three buttons |

## Firmware pin map

| Function | Adalogger GPIO | Notes |
| --- | ---: | --- |
| OLED SDA | GP2 | I2C1 |
| OLED SCL | GP3 | I2C1 |
| Button A | GP9 | Modality, active low |
| Button B | GP6 | Next digit, active low |
| Button C | GP5 | Send/select, active low |
| SD SCK | GP18 | Onboard microSD, SPI0 |
| SD MOSI | GP19 | Onboard microSD, SPI0 |
| SD MISO | GP20 | Onboard microSD, SPI0 |
| SD chip select | GP23 | Onboard microSD |
| Status LED | GP13 | Adalogger onboard LED |

The buttons use the RP2040's internal pull-ups. The display's default I2C address is 0x3C; the firmware also checks 0x3D. The SD card detect pin is not required by the firmware.

## Enclosure notes

- Keep the Feather boards stacked without pressure on the OLED glass or its flex area.
- Provide access to the Feather's USB-C connector and the Adalogger's microSD slot.
- Provide front-panel access to the OLED FeatherWing's three controls and preserve their A/B/C labels.
- Measure the actual stacked boards, headers, OLED window, and button height before designing or printing an enclosure.
- Preserve access to the Adalogger BOOT button for firmware recovery.

## Bring-up

Verify the OLED responds at 0x3C and that all three buttons pull their respective GPIOs low when pressed. Confirm the microSD card is FAT32 formatted and that the Adalogger's onboard SPI slot can read the card. SD failures are reflected on the OLED; an unresponsive OLED leaves the onboard status LED on.
