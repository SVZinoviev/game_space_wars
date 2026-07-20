#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "math.h"

/* Both panels latch RGB565 big-endian, so bake the byte swap into the color
 * constants and let panel_flush() blit the framebuffer straight through. */
#define GFX_SWAP_COLOR_BYTES 1
#include "gfx.h"
#include "ft6336u_glue.h"

/* Select the target display: set DISPLAY_DRIVER to one of the values below. */
#define DISPLAY_ILI9341  0
#define DISPLAY_ST7789   1

#define DISPLAY_DRIVER   DISPLAY_ILI9341

#if DISPLAY_DRIVER == DISPLAY_ILI9341
#include "ili9341_esp_driver.h"
#define lcd_init        ili9341_esp_driver_init
#define lcd_draw_bitmap ili9341_esp_driver_draw_bitmap
#endif

#include "game_renderer.h"

static const char *TAG = "gfx_rect";

/* Signalled from the panel ISR when a transfer completes, so the next flush
 * waits before overwriting the framebuffer. */
static SemaphoreHandle_t s_trans_done;

bool lcd_trans_done(esp_lcd_panel_io_handle_t io,
                    esp_lcd_panel_io_event_data_t *edata,
                    void *user_ctx);

/* --- glue layer (outside the gfx component) ------------------------------- */

/* gfx blit callback. With GFX_SWAP_COLOR_BYTES the framebuffer is already in
 * the panel's big-endian order, so this is a straight passthrough: hand the
 * region to the driver, then wait for the transfer to finish before the next
 * flush overwrites the framebuffer. */
static void panel_flush(void *ctx, uint16_t x, uint16_t y, uint16_t w,
                        uint16_t h, const void *pixels)
{
    (void)ctx;
    lcd_draw_bitmap(x, y, w, h, (uint16_t *)pixels);
    xSemaphoreTake(s_trans_done, portMAX_DELAY);
}

static const gfx_color_t black  = gfx_rgb888_to_rgb565(0, 0, 0);
// static const gfx_color_t orange = gfx_rgb888_to_rgb565(220, 100, 90);
// static const gfx_color_t gray = gfx_rgb888_to_rgb565(128, 128, 128);;

/* I2C pins for the FT6336U */
#define TOUCH_I2C_SDA 16
#define TOUCH_I2C_SCL 15

/* Touch detection threshold (THGROUP); higher = firmer touch required. */
#define TOUCH_THRESHOLD 22

/* Filter coefficient (FILTER_COE); higher = smoother coordinates, more lag. */
#define TOUCH_FILTER 0x10

#define BTN_CCW_X0 3
#define BTN_CCW_Y0 230
#define BTN_CCW_X1 106
#define BTN_CCW_Y1 180

#define BTN_TH_X0 110
#define BTN_TH_Y0 230
#define BTN_TH_X1 211
#define BTN_TH_Y1 180

#define BTN_CW_X0 215
#define BTN_CW_Y0 230
#define BTN_CW_X1 318
#define BTN_CW_Y1 180

/* Owned by touch_task; the render loop only reads the volatiles below. */
static struct ft6336_instance touch;
static volatile bool g_touch_ok;    /* set once the controller is up        */

static bool ccw = false;
static bool cw = false;
static bool th = false;

/* Fired by ft6336_poll() on a press-down inside the button zone. */
static void on_ccw_button(struct ft6336_instance *pinstance, uint16_t x, uint16_t y,
                      void *puser)
{
    (void)pinstance;
    (void)x;
    (void)y;
    (void)puser;
    ccw = true;
}

/* Fired by ft6336_poll() on a press-down inside the button zone. */
static void on_th_button(struct ft6336_instance *pinstance, uint16_t x, uint16_t y,
                      void *puser)
{
    (void)pinstance;
    (void)x;
    (void)y;
    (void)puser;
    th = true;
}

/* Fired by ft6336_poll() on a press-down inside the button zone. */
static void on_cw_button(struct ft6336_instance *pinstance, uint16_t x, uint16_t y,
                      void *puser)
{
    (void)pinstance;
    (void)x;
    (void)y;
    (void)puser;
    cw = true;
}

/* Dedicated input task: owns the FT6336U, dispatches zone callbacks and
 * publishes the latest touch point for the renderer to draw. */
static void touch_task(void *arg)
{
    (void)arg;

    if (ft6336u_glue_install(&touch, TOUCH_I2C_SDA, TOUCH_I2C_SCL, 400000,
                             TOUCH_THRESHOLD, TOUCH_FILTER) != FT6336_OK) {
        ESP_LOGW(TAG, "FT6336U not found; touch disabled");
        vTaskDelete(NULL);
        return;
    }
    /* Native-portrait panel under a landscape display: swap axes, mirror Y. */
    touch.width = LCD_WIDTH;
    touch.height = LCD_HEIGHT;
    touch.swap_xy = true;
    touch.invert_y = true;
    ft6336_register_zone(&touch, BTN_CCW_X0, BTN_CCW_Y0, BTN_CCW_X1, BTN_CCW_Y1, on_ccw_button, NULL);
    ft6336_register_zone(&touch, BTN_TH_X0, BTN_TH_Y0, BTN_TH_X1, BTN_TH_Y1, on_th_button, NULL);
    ft6336_register_zone(&touch, BTN_CW_X0, BTN_CW_Y0, BTN_CW_X1, BTN_CW_Y1, on_cw_button, NULL);
    g_touch_ok = true;
    ESP_LOGI(TAG, "FT6336U touch ready");

    while (true) {
        ft6336_poll(&touch);  /* read + dispatch zone callbacks (press edge) */
        vTaskDelay(pdMS_TO_TICKS(5));  /* ~50 Hz input sampling */
    }
}

void app_main(void)
{
    s_trans_done = xSemaphoreCreateBinary();

    lcd_init(lcd_trans_done);

    struct gfx_display display = {
        .width = LCD_WIDTH,
        .height = LCD_HEIGHT,
        .format = GFX_FMT_RGB565,
        .flush = panel_flush,
        .user_ctx = NULL,
    };
    struct gfx canvas;
    if (gfx_init(&canvas, &display) != GFX_OK) {
        ESP_LOGE(TAG, "gfx_init failed (out of memory?)");
        return;
    }

    gfx_clear(&canvas, black);

    gfx_flush(&canvas);

    xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);

    while (true) {
        gfx_clear(&canvas, black);

        if (cw) {rotate_ship_cw(); cw = false;}
        if (ccw) {rotate_ship_ccw(); ccw = false;}
        if (th) {thrust_increase(); th = false;}

        render_spaceship(&canvas);
        render_buttons(&canvas);
        gfx_flush(&canvas);
    }
}

bool lcd_trans_done(esp_lcd_panel_io_handle_t io,
                    esp_lcd_panel_io_event_data_t *edata,
                    void *user_ctx)
{
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &hp_task_woken);
    portYIELD_FROM_ISR( hp_task_woken );
    return hp_task_woken == pdTRUE;
}
