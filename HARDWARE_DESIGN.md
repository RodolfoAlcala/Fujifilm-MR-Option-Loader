# FUJIFILM MR Option Loader Hardware Design

## Revision 1 direction

Build a two-layer carrier PCB for the original Raspberry Pi Pico. The Pico, Adafruit OLED, and Adafruit microSD breakout are permanently soldered to the carrier PCB through plated through-holes. The enclosure is a handheld two-piece printed shell with the OLED and three controls on the front, the microSD slot accessible from one side, and the Pico micro-USB connector accessible from the opposite end.

Use KiCad for the schematic and PCB layout. Keep the firmware pin assignments unchanged for this revision.

## Components

| Item | Selected part or requirement |
| --- | --- |
| MCU | Original Raspberry Pi Pico, RP2040, soldered directly to the carrier |
| Display | Adafruit product 4440, 0.91-inch 128x32 I2C OLED, 20 x 35 mm PCB, about 4 mm thick |
| Storage | Adafruit product 4682, Micro SD SPI or SDIO breakout, 25.4 x 22.8 mm PCB, 3V only |
| Controls | Three normally-open momentary pushbuttons |
| Contrast | 10 kOhm potentiometer, 3.3 V compatible, panel accessible |
| Power/data | Pico micro-USB connector for revision 1 |
| PCB | Two-layer FR-4, nominal 1.6 mm thickness, lead-free assembly compatible |
| Enclosure | Two-piece handheld case, printed in a material selected after printer choice |

The USB-C option is deferred. Adding a separate USB-C connector would require a dedicated USB data/power connection to the Pico and careful handling of the Pico's existing micro-USB connector. Exposing the original Pico connector is simpler and preserves BOOTSEL access.

## Firmware pin map

| Function | Pico GPIO | Carrier net |
| --- | ---: | --- |
| OLED SDA | GP4 | I2C0_SDA |
| OLED SCL | GP5 | I2C0_SCL |
| Next-character button | GP13 | BTN_NEXT, active low |
| Modality button | GP14 | BTN_MODALITY, active low |
| Send/select button | GP15 | BTN_SEND, active low |
| SD DAT0 / MISO | GP16 | SD_MISO |
| SD chip select | GP17 | SD_CS |
| SD clock | GP18 | SD_SCK |
| SD CMD / MOSI | GP19 | SD_MOSI |
| OLED contrast wiper | GP26 / ADC0 | CONTRAST_WIPER |
| 3.3 V | Pico 3V3(OUT) | +3V3 |
| Ground | Pico GND | GND |

The SD breakout is used in SPI mode. DAT1, DAT2, and DAT3 remain unconnected. The OLED is I2C at address 0x3C for product 4440.

## Initial board envelope

Use an initial carrier outline of approximately 90 x 55 mm. This is a starting envelope, not a fabrication release. Place the OLED near the front face, with its 25 x 7 mm active display area centered behind the case window. Place the three buttons below the display in a horizontal row. Place the potentiometer beside or below the buttons. Place the SD breakout near a side wall so the card can be inserted without opening the case. Place the Pico lengthwise with its micro-USB connector facing an end wall.

Reserve at least:

- 1.0 mm PCB edge clearance for copper and footprints
- 2.0 mm clearance around the OLED display opening
- 1.5 mm clearance around the SD card and card insertion path
- 3.0 mm clearance from the case wall for button and potentiometer hardware
- 3.0 mm minimum screw-post or standoff diameter around mounting holes
- 0.25 mm minimum enclosure clearance around fixed PCB features before printer-specific tuning

The final outline must be based on the actual Pico and breakout board hole locations, not only the product-level board dimensions. Measure the physical boards before releasing the PCB or enclosure.

## Schematic requirements

- Route +3V3 and GND to the Pico and both breakouts.
- Use the Pico's internal pull-ups for all three buttons; each button connects its GPIO directly to GND when pressed.
- Connect the potentiometer ends to +3V3 and GND, with the wiper to GP26.
- Add test pads for +3V3, GND, I2C_SDA, I2C_SCL, SD_CS, SD_SCK, SD_MOSI, and SD_MISO.
- Add clearly labeled silkscreen for all buttons, the potentiometer, the display orientation, and the SD connector.
- Do not add a 5 V rail or level shifter. Product 4682 is a 3 V-only breakout.

## Enclosure requirements

- Two-piece shell with captive or heat-set-insert fasteners.
- Front window sized from the OLED active area, not the outside PCB size.
- Openings for the Pico micro-USB connector, microSD card, and any needed BOOTSEL access.
- The enclosure should not press directly on the OLED glass or flex area.
- Provide a small internal strain-relief feature for the USB cable path.
- Add a recessed label area for button functions and the FUJIFILM MR Option Loader name.

## Before PCB release

1. Confirm the exact Pico mechanical orientation and whether the board will be flush-mounted or raised on spacers.
2. Confirm the physical button type and actuator height.
3. Confirm the potentiometer shaft diameter, body height, and shaft style.
4. Measure the actual OLED and SD breakout hole centers and pin spacing.
5. Decide whether the SD card exits from the left or right side of the case.
6. Select the printer, nozzle diameter, layer height, and preferred fastener size.
7. Print a thin front-panel fit prototype before ordering the PCB.

## Current electrical risk

The firmware currently assumes the OLED is connected at 0x3C or 0x3D and the SD card is powered from 3.3 V. The selected Adafruit boards match those assumptions, but the carrier PCB must still be continuity-tested and current-limited during first power-up.
