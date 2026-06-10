
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "lv_demos.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_touch.h"
#include "user_config.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "sdcard_bsp.h"
#include "power_bsp.h"
#include "ui_reader.h"
#include "ui_menu.h"


static const char *TAG = "example";

static SemaphoreHandle_t lvgl_mux = NULL;   
static SemaphoreHandle_t flush_done_semaphore = NULL; 
uint8_t *lvgl_dest = NULL;

static uint16_t *trans_buf_1;

#define LCD_BIT_PER_PIXEL 16
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUFF_SIZE (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * BYTES_PER_PIXEL)

#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 10
#define LVGL_TASK_STACK_SIZE   (8 * 1024)   // 8KB: bumping higher starves the 48KB book-load task at boot
#define LVGL_TASK_PRIORITY     2


static void example_backlight_loop_task(void *arg);


static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = 
{
  	{0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 100},
};

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
  	BaseType_t high_task_awoken = pdFALSE;
  	xSemaphoreGiveFromISR(flush_done_semaphore, &high_task_awoken);
  	return false;
}

static void example_lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * color_p)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    lv_draw_sw_rgb565_swap(color_p, lv_area_get_width(area) * lv_area_get_height(area));
#if (Rotated == USER_DISP_ROT_90)
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    lv_area_t rotated_area;
    if(rotation != LV_DISPLAY_ROTATION_0)
    {
        lv_color_format_t cf = lv_display_get_color_format(disp);
        /*Calculate the position of the rotated area*/
        rotated_area = *area;
        lv_display_rotate_area(disp, &rotated_area);
        /*Calculate the source stride (bytes in a line) from the width of the area*/
        uint32_t src_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), cf);
        /*Calculate the stride of the destination (rotated) area too*/
        uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&rotated_area), cf);
        /*Have a buffer to store the rotated area and perform the rotation*/
        
        int32_t src_w = lv_area_get_width(area);
        int32_t src_h = lv_area_get_height(area);
        lv_draw_sw_rotate(color_p, lvgl_dest, src_w, src_h, src_stride, dest_stride, rotation, cf);
        /*Use the rotated area and rotated buffer from now on*/
        area = &rotated_area;
    }

    const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
    const int offgap = (EXAMPLE_LCD_V_RES / flush_coun);
    const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
    int offsetx1 = 0;
    int offsety1 = 0;
    int offsetx2 = EXAMPLE_LCD_H_RES;
    int offsety2 = offgap;

    uint16_t *map = (uint16_t *)lvgl_dest;
    xSemaphoreGive(flush_done_semaphore);
    for(int i = 0; i<flush_coun; i++)
    {
        xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
        memcpy(trans_buf_1,map,LVGL_DMA_BUFF_LEN);
        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2, offsety2, trans_buf_1);
        offsety1 += offgap;
        offsety2 += offgap;
        map += dmalen;
    }
    xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
    lv_disp_flush_ready(disp);
#else
    const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
    const int offgap = (EXAMPLE_LCD_V_RES / flush_coun);
    const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
    int offsetx1 = 0;
    int offsety1 = 0;
    int offsetx2 = EXAMPLE_LCD_H_RES;
    int offsety2 = offgap;

    uint16_t *map = (uint16_t *)color_p;
    xSemaphoreGive(flush_done_semaphore);
    for(int i = 0; i<flush_coun; i++)
    {
        xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
        memcpy(trans_buf_1,map,LVGL_DMA_BUFF_LEN);
        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2, offsety2, trans_buf_1);
        offsety1 += offgap;
        offsety2 += offgap;
        map += dmalen;
    }
    xSemaphoreTake(flush_done_semaphore,portMAX_DELAY);
    lv_disp_flush_ready(disp);
#endif
}

static esp_lcd_touch_handle_t g_tp = NULL;

// Touch is sampled in its own ~100 Hz task so the latest finger state is always
// fresh regardless of LVGL render load; the indev read_cb just copies this
// snapshot. The spinlock guards the cross-core (task vs LVGL) read/write.
static portMUX_TYPE      g_touch_mux     = portMUX_INITIALIZER_UNLOCKED;
static volatile bool     g_touch_pressed = false;
static volatile uint16_t g_touch_x       = 0;
static volatile uint16_t g_touch_y       = 0;

static void touch_sample_task(void *arg)
{
    int release_debounce = 0;
    for (;;) {
        uint16_t x = 0, y = 0;
        uint8_t  cnt = 0;
        esp_lcd_touch_read_data(g_tp);
        bool raw = esp_lcd_touch_get_coordinates(g_tp, &x, &y, NULL, &cnt, 1) && cnt > 0;

        // Bridge brief touch dropouts during a finger drag: the AXS15231B reports
        // no-touch for a few samples mid-motion, which would otherwise fragment one
        // swipe into many tiny taps. Hold "pressed" for up to ~80ms after the last
        // real reading; only a real reading updates the published position.
        bool pressed;
        if (raw) {
            release_debounce = 8;            // ~80ms at 100Hz
            pressed = true;
        } else {
            pressed = (release_debounce > 0);
            if (release_debounce > 0) release_debounce--;
        }

        taskENTER_CRITICAL(&g_touch_mux);
        g_touch_pressed = pressed;
        if (raw) { g_touch_x = x; g_touch_y = y; }
        taskEXIT_CRITICAL(&g_touch_mux);
        vTaskDelay(pdMS_TO_TICKS(10));       // ~100 Hz
    }
}

static void TouchInputReadCallback(lv_indev_t * indev, lv_indev_data_t *indevData)
{
    bool pressed_now;
    uint16_t x, y;
    taskENTER_CRITICAL(&g_touch_mux);
    pressed_now = g_touch_pressed;
    x = g_touch_x;
    y = g_touch_y;
    taskEXIT_CRITICAL(&g_touch_mux);

    if (pressed_now) {
        // Map the AXS15231B's reported coords into the rotated 640x172 logical space.
        // Calibrated from corner taps: driver-y = horizontal (20..620), driver-x =
        // inverted vertical (630=top .. 509=bottom). LVGL renders logical (flush rotates
        // the pixels), so it hit-tests in logical coords.
        // Calibrated affine (driver coords -> logical 640x172) from 3-point calibration.
        int lx = (78 * (int)x + 999 * (int)y) / 1000 - 54;
        int ly = (-891 * (int)x + 11 * (int)y) / 1000 + 565;
        if (lx < 0) lx = 0; else if (lx > 639) lx = 639;
        if (ly < 0) ly = 0; else if (ly > 171) ly = 171;
        // LVGL rotates the fed (native 172x640) point to logical as hit=(640-fed_y, fed_x),
        // so feed the inverse to land on (lx, ly).
        indevData->state = LV_INDEV_STATE_PRESSED;
        indevData->point.x = ly;
        indevData->point.y = 640 - lx;
    } else {
        indevData->state = LV_INDEV_STATE_RELEASED;
    }
}

static void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static bool example_lvgl_lock(int timeout_ms)
{
    assert(lvgl_mux && "bsp_display_start must be called first");

    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

static void example_lvgl_unlock(void)
{
    assert(lvgl_mux && "bsp_display_start must be called first");
    xSemaphoreGive(lvgl_mux);
}

static void example_lvgl_port_task(void *arg)
{
    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    for(;;)
    {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (example_lvgl_lock(-1))
        {
            task_delay_ms = lv_timer_handler();
            // Release the mutex
            example_lvgl_unlock();
        }
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        }
        else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

void app_main(void)
{
    power_bsp_init();   // assert battery power-hold (TCA9554 P6) ASAP
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    flush_done_semaphore = xSemaphoreCreateBinary();
    assert(flush_done_semaphore);
    touch_i2c_master_Init();
    sdcard_init();
    ESP_LOGI(TAG, "Initialize SPI bus");
	gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = ((uint64_t)0x01<<EXAMPLE_PIN_NUM_LCD_RST);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num =  EXAMPLE_PIN_NUM_LCD_PCLK;  
    buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;            
    buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;             
    buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));  
    
	ESP_LOGI(TAG, "Install panel IO");
	esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    
    esp_lcd_panel_io_spi_config_t io_config = {};
		io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;                 
        io_config.dc_gpio_num = -1;          
        io_config.spi_mode = 3;              
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;    
        io_config.on_color_trans_done = example_notify_lvgl_flush_ready; 
        //io_config.user_ctx = &disp_drv,         
        io_config.lcd_cmd_bits = 32;         
        io_config.lcd_param_bits = 8;        
        io_config.flags.quad_mode = true;                         
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));
    
	axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);
    
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config = &vendor_config;
    
    ESP_LOGI(TAG, "Install panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));
    
	ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST,1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST,0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST,1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    /*lvgl port*/
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    lv_display_t * disp = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);  /* 以水平和垂直分辨率（像素）进行基本初始化 */
    lv_display_set_flush_cb(disp, example_lvgl_flush_cb);                           /* 设置刷新回调函数以绘制到显示屏 */
    
    uint8_t *buffer_1 = NULL;
    uint8_t *buffer_2 = NULL;
    buffer_1 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(buffer_1);
    buffer_2 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(buffer_2);
	trans_buf_1 = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
	assert(trans_buf_1);
    lv_display_set_buffers(disp, buffer_1, buffer_2, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(disp, panel);
#if (Rotated == USER_DISP_ROT_90)
    lvgl_dest = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM); //旋转buf
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
#endif

    /*port indev*/
    lv_indev_t *touch_indev = NULL;
    touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, TouchInputReadCallback);
    lv_indev_set_scroll_limit(touch_indev, 80);   // touch drifts ~44px during a tap; don't misread taps as scrolls

    /* AXS15231B touch over the shared I2C bus (user_i2c_port1_handle). Feed LVGL
       its logical (pre-rotation, portrait 172x640) coordinates; swap_xy converts
       the panel's landscape raw read. mirror_* dialed in via the corner-tap check. */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    tp_io_cfg.scl_speed_hz = 300000;   // CONFIG_EX macro is buggy (param name collides with field)
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(user_i2c_port1_handle, &tp_io_cfg, &tp_io));
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = { .swap_xy = 1, .mirror_x = 0, .mirror_y = 1 },   // mirror_y=1: taps land on the menu/list/buttons
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs15231b(tp_io, &tp_cfg, &g_tp));
    lv_timer_set_period(lv_indev_get_read_timer(touch_indev), 10);  // ~100 Hz indev ceiling
    xTaskCreatePinnedToCore(touch_sample_task, "touch", 4 * 1024, NULL, 3, NULL, 1);

    esp_timer_create_args_t lvgl_tick_timer_args = {};
    lvgl_tick_timer_args.callback = &example_increase_lvgl_tick;
    lvgl_tick_timer_args.name = "lvgl_tick";
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    lvgl_mux = xSemaphoreCreateMutex(); //mutex semaphores
    assert(lvgl_mux);
    xTaskCreatePinnedToCore(example_lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL,0);
    xTaskCreatePinnedToCore(example_backlight_loop_task, "example_backlight_loop_task", 4 * 1024, NULL, 2, NULL,0); 
    if (example_lvgl_lock(-1))
    {
        rsvp_reader_init(); /* persistent 48KB book-load task while internal RAM is free */
        ui_menu_init();     /* BOOT button + navigation timer */
        ui_menu_open();     /* boot screen = the menu; books load on demand from the Library */
        example_lvgl_unlock();
    }
}

static void example_backlight_loop_task(void *arg)
{
    for(;;)
    {
#if  (Backlight_Testing == true)
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_255);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_175);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_125);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_0);
#else
        vTaskDelay(pdMS_TO_TICKS(2000));
#endif
    }
}
