#include "pico/stdlib.h"
#include "tusb.h"
#include "ff.h"
#include "hw_config.h"
#include "diskio.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SEND_BUTTON_PIN 22
#define NEXT_CHARACTER_BUTTON_PIN 28
#define MODALITY_BUTTON_PIN 20
#define STATUS_LED_PIN PICO_DEFAULT_LED_PIN
#define MAX_OPTIONS_PER_ASSET 50
#define MAX_OPTION_LENGTH 16
#define MAX_DESCRIPTION_LENGTH 64
#define MAX_CSV_LINE_LENGTH 256
#define KEYBOARD_REPORT_DELAY_MS 10
#define LCD_I2C i2c0
#define LCD_SDA_PIN 4
#define LCD_SCL_PIN 5
#define OLED_WIDTH 128
#define OLED_PAGES 4
#define SEND_LONG_PRESS_MS 800
#define UPDATE_MODE_EXIT_HOLD_MS 3000
#define UPDATE_MODE_REBOOT_MAGIC 0x53445550
#define SD_SECTOR_SIZE 512
#define SCSI_CMD_SYNCHRONIZE_CACHE_10 0x35
#define OLED_ADDRESS_PRIMARY 0x3c

bool maintenance_mode;

typedef struct {
    bool raw_down;
    bool stable_down;
    uint32_t raw_changed_ms;
} ButtonState;

typedef enum {
    BUTTON_EVENT_NONE,
    BUTTON_EVENT_PRESSED,
    BUTTON_EVENT_RELEASED
} ButtonEvent;

#define BUTTON_DEBOUNCE_MS 30

static char output_lines[MAX_OPTIONS_PER_ASSET][MAX_OPTION_LENGTH + 1];
static char option_descriptions[MAX_OPTIONS_PER_ASSET][MAX_DESCRIPTION_LENGTH + 1];
static size_t output_line_count;
static bool assets_loaded;
static bool selected_file_has_no_codes;
static bool sd_missing;
static bool awaiting_asset_selection;
static size_t asset_file_count;
static bool codes_sending;
static bool codes_sent;
static bool manual_transfer_active;
static size_t current_code_index;
static size_t invalid_csv_row_count;
static bool maintenance_sd_ready;
static bool maintenance_drive_ejected;
static uint32_t maintenance_sector_count;
static uint8_t maintenance_sector_buffer[SD_SECTOR_SIZE];
static ButtonState send_button_state;
static ButtonState next_character_button_state;
static ButtonState modality_button_state;
static const char *modalities[] = {"M", "OV", "SY", "Y"};
static size_t modality_index = 2;
static char selected_asset_name[16] = "SY629";
static size_t selected_digit_index;
static bool lcd_ready;
static uint8_t lcd_address;

// The OLED uses a command/data control byte before each I2C payload.
static void lcd_write_command(uint8_t value) {
    uint8_t packet[] = {0x00, value};
    i2c_write_blocking(LCD_I2C, lcd_address, packet, sizeof(packet), false);
}

static void lcd_write_data(uint8_t value) {
    uint8_t packet[] = {0x40, value};
    i2c_write_blocking(LCD_I2C, lcd_address, packet, sizeof(packet), false);
}

static void lcd_set_cursor(uint8_t x, uint8_t y) {
    lcd_write_command((uint8_t)(0xb0 | y));
    lcd_write_command((uint8_t)(x & 0x0f));
    lcd_write_command((uint8_t)(0x10 | (x >> 4)));
}

static void lcd_clear(void) {
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        lcd_set_cursor(0, page);
        for (size_t i = 0; i < OLED_WIDTH; i++) {
            lcd_write_data(0);
        }
    }
    lcd_set_cursor(0, 0);
}

static const uint8_t *lcd_glyph(char character) {
    static const char characters[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.-/: ";
    static const uint8_t glyphs[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
        {0x00,0x00,0x60,0x00,0x00},{0x00,0x08,0x08,0x08,0x08},
        {0x40,0x20,0x10,0x08,0x04},{0x00,0x14,0x00,0x14,0x00},
        {0x00,0x00,0x00,0x00,0x00}
    };
    char uppercase = character >= 'a' && character <= 'z'
                         ? (char)(character - 'a' + 'A') : character;
    for (size_t i = 0; i < sizeof(characters) - 1; i++) {
        if (characters[i] == uppercase) {
            return glyphs[i];
        }
    }
    return glyphs[sizeof(glyphs) / sizeof(glyphs[0]) - 1];
}

static void lcd_write_text(const char *text) {
    size_t length = strlen(text);
    if (length > OLED_WIDTH / 6) {
        length = OLED_WIDTH / 6;
    }
    for (size_t i = 0; i < length; i++) {
        const uint8_t *glyph = lcd_glyph(text[i]);
        for (size_t column = 0; column < 5; column++) {
            lcd_write_data(glyph[column]);
        }
        lcd_write_data(0);
    }
}

static void lcd_write_centered_text(const char *text, uint8_t page) {
    size_t length = strlen(text);
    if (length > OLED_WIDTH / 6) {
        length = OLED_WIDTH / 6;
    }
    lcd_set_cursor((uint8_t)((OLED_WIDTH - length * 6) / 2), page);
    lcd_write_text(text);
}

static void lcd_write_asset_name(void) {
    size_t length = strlen(selected_asset_name);
    size_t number_start = 0;
    while (selected_asset_name[number_start] != '\0' &&
           (selected_asset_name[number_start] < '0' ||
            selected_asset_name[number_start] > '9')) {
        number_start++;
    }

    uint8_t x = (uint8_t)((OLED_WIDTH - length * 12) / 2);
    for (uint8_t page = 0; page < 2; page++) {
        lcd_set_cursor(x, page);
        for (size_t i = 0; i < length; i++) {
            const uint8_t *glyph = lcd_glyph(selected_asset_name[i]);
            bool selected_digit = !manual_transfer_active &&
                                  i == number_start + selected_digit_index;
            for (size_t column = 0; column < 12; column++) {
                uint8_t pixels = 0;
                if (column < 10) {
                    uint8_t glyph_column = glyph[column / 2];
                    for (uint8_t bit = 0; bit < 8; bit++) {
                        uint8_t source_row =
                            (uint8_t)((page * 8 + bit) / 2);
                        if (source_row < 7 &&
                            (glyph_column & (1u << source_row)) != 0) {
                            pixels |= (uint8_t)(1u << bit);
                        }
                    }
                }
                if (selected_digit) {
                    pixels = (uint8_t)~pixels;
                }
                lcd_write_data(pixels);
            }
        }
    }
}

static void lcd_init(void) {
    i2c_init(LCD_I2C, 400 * 1000);
    gpio_set_function(LCD_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(LCD_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(LCD_SDA_PIN);
    gpio_pull_up(LCD_SCL_PIN);
    lcd_address = OLED_ADDRESS_PRIMARY;
    uint8_t probe = 0;
    // Adafruit OLED boards commonly use either 0x3C or 0x3D.
    if (i2c_write_blocking(LCD_I2C, lcd_address, &probe, 1, false) < 0) {
        lcd_address = 0x3d;
        if (i2c_write_blocking(LCD_I2C, lcd_address, &probe, 1, false) < 0) {
            printf("OLED not found at 0x3C or 0x3D\n");
            gpio_put(STATUS_LED_PIN, 1);
            return;
        }
    }
    lcd_ready = true;
    printf("OLED found at 0x%02X\n", lcd_address);
    sleep_ms(50);
    lcd_write_command(0xae);
    lcd_write_command(0xd5); lcd_write_command(0x80);
    lcd_write_command(0xa8); lcd_write_command(0x1f);
    lcd_write_command(0xd3); lcd_write_command(0x00);
    lcd_write_command(0x40);
    lcd_write_command(0x8d); lcd_write_command(0x14);
    lcd_write_command(0x20); lcd_write_command(0x00);
    lcd_write_command(0xa1);
    lcd_write_command(0xc8);
    lcd_write_command(0xda); lcd_write_command(0x02);
    lcd_write_command(0x81); lcd_write_command(0x7f);
    lcd_write_command(0xd9); lcd_write_command(0xf1);
    lcd_write_command(0xdb); lcd_write_command(0x40);
    lcd_write_command(0xa4);
    lcd_write_command(0xa6);
    lcd_write_command(0xaf);
    lcd_clear();
}

static void lcd_show_asset_count(void);
static void lcd_show_asset(void);

static void update_lcd_presence(void) {
    static uint32_t next_check_ms;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now_ms - next_check_ms) < 0) {
        return;
    }
    next_check_ms = now_ms + 1000;

    // Probe once per second so a disconnected display does not stall the main loop.
    if (lcd_ready) {
        uint8_t probe = 0;
        if (i2c_write_blocking(LCD_I2C, lcd_address, &probe, 1, false) < 0) {
            lcd_ready = false;
            gpio_put(STATUS_LED_PIN, 1);
            printf("OLED disconnected\n");
        }
        return;
    }

    lcd_init();
    if (lcd_ready) {
        gpio_put(STATUS_LED_PIN, 0);
        if (awaiting_asset_selection) {
            lcd_show_asset_count();
        } else {
            lcd_show_asset();
        }
        printf("OLED reconnected\n");
    }
}

static void lcd_show_message(const char *message) {
    if (!lcd_ready) {
        return;
    }
    lcd_clear();
    lcd_write_centered_text(message, 1);
}

static void lcd_show_asset_count(void) {
    if (!lcd_ready) {
        return;
    }
    lcd_clear();
    char count_message[17];
    snprintf(count_message, sizeof(count_message), "Assets: %u",
             (unsigned)asset_file_count);
    lcd_write_centered_text("FUJIFILM Healthcare", 0);
    lcd_write_centered_text("MR Option Loader", 1);
    lcd_write_centered_text(count_message, 2);
    lcd_write_centered_text("Press button", 3);
}

static void lcd_show_update_mode(bool drive_ejected, bool eject_required) {
    if (!lcd_ready) {
        return;
    }
    lcd_clear();
    lcd_write_centered_text("SD UPDATE MODE", 0);
    if (!maintenance_sd_ready) {
        lcd_write_centered_text("SD card unavailable", 1);
        lcd_write_centered_text("Check card and reset", 3);
        return;
    }
    lcd_write_centered_text("Edit CSV on computer", 1);
    const char *drive_status = eject_required
                                   ? "Eject drive first"
                                   : drive_ejected ? "Drive ejected"
                                                   : "Safely eject drive";
    lcd_write_centered_text(drive_status, 2);
    lcd_write_centered_text("Hold SEND 3 seconds", 3);
}

static void format_description(char *output, size_t output_size,
                               const char *description) {
    size_t length = strlen(description);
    size_t max_length = output_size - 1;
    if (length <= max_length) {
        memcpy(output, description, length + 1);
        return;
    }

    size_t visible_length = max_length > 3 ? max_length - 3 : max_length;
    memcpy(output, description, visible_length);
    if (max_length > 3) {
        memcpy(output + visible_length, "...", 3);
    }
    output[max_length] = '\0';
}

static void lcd_show_asset(void) {
    if (!lcd_ready) {
        return;
    }
    lcd_clear();
    lcd_write_asset_name();
    char selection[22];
    char status[22];
    if (manual_transfer_active && current_code_index < output_line_count) {
        snprintf(selection, sizeof(selection), "%s",
                 output_lines[current_code_index]);
        const char *description = option_descriptions[current_code_index];
        format_description(status, sizeof(status),
                           description[0] == '\0' ? "No description"
                                                  : description);
    } else if (codes_sent) {
        snprintf(selection, sizeof(selection), "All codes sent");
        snprintf(status, sizeof(status), "%u options",
                 (unsigned)output_line_count);
    } else if (codes_sending) {
        snprintf(selection, sizeof(selection), "Sending code...");
        snprintf(status, sizeof(status), "%u options",
                 (unsigned)output_line_count);
    } else {
        size_t number_start = 0;
        while (selected_asset_name[number_start] != '\0' &&
               (selected_asset_name[number_start] < '0' ||
                selected_asset_name[number_start] > '9')) {
            number_start++;
        }
        snprintf(selection, sizeof(selection), "Digit %u/3: %c",
                 (unsigned)(selected_digit_index + 1),
                 selected_asset_name[number_start + selected_digit_index]);
        if (sd_missing) {
            snprintf(status, sizeof(status), "SD not inserted");
        } else if (assets_loaded) {
            snprintf(status, sizeof(status), "%u options",
                     (unsigned)output_line_count);
        } else if (selected_file_has_no_codes) {
            snprintf(status, sizeof(status), "No valid codes");
        } else {
            snprintf(status, sizeof(status), "No such file");
        }
    }
    size_t status_length = strlen(status);
    if (status_length > OLED_WIDTH / 6) {
        status_length = OLED_WIDTH / 6;
    }
    lcd_set_cursor((uint8_t)((OLED_WIDTH - status_length * 6) / 2), 3);
    lcd_write_text(status);
}

typedef enum {
    KEYBOARD_IDLE,
    KEYBOARD_PRESS,
    KEYBOARD_RELEASE
} KeyboardState;

typedef struct {
    KeyboardState state;
    const char *text;
    size_t character_index;
    bool sending_newline;
    uint32_t next_action_ms;
} KeyboardTask;

static KeyboardTask keyboard_task_state = {0};

static bool load_assets_from_sd(const char *asset_name);

static sd_card_t *maintenance_card(void) {
    return maintenance_sd_ready ? sd_get_by_num(0) : NULL;
}

static bool initialize_maintenance_card(void) {
    sd_card_t *card = sd_get_by_num(0);
    if (card == NULL || !sd_init_driver() || card->init == NULL) {
        return false;
    }

    int status = card->init(card);
    if ((status & (STA_NOINIT | STA_NODISK)) != 0 || card->sectors == 0 ||
        card->read_blocks == NULL || card->write_blocks == NULL) {
        return false;
    }

    maintenance_sector_count = card->sectors > UINT32_MAX
                                   ? UINT32_MAX
                                   : (uint32_t)card->sectors;
    return maintenance_sector_count != 0;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memset(vendor_id, ' ', 8);
    memset(product_id, ' ', 16);
    memset(product_rev, ' ', 4);
    memcpy(vendor_id, "MRLoader", 8);
    memcpy(product_id, "SD Card", 7);
    memcpy(product_rev, "1.0", 3);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    return lun == 0 && maintenance_sd_ready && !maintenance_drive_ejected;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                         uint16_t *block_size) {
    *block_count = lun == 0 ? maintenance_sector_count : 0;
    *block_size = lun == 0 ? SD_SECTOR_SIZE : 0;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
    sd_card_t *card = maintenance_card();
    if (lun != 0 || card == NULL || card->read_blocks == NULL ||
        lba >= maintenance_sector_count ||
        offset > SD_SECTOR_SIZE || bufsize > SD_SECTOR_SIZE - offset ||
        card->read_blocks(card, maintenance_sector_buffer, lba, 1) != 0) {
        return -1;
    }

    memcpy(buffer, maintenance_sector_buffer + offset, bufsize);
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize) {
    sd_card_t *card = maintenance_card();
    if (lun != 0 || card == NULL || card->read_blocks == NULL ||
        card->write_blocks == NULL || lba >= maintenance_sector_count ||
        offset > SD_SECTOR_SIZE || bufsize > SD_SECTOR_SIZE - offset ||
        card->read_blocks(card, maintenance_sector_buffer, lba, 1) != 0) {
        return -1;
    }

    memcpy(maintenance_sector_buffer + offset, buffer, bufsize);
    if (card->write_blocks(card, maintenance_sector_buffer, lba, 1) != 0) {
        return -1;
    }

    maintenance_drive_ejected = false;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void *buffer, uint16_t bufsize) {
    (void)buffer;
    (void)bufsize;
    if (lun == 0 && scsi_cmd[0] == SCSI_CMD_SYNCHRONIZE_CACHE_10) {
        return 0;
    }

    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                           bool start, bool load_eject) {
    (void)power_condition;
    if (lun != 0) {
        return false;
    }
    if (load_eject) {
        maintenance_drive_ejected = !start;
    }
    return true;
}

static void initialize_button(uint pin, ButtonState *state) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    state->raw_down = !gpio_get(pin);
    state->stable_down = state->raw_down;
    state->raw_changed_ms = to_ms_since_boot(get_absolute_time());
}

static ButtonEvent update_button(uint pin, ButtonState *state,
                                 uint32_t now_ms) {
    bool raw_down = !gpio_get(pin);
    if (raw_down != state->raw_down) {
        state->raw_down = raw_down;
        state->raw_changed_ms = now_ms;
    }

    if (state->stable_down != state->raw_down &&
        now_ms - state->raw_changed_ms >= BUTTON_DEBOUNCE_MS) {
        state->stable_down = state->raw_down;
        return state->stable_down ? BUTTON_EVENT_PRESSED
                                  : BUTTON_EVENT_RELEASED;
    }
    return BUTTON_EVENT_NONE;
}

static void run_sd_update_mode(void) {
    maintenance_sd_ready = initialize_maintenance_card();
    gpio_put(STATUS_LED_PIN, !maintenance_sd_ready || !lcd_ready);
    lcd_show_update_mode(false, false);

    bool button_released = !send_button_state.stable_down;
    bool tracking_hold = false;
    bool exit_attempted = false;
    bool exit_blocked = false;
    bool displayed_ejected = maintenance_drive_ejected;
    uint32_t hold_started_ms = 0;

    while (true) {
        tud_task();
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        ButtonEvent send_event =
            update_button(SEND_BUTTON_PIN, &send_button_state, now_ms);
        bool button_down = send_button_state.stable_down;

        if (send_event == BUTTON_EVENT_RELEASED) {
            button_released = true;
            tracking_hold = false;
            exit_attempted = false;
            if (exit_blocked) {
                exit_blocked = false;
                lcd_show_update_mode(maintenance_drive_ejected, false);
            }
        } else if (send_event == BUTTON_EVENT_PRESSED && button_released) {
            tracking_hold = true;
            hold_started_ms = now_ms;
        }

        if (button_down && tracking_hold && !exit_attempted &&
            now_ms - hold_started_ms >= UPDATE_MODE_EXIT_HOLD_MS) {
                exit_attempted = true;
                tracking_hold = false;
                if (!maintenance_sd_ready || maintenance_drive_ejected) {
                    watchdog_hw->scratch[0] = UPDATE_MODE_REBOOT_MAGIC;
                    watchdog_reboot(0, 0, 0);
                    while (true) {
                        tight_loop_contents();
                    }
                }
                exit_blocked = true;
                lcd_show_update_mode(maintenance_drive_ejected, true);
        }

        if (displayed_ejected != maintenance_drive_ejected) {
            displayed_ejected = maintenance_drive_ejected;
            if (!exit_blocked) {
                lcd_show_update_mode(displayed_ejected, false);
            }
        }
    }
}

static bool is_csv_file(const char *filename) {
    size_t length = strlen(filename);
    if (length < 4 || filename[length - 4] != '.') {
        return false;
    }
    return (filename[length - 3] == 'C' || filename[length - 3] == 'c') &&
           (filename[length - 2] == 'S' || filename[length - 2] == 's') &&
           (filename[length - 1] == 'V' || filename[length - 1] == 'v');
}

static size_t count_asset_files_on_sd(void) {
    sd_card_t *card = sd_get_by_num(0);
    if (card == NULL) {
        sd_missing = true;
        printf("SD configuration missing\n");
        return 0;
    }

    FRESULT mount_result = f_mount(&card->fatfs, card->pcName, 1);
    if (mount_result != FR_OK) {
        sd_missing = true;
        printf("SD mount failed (%d)\n", mount_result);
        return 0;
    }

    DIR directory;
    FILINFO file_info;
    size_t count = 0;
    FRESULT open_result = f_opendir(&directory, "0:/");
    // Only root-level CSV files represent selectable assets.
    if (open_result == FR_OK) {
        while (f_readdir(&directory, &file_info) == FR_OK &&
               file_info.fname[0] != '\0') {
            if ((file_info.fattrib & AM_DIR) == 0 &&
                is_csv_file(file_info.fname)) {
                count++;
            }
        }
        f_closedir(&directory);
    } else {
        printf("SD directory open failed (%d)\n", open_result);
    }
    f_unmount(card->pcName);
    sd_missing = open_result != FR_OK;
    printf("Found %u asset files\n", (unsigned)count);
    return count;
}

static void update_sd_presence(void) {
    static uint32_t next_check_ms;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    // Avoid mounting the card repeatedly while the user is still on the startup screen
    // or while a keyboard transfer is in progress.
    if (awaiting_asset_selection ||
        (int32_t)(now_ms - next_check_ms) < 0 ||
        keyboard_task_state.state != KEYBOARD_IDLE) {
        return;
    }
    next_check_ms = now_ms + 1000;

    sd_card_t *card = sd_get_by_num(0);
    if (card == NULL) {
        sd_missing = true;
        assets_loaded = false;
        lcd_show_asset();
        return;
    }

    FRESULT mount_result = f_mount(&card->fatfs, card->pcName, 1);
    if (mount_result != FR_OK) {
        if (!sd_missing) {
            sd_missing = true;
            assets_loaded = false;
            lcd_show_asset();
        }
        return;
    }

    if (sd_missing) {
        assets_loaded = load_assets_from_sd(selected_asset_name);
        lcd_show_asset();
    }
}

typedef struct {
    uint8_t toggles_remaining;
    uint32_t next_toggle_ms;
} LedBlinkTask;

static LedBlinkTask led_blink_task = {0};

static void start_sd_error_blink(void) {
    gpio_put(STATUS_LED_PIN, 1);
    led_blink_task.toggles_remaining = 7;
    led_blink_task.next_toggle_ms =
        to_ms_since_boot(get_absolute_time()) + 100;
}

static void led_blink_task_update(void) {
    if (led_blink_task.toggles_remaining == 0) {
        return;
    }

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now_ms - led_blink_task.next_toggle_ms) < 0) {
        return;
    }

    led_blink_task.toggles_remaining--;
    if (led_blink_task.toggles_remaining == 0) {
        gpio_put(STATUS_LED_PIN, !lcd_ready);
        return;
    }

    gpio_put(STATUS_LED_PIN, !gpio_get(STATUS_LED_PIN));
    led_blink_task.next_toggle_ms = now_ms + 100;
}

static bool is_csv_space(char character) {
    return character == ' ' || character == '\t';
}

static char detect_csv_delimiter(const char *header) {
    const char delimiters[] = {',', ';', '\t'};
    size_t counts[sizeof(delimiters)] = {0};
    bool quoted = false;

    for (size_t i = 0; header[i] != '\0'; i++) {
        if (header[i] == '"') {
            if (quoted && header[i + 1] == '"') {
                i++;
            } else {
                quoted = !quoted;
            }
        } else if (!quoted) {
            for (size_t delimiter = 0; delimiter < sizeof(delimiters); delimiter++) {
                if (header[i] == delimiters[delimiter]) {
                    counts[delimiter]++;
                    break;
                }
            }
        }
    }

    size_t selected = 0;
    for (size_t i = 1; i < sizeof(delimiters); i++) {
        if (counts[i] > counts[selected]) {
            selected = i;
        }
    }
    return delimiters[selected];
}

static bool extract_csv_field(const char **cursor, char delimiter,
                              char *field, size_t field_size,
                              bool allow_empty, bool truncate) {
    while (is_csv_space(**cursor)) {
        (*cursor)++;
    }

    bool quoted = **cursor == '"';
    bool closed = false;
    if (quoted) {
        (*cursor)++;
    }

    size_t length = 0;
    bool too_long = false;
    while (**cursor != '\0') {
        char character = *(*cursor)++;
        if (quoted && character == '"') {
            if (**cursor == '"') {
                (*cursor)++;
                character = '"';
            } else {
                closed = true;
                break;
            }
        } else if (!quoted && character == delimiter) {
            break;
        }

        if (length < field_size - 1) {
            field[length++] = character;
        } else {
            too_long = true;
        }
    }

    if (quoted && !closed) {
        return false;
    }

    if (quoted) {
        while (is_csv_space(**cursor)) {
            (*cursor)++;
        }
        if (**cursor != '\0' && **cursor != delimiter) {
            return false;
        }
    }
    if (**cursor == delimiter) {
        (*cursor)++;
    }

    while (length > 0 && is_csv_space(field[length - 1])) {
        length--;
    }
    field[length] = '\0';
    return (truncate || !too_long) && (allow_empty || length > 0);
}

static bool extract_csv_fields(const char *line, char delimiter,
                               char *description, size_t description_size,
                               char *code, size_t code_size) {
    const char *cursor = line;
    return extract_csv_field(&cursor, delimiter, description,
                             description_size, true, true) &&
           extract_csv_field(&cursor, delimiter, code, code_size, false, false);
}

static bool load_assets_from_sd(const char *asset_name) {
    selected_file_has_no_codes = false;
    sd_missing = false;
    invalid_csv_row_count = 0;
    sd_card_t *card = sd_get_by_num(0);
    if (card == NULL) {
        sd_missing = true;
        printf("SD configuration missing\n");
        return false;
    }

    FRESULT mount_result = f_mount(&card->fatfs, card->pcName, 1);
    if (mount_result != FR_OK) {
        sd_missing = true;
        printf("SD mount failed (%d)\n", mount_result);
        return false;
    }

    FIL file;
    char filename[32];
    snprintf(filename, sizeof(filename), "0:/%s.CSV", asset_name);
    FRESULT open_result = f_open(&file, filename, FA_READ);
    if (open_result != FR_OK) {
        f_unmount(card->pcName);
        printf("%s open failed (%d)\n", filename, open_result);
        return false;
    }

    output_line_count = 0;
    char line[MAX_CSV_LINE_LENGTH];
    bool header_skipped = false;
    char delimiter = ',';
    // The header selects a delimiter; only the second field of each row is typed.
    while (output_line_count < MAX_OPTIONS_PER_ASSET &&
           f_gets(line, sizeof(line), &file) != NULL) {
        bool line_truncated = strchr(line, '\n') == NULL && !f_eof(&file);
        bool header_row = !header_skipped;
        if (header_row) {
            if (strlen(line) >= 3 &&
                (uint8_t)line[0] == 0xef &&
                (uint8_t)line[1] == 0xbb &&
                (uint8_t)line[2] == 0xbf) {
                memmove(line, line + 3, strlen(line + 3) + 1);
            }
            delimiter = detect_csv_delimiter(line);
            header_skipped = true;
        } else if (!line_truncated) {
            char description[MAX_DESCRIPTION_LENGTH + 1];
            char code[MAX_OPTION_LENGTH + 1];
            line[strcspn(line, "\r\n")] = '\0';
            if (extract_csv_fields(line, delimiter, description,
                                   sizeof(description), code, sizeof(code))) {
                memcpy(option_descriptions[output_line_count], description,
                       strlen(description) + 1);
                memcpy(output_lines[output_line_count], code, strlen(code) + 1);
                output_line_count++;
            } else {
                invalid_csv_row_count++;
            }
        }

        if (line_truncated) {
            do {
                if (f_gets(line, sizeof(line), &file) == NULL ||
                    strchr(line, '\n') != NULL) {
                    break;
                }
            } while (!f_eof(&file));
            if (!header_row) {
                invalid_csv_row_count++;
            }
        }
    }
    f_close(&file);
    f_unmount(card->pcName);

    if (output_line_count == 0) {
        selected_file_has_no_codes = true;
        printf("%s has no valid option codes (%u invalid rows)\n", filename,
               (unsigned)invalid_csv_row_count);
        return false;
    }
    printf("Loaded %u codes from %s; skipped %u invalid rows\n",
           (unsigned)output_line_count, filename,
           (unsigned)invalid_csv_row_count);
    return true;
}

static void select_modality(int direction) {
    size_t count = sizeof(modalities) / sizeof(modalities[0]);
    modality_index = (modality_index + count + direction) % count;

    char number[sizeof(selected_asset_name)];
    const char *name_number = selected_asset_name;
    while (*name_number != '\0' &&
           (*name_number < '0' || *name_number > '9')) {
        name_number++;
    }
    strncpy(number, name_number, sizeof(number) - 1);
    number[sizeof(number) - 1] = '\0';
    snprintf(selected_asset_name, sizeof(selected_asset_name), "%s%s",
             modalities[modality_index], number);
    assets_loaded = load_assets_from_sd(selected_asset_name);
    selected_digit_index = 0;
    codes_sending = false;
    codes_sent = false;
    lcd_show_asset();
}

static void increment_selected_digit(void) {
    size_t number_start = 0;
    while (selected_asset_name[number_start] != '\0' &&
           (selected_asset_name[number_start] < '0' ||
            selected_asset_name[number_start] > '9')) {
        number_start++;
    }

    int digit = selected_asset_name[number_start + selected_digit_index] - '0';
    digit = (digit + 1) % 10;
    selected_asset_name[number_start + selected_digit_index] = (char)('0' + digit);
    assets_loaded = load_assets_from_sd(selected_asset_name);
    codes_sending = false;
    codes_sent = false;
    lcd_show_asset();
}

static bool character_to_key(char character, uint8_t *keycode, uint8_t *modifier) {
    *keycode = 0;
    *modifier = 0;

    if (character >= 'a' && character <= 'z') {
        *keycode = HID_KEY_A + (character - 'a');
    } else if (character >= 'A' && character <= 'Z') {
        *keycode = HID_KEY_A + (character - 'A');
        *modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    } else if (character == ' ') {
        *keycode = HID_KEY_SPACE;
    } else if (character >= '1' && character <= '9') {
        *keycode = HID_KEY_1 + (character - '1');
    } else if (character == '0') {
        *keycode = HID_KEY_0;
    } else if (character == '-') {
        *keycode = HID_KEY_MINUS;
    } else if (character == '!') {
        *keycode = HID_KEY_1;
        *modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    } else if (character == '\n') {
        *keycode = HID_KEY_ENTER;
    } else {
        return false;
    }

    return true;
}

static void start_current_code(void) {
    keyboard_task_state.text = output_lines[current_code_index];
    keyboard_task_state.character_index = 0;
    keyboard_task_state.sending_newline = false;
    keyboard_task_state.state = KEYBOARD_PRESS;
    keyboard_task_state.next_action_ms = 0;
    codes_sending = true;
    codes_sent = false;
    lcd_show_asset();
}

static void start_selected_asset(void) {
    current_code_index = 0;
    manual_transfer_active = true;
    start_current_code();
}

static void keyboard_task(void) {
    KeyboardTask *task = &keyboard_task_state;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if (task->state == KEYBOARD_IDLE || !tud_mounted() ||
        (int32_t)(now_ms - task->next_action_ms) < 0) {
        return;
    }

    // Send one HID press and one release per interval so the host can consume
    // every character reliably without blocking the rest of the firmware.
    if (task->state == KEYBOARD_PRESS) {
        if (task->text[task->character_index] == '\0') {
            if (task->sending_newline) {
                task->state = KEYBOARD_IDLE;
                codes_sending = false;
                codes_sent = current_code_index + 1 >= output_line_count;
                lcd_show_asset();
                return;
            } else {
                task->text = "\n";
                task->sending_newline = true;
            }
            task->character_index = 0;
        }

        uint8_t keycode[6] = {0};
        uint8_t modifier;
        if (!character_to_key(task->text[task->character_index], &keycode[0], &modifier)) {
            task->character_index++;
            return;
        }
        if (!tud_hid_ready()) {
            return;
        }

        tud_hid_keyboard_report(0, modifier, keycode);
        task->state = KEYBOARD_RELEASE;
        task->next_action_ms = now_ms + KEYBOARD_REPORT_DELAY_MS;
        return;
    }

    if (tud_hid_ready()) {
        tud_hid_keyboard_report(0, 0, NULL);
        task->character_index++;
        task->state = KEYBOARD_PRESS;
        task->next_action_ms = now_ms + KEYBOARD_REPORT_DELAY_MS;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

int main(void) {
    stdio_init_all();
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_PIN, 0);

    initialize_button(SEND_BUTTON_PIN, &send_button_state);
    initialize_button(NEXT_CHARACTER_BUTTON_PIN, &next_character_button_state);
    initialize_button(MODALITY_BUTTON_PIN, &modality_button_state);
    sleep_ms(50);
    bool skip_update_mode = watchdog_hw->scratch[0] == UPDATE_MODE_REBOOT_MAGIC;
    watchdog_hw->scratch[0] = 0;
    maintenance_mode = !skip_update_mode && !gpio_get(SEND_BUTTON_PIN);
    tusb_init();

    printf("Pico starting\n");
    lcd_init();
    if (maintenance_mode) {
        run_sd_update_mode();
    }

    lcd_show_message("Initializing...");
    sleep_ms(500);
    lcd_show_message("Loading SD...");
    printf("SD initializing\n");
    asset_file_count = count_asset_files_on_sd();
    awaiting_asset_selection = true;
    printf("Startup complete\n");
    lcd_show_asset_count();

    gpio_put(STATUS_LED_PIN, 0);

    uint32_t send_pressed_ms = 0;
    bool send_long_press_handled = false;

    while (true) {
        // Keep every service routine short and non-blocking so USB, storage, and
        // display updates can continue while the user interacts with the buttons.
        tud_task();
        keyboard_task();
        update_lcd_presence();
        update_sd_presence();
        led_blink_task_update();

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        ButtonEvent send_event =
            update_button(SEND_BUTTON_PIN, &send_button_state, now_ms);
        ButtonEvent next_character_event =
            update_button(NEXT_CHARACTER_BUTTON_PIN,
                          &next_character_button_state, now_ms);
        ButtonEvent modality_event =
            update_button(MODALITY_BUTTON_PIN, &modality_button_state, now_ms);
        bool button_down = send_button_state.stable_down;

        if (awaiting_asset_selection) {
            // The first SEND release leaves the startup screen and loads the default asset.
            if (send_event == BUTTON_EVENT_RELEASED) {
                awaiting_asset_selection = false;
                assets_loaded = load_assets_from_sd(selected_asset_name);
                lcd_show_asset();
            }
            continue;
        }
        if (send_event == BUTTON_EVENT_PRESSED) {
            send_pressed_ms = now_ms;
            send_long_press_handled = false;
        }
        if (keyboard_task_state.state == KEYBOARD_IDLE && manual_transfer_active) {
            if (button_down && !send_long_press_handled &&
                now_ms - send_pressed_ms >= UPDATE_MODE_EXIT_HOLD_MS) {
                manual_transfer_active = false;
                codes_sent = false;
                send_long_press_handled = true;
                lcd_show_asset();
            }
            if (send_event == BUTTON_EVENT_RELEASED &&
                !send_long_press_handled) {
                if (codes_sent) {
                    manual_transfer_active = false;
                    codes_sent = false;
                    lcd_show_asset();
                } else if (current_code_index + 1 < output_line_count) {
                    current_code_index++;
                    start_current_code();
                }
            }
        } else if (keyboard_task_state.state == KEYBOARD_IDLE) {
            if (next_character_event == BUTTON_EVENT_PRESSED) {
                increment_selected_digit();
            }
            if (modality_event == BUTTON_EVENT_PRESSED) {
                select_modality(1);
            }
            if (button_down && !send_long_press_handled &&
                now_ms - send_pressed_ms >= SEND_LONG_PRESS_MS) {
                if (assets_loaded) {
                    start_selected_asset();
                } else {
                    start_sd_error_blink();
                }
                send_long_press_handled = true;
            }
            if (send_event == BUTTON_EVENT_RELEASED &&
                !send_long_press_handled) {
                selected_digit_index = (selected_digit_index + 1) % 3;
                lcd_show_asset();
            }
        }
    }
}
