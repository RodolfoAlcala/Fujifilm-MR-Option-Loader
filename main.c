#include "pico/stdlib.h"
#include "tusb.h"
#include "ff.h"
#include "hw_config.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BUTTON_PIN 15
#define NEXT_CHARACTER_BUTTON_PIN 13
#define MODALITY_BUTTON_PIN 14
#define STATUS_LED_PIN PICO_DEFAULT_LED_PIN
#define MAX_OPTIONS_PER_ASSET 50
#define MAX_OPTION_LENGTH 16
#define MAX_CSV_LINE_LENGTH 64
#define KEYBOARD_REPORT_DELAY_MS 10
#define LCD_I2C i2c0
#define LCD_SDA_PIN 4
#define LCD_SCL_PIN 5
#define OLED_WIDTH 128
#define OLED_PAGES 4
#define LCD_CONTRAST_ADC 0
#define LCD_CONTRAST_PIN 26
#define SEND_LONG_PRESS_MS 800
#define OLED_ADDRESS_PRIMARY 0x3c

static char output_lines[MAX_OPTIONS_PER_ASSET][MAX_OPTION_LENGTH + 1];
static size_t output_line_count;
static bool assets_loaded;
static bool selected_file_empty;
static bool sd_missing;
static bool awaiting_asset_selection;
static size_t asset_file_count;
static bool codes_sending;
static bool codes_sent;
static const char *modalities[] = {"M", "OV", "SY", "Y"};
static size_t modality_index = 2;
static char selected_asset_name[16] = "SY629";
static size_t selected_digit_index;
static bool lcd_ready;
static uint8_t lcd_contrast = 0xff;
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

static void lcd_set_contrast(uint8_t contrast) {
    if (!lcd_ready || contrast == lcd_contrast) {
        return;
    }
    lcd_write_command(0x81);
    lcd_write_command(contrast);
    lcd_contrast = contrast;
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

static void update_lcd_contrast(void) {
    adc_select_input(LCD_CONTRAST_ADC);
    uint16_t reading = adc_read();
    uint8_t contrast = (uint8_t)((reading * 255u) / 4095u);
    lcd_set_contrast(contrast);
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
            printf("OLED disconnected\n");
        }
        return;
    }

    lcd_init();
    if (lcd_ready) {
        update_lcd_contrast();
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

static void lcd_show_asset(void) {
    if (!lcd_ready) {
        return;
    }
    lcd_clear();
    lcd_write_centered_text(selected_asset_name, 1);
    char selection[17];
    if (codes_sending) {
        snprintf(selection, sizeof(selection), "Sending codes...");
    } else if (codes_sent) {
        snprintf(selection, sizeof(selection), "All codes sent");
    } else {
        size_t number_start = 0;
        while (selected_asset_name[number_start] != '\0' &&
               (selected_asset_name[number_start] < '0' ||
                selected_asset_name[number_start] > '9')) {
            number_start++;
        }
        snprintf(selection, sizeof(selection), "Digit: %c",
                 selected_asset_name[number_start + selected_digit_index]);
    }
    lcd_write_centered_text(selection, 2);
    char status[17];
    if (sd_missing) {
        snprintf(status, sizeof(status), "SD not inserted");
    } else if (assets_loaded) {
        snprintf(status, sizeof(status), "%u options",
                 (unsigned)output_line_count);
    } else if (selected_file_empty) {
        snprintf(status, sizeof(status), "File empty");
    } else {
        snprintf(status, sizeof(status), "No such file");
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
    size_t line_index;
    size_t character_index;
    bool sending_newline;
    uint32_t next_action_ms;
} KeyboardTask;

static KeyboardTask keyboard_task_state = {0};

static bool load_assets_from_sd(const char *asset_name);

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
        gpio_put(STATUS_LED_PIN, 0);
        return;
    }

    gpio_put(STATUS_LED_PIN, !gpio_get(STATUS_LED_PIN));
    led_blink_task.next_toggle_ms = now_ms + 100;
}

static bool load_assets_from_sd(const char *asset_name) {
    selected_file_empty = false;
    sd_missing = false;
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
    // Each CSV row is "description,code"; only the second column is typed.
    while (output_line_count < MAX_OPTIONS_PER_ASSET &&
           f_gets(line, sizeof(line), &file) != NULL) {
        if (!header_skipped) {
            header_skipped = true;
            continue;
        }
        char *separator = strchr(line, ',');
        if (separator != NULL) {
            *separator = '\0';
        } else {
            continue;
        }
        char *code = separator + 1;
        line[strcspn(line, "\r\n")] = '\0';
        code[strcspn(code, "\r\n")] = '\0';
        if (strlen(code) == 0 || strlen(code) > MAX_OPTION_LENGTH) {
            continue;
        }
        strncpy(output_lines[output_line_count++], code, MAX_OPTION_LENGTH);
        printf("Loaded option: %s\n", output_lines[output_line_count - 1]);
    }
    f_close(&file);
    f_unmount(card->pcName);

    if (output_line_count == 0) {
        selected_file_empty = true;
        printf("%s has no option codes\n", filename);
        return false;
    }
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
    while (*name_number != '\0' && (*name_number < '0' || *name_number > '9')) {
        name_number++;
    }
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

static void start_selected_asset(void) {
    keyboard_task_state.text = output_lines[0];
    keyboard_task_state.line_index = 0;
    keyboard_task_state.character_index = 0;
    keyboard_task_state.sending_newline = false;
    keyboard_task_state.state = KEYBOARD_PRESS;
    keyboard_task_state.next_action_ms = 0;
    codes_sending = true;
    codes_sent = false;
    lcd_show_asset();
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
                // A completed line is followed by a newline, then the next code.
                task->line_index++;
                if (task->line_index >= output_line_count) {
                    task->state = KEYBOARD_IDLE;
                    codes_sending = false;
                    codes_sent = true;
                    lcd_show_asset();
                    return;
                }
                task->text = output_lines[task->line_index];
                task->sending_newline = false;
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
    printf("Pico starting\n");
    adc_init();
    adc_gpio_init(LCD_CONTRAST_PIN);
    lcd_init();
    update_lcd_contrast();
    lcd_show_message("Initializing...");
    sleep_ms(500);
    lcd_show_message("Loading SD...");
    printf("SD initializing\n");
    asset_file_count = count_asset_files_on_sd();
    awaiting_asset_selection = true;
    printf("Startup complete\n");
    lcd_show_asset_count();

    gpio_put(STATUS_LED_PIN, 0);

    // Buttons are wired to ground, so the internal pull-ups make a press read low.
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    gpio_init(NEXT_CHARACTER_BUTTON_PIN);
    gpio_set_dir(NEXT_CHARACTER_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(NEXT_CHARACTER_BUTTON_PIN);
    gpio_init(MODALITY_BUTTON_PIN);
    gpio_set_dir(MODALITY_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(MODALITY_BUTTON_PIN);

    tusb_init();

    bool button_latched = false;
    bool next_character_latched = false;
    bool modality_latched = false;
    uint32_t send_pressed_ms = 0;
    bool send_long_press_handled = false;

    while (true) {
        // Keep every service routine short and non-blocking so USB, storage, and
        // display updates can continue while the user interacts with the buttons.
        tud_task();
        keyboard_task();
        update_lcd_presence();
        update_sd_presence();
        update_lcd_contrast();
        led_blink_task_update();

        // Convert active-low GPIO readings into the logical button state.
        bool button_down = !gpio_get(BUTTON_PIN);
        bool next_character_down = !gpio_get(NEXT_CHARACTER_BUTTON_PIN);
        bool modality_down = !gpio_get(MODALITY_BUTTON_PIN);
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (awaiting_asset_selection) {
            // The first SEND release leaves the startup screen and loads the default asset.
            if (!button_down && button_latched) {
                awaiting_asset_selection = false;
                assets_loaded = load_assets_from_sd(selected_asset_name);
                lcd_show_asset();
            }
            button_latched = button_down;
            next_character_latched = next_character_down;
            modality_latched = modality_down;
            continue;
        }
        if (keyboard_task_state.state == KEYBOARD_IDLE) {
            // Rising logical edges prevent a held button from repeating an action.
            if (next_character_down && !next_character_latched) {
                increment_selected_digit();
            }
            if (modality_down && !modality_latched) {
                select_modality(1);
            }
            if (button_down && !button_latched) {
                send_pressed_ms = now_ms;
                send_long_press_handled = false;
            }
            if (button_down && !send_long_press_handled &&
                now_ms - send_pressed_ms >= SEND_LONG_PRESS_MS) {
                // A long press sends all loaded codes; without a valid file, signal an SD error.
                if (assets_loaded) {
                    start_selected_asset();
                } else {
                    start_sd_error_blink();
                }
                send_long_press_handled = true;
            }
            if (!button_down && button_latched && !send_long_press_handled) {
                // A short SEND release advances the digit selected for editing.
                selected_digit_index = (selected_digit_index + 1) % 3;
                lcd_show_asset();
            }
        }
        button_latched = button_down;
        next_character_latched = next_character_down;
        modality_latched = modality_down;
    }
}
