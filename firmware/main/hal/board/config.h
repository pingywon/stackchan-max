#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// M5Stack StackChan Board configuration

#include <driver/gpio.h>

#define AUDIO_INPUT_REFERENCE    true
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_0
#define AUDIO_I2S_GPIO_WS GPIO_NUM_33
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_34
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_14
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_13

#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_12
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_11
#define AUDIO_CODEC_AW88298_ADDR AW88298_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#define DISPLAY_SDA_PIN GPIO_NUM_NC
#define DISPLAY_SCL_PIN GPIO_NUM_NC
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true



/* Camera pins */
#define CAMERA_PIN_PWDN GPIO_NUM_NC
#define CAMERA_PIN_RESET GPIO_NUM_NC
#define CAMERA_PIN_XCLK  GPIO_NUM_NC // 像素时钟 (固定由 20MHz 外部晶振输入) 
#define CAMERA_PIN_SIOD GPIO_NUM_NC  // 串行时钟 Using existing I2C port
#define CAMERA_PIN_SIOC GPIO_NUM_NC  // 串行时钟 Using existing I2C port
#define CAMERA_PIN_D0 GPIO_NUM_39
#define CAMERA_PIN_D1 GPIO_NUM_40
#define CAMERA_PIN_D2 GPIO_NUM_41
#define CAMERA_PIN_D3 GPIO_NUM_42
#define CAMERA_PIN_D4 GPIO_NUM_15
#define CAMERA_PIN_D5 GPIO_NUM_16
#define CAMERA_PIN_D6 GPIO_NUM_48
#define CAMERA_PIN_D7 GPIO_NUM_47
#define CAMERA_PIN_VSYNC GPIO_NUM_46
#define CAMERA_PIN_HREF GPIO_NUM_38
#define CAMERA_PIN_PCLK GPIO_NUM_45

#define XCLK_FREQ_HZ 20000000

/* microSD card (SPI mode) — for uploaded MP3 playback and, later, SD-backed wake models.
 *
 * Pins confirmed against M5Stack's own official docs (docs.m5stack.com/en/arduino/
 * m5cores3/sdcard, exact init snippet: SPI.begin(36,35,37,4); SD.begin(4, SPI, 25000000)).
 * Not placeholders anymore — but STILL BLOCKED, for a different reason than "unknown pins":
 *
 * The SD card shares the display's SPI bus (SCK=36, MOSI=37 — same pins InitializeSpi()
 * already uses for the LCD, hence SD_SPI_HOST = SPI3_HOST below, not a separate host).
 * Worse: GPIO35 does double duty as BOTH the SD card's MISO AND the LCD's D/C (data/
 * command) line. Confirmed at the ESP-IDF level, not just Arduino/M5GFX — see the M5Stack
 * community thread "Core S3 SD/TF Card Issues": the LCD driver drives GPIO35 as an output
 * for D/C, which directly conflicts with MISO needing to be an input for SD reads. The
 * documented fix flips GPIO35's direction with gpio_set_direction() in SPI pre/post
 * transaction callbacks around each SD transaction — but `sdspi_device_config_t` (what
 * `esp_vfs_fat_sdspi_mount()` — the convenience API this file's InitializeSdCard() uses —
 * takes) has no pre_cb/post_cb fields to hook that in. Making this real needs either
 * patching the vendored `esp_driver_sdspi` component directly (not something any
 * `fetch_repos.py` patch touches today) or a lower-level custom SD-over-SPI + FATFS
 * integration that bypasses the convenience mount call entirely. Do not "fix" this by
 * just enabling MISO on InitializeSpi()'s existing bus without also solving the GPIO35
 * arbitration — that risks breaking the already-working display.
 *
 * Current behaviour with the pins below: InitializeSdCard() calls spi_bus_initialize()
 * on SPI3_HOST, which InitializeSpi() already initialized for the display — that call
 * correctly fails with ESP_ERR_INVALID_STATE, InitializeSdCard() logs a warning and
 * returns, sd_card_available_ stays false. Safe (touches nothing already working), but
 * means the SD/MP3 feature stays unavailable until the GPIO35 conflict above is solved.
 */
#define SD_MOSI_PIN GPIO_NUM_37   // shared with the display bus — same pin InitializeSpi() uses
#define SD_MISO_PIN GPIO_NUM_35   // ALSO the LCD's D/C pin — see the GPIO35 conflict above
#define SD_CLK_PIN  GPIO_NUM_36   // shared with the display bus — same pin InitializeSpi() uses
#define SD_CS_PIN   GPIO_NUM_4
#define SD_SPI_HOST SPI3_HOST     // same host the display uses — this is a shared bus, not a separate one
#define SD_MOUNT_POINT "/sdcard"

#endif // _BOARD_CONFIG_H_
