#include <stddef.h>

#include "hw_config.h"

static spi_t spis[] = {
    {
        .hw_inst = spi0,
        .miso_gpio = 16,
        .mosi_gpio = 19,
        .sck_gpio = 18,
        .baud_rate = 12 * 1000 * 1000,
    },
};

static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",
        .spi = &spis[0],
        .ss_gpio = 17,
        .use_card_detect = false,
        .card_detect_gpio = 0,
        .card_detected_true = 1,
    },
};

size_t sd_get_num(void) {
    return sizeof(sd_cards) / sizeof(sd_cards[0]);
}

sd_card_t *sd_get_by_num(size_t num) {
    return num < sd_get_num() ? &sd_cards[num] : NULL;
}

size_t spi_get_num(void) {
    return sizeof(spis) / sizeof(spis[0]);
}

spi_t *spi_get_by_num(size_t num) {
    return num < spi_get_num() ? &spis[num] : NULL;
}
