#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#include "core/mailbox.h"
#include "device/sensor_common.h"
#include "service/weather.h"

#define DISP_BUF_SIZE (128 * 1024)

static unsigned int g_weather_version = 0;
static unsigned short g_weather_selected = 0;

static void refresh_weather_labels(const weather_forecast_t *forecast, unsigned short index)
{
    char weather_text[128] = {0};

    if (forecast == NULL || !forecast->valid || forecast->count <= 0) {
        lv_roller_set_options(ui_weatherDateDropdown, "N/A", LV_ROLLER_MODE_NORMAL);
        lv_roller_set_selected(ui_weatherDateDropdown, 0, LV_ANIM_OFF);
        lv_label_set_text(ui_weekLabel, "N/A");
        lv_label_set_text(ui_cityLabel, "N/A");
        lv_label_set_text(ui_Label20, "N/A");
        g_weather_selected = 0;
        return;
    }

    if (index >= (unsigned short)forecast->count) {
        index = (unsigned short)(forecast->count - 1);
    }

    lv_label_set_text(ui_weekLabel, forecast->days[index].week);
    lv_label_set_text(ui_cityLabel, forecast->days[index].city);
    snprintf(weather_text,
             sizeof(weather_text),
             "%s %s",
             forecast->days[index].weather,
             forecast->days[index].temperature);
    lv_label_set_text(ui_Label20, weather_text);
    g_weather_selected = index;
}

static void refresh_weather_roller_if_needed(void)
{
    weather_forecast_t forecast;
    unsigned int version = 0;
    char options[WEATHER_FORECAST_MAX_DAYS * WEATHER_FIELD_LEN] = {0};
    int index = 0;

    version = weather_cache_get_version();
    if (version == g_weather_version && version != 0) {
        return;
    }

    if (weather_cache_get(&forecast) != 0) {
        if (g_weather_version == 0) {
            refresh_weather_labels(NULL, 0);
        }
        return;
    }

    for (index = 0; index < forecast.count; ++index) {
        if (index > 0) {
            strncat(options, "\n", sizeof(options) - strlen(options) - 1);
        }
        strncat(options,
                forecast.days[index].day,
                sizeof(options) - strlen(options) - 1);
    }

    lv_roller_set_options(ui_weatherDateDropdown, options, LV_ROLLER_MODE_NORMAL);
    if (g_weather_selected >= (unsigned short)forecast.count) {
        g_weather_selected = (unsigned short)(forecast.count - 1);
    }
    lv_roller_set_selected(ui_weatherDateDropdown, g_weather_selected, LV_ANIM_OFF);
    refresh_weather_labels(&forecast, g_weather_selected);
    g_weather_version = version;
}

static void weather_roller_event_cb(lv_event_t *event)
{
    weather_forecast_t forecast;
    lv_obj_t *target = NULL;

    target = lv_event_get_target(event);
    if (target == NULL) {
        return;
    }

    if (weather_cache_get(&forecast) != 0) {
        refresh_weather_labels(NULL, 0);
        return;
    }

    g_weather_selected = (unsigned short)lv_roller_get_selected(target);
    refresh_weather_labels(&forecast, g_weather_selected);
}

void my_timer(lv_timer_t *timer)
{
    char tempbuff[64] = {0};
    char humbuff[64] = {0};
    char alcbuff[64] = {0};
    char accxbuff[32] = {0};
    char accybuff[32] = {0};
    char acczbuff[32] = {0};
    data_t tmpdata;
    int ret = 0;

    (void)timer;
    ret = mailbox_recv_msg(&tmpdata);
    if (ret > 0) {

        sprintf(tempbuff, "%.2f C (%s)",
                tmpdata.temp,
                temp_source_name(tmpdata.temp_source));
        lv_label_set_text(ui_TempLabel, tempbuff);

        if (tmpdata.hum_valid) {
            sprintf(humbuff, " %.2f", tmpdata.hum);
        } else {
            sprintf(humbuff, "N/A");
        }
        lv_label_set_text(ui_HumLabel, humbuff);

        if (tmpdata.alcohol_valid) {
            sprintf(alcbuff, "%d (%s)",
                    tmpdata.alcohol_raw,
                    alcohol_level_name((alcohol_level_t)tmpdata.alcohol_level));
        } else {
            sprintf(alcbuff, "N/A");
        }
        lv_label_set_text(ui_alcLabel, alcbuff);

        if (tmpdata.accel_valid) {
            sprintf(accxbuff, "%d", tmpdata.accel_x);
            sprintf(accybuff, "%d", tmpdata.accel_y);
            sprintf(acczbuff, "%d", tmpdata.accel_z);
            lv_label_set_text(ui_acceLabelx, accxbuff);
            lv_label_set_text(ui_acceLabely, accybuff);
            lv_label_set_text(ui_acceLabelz, acczbuff);
            lv_label_set_text(ui_stateLabel, motion_state_name((motion_state_t)tmpdata.motion_state));
            lv_label_set_text(ui_alarmLabel, tmpdata.motion_alarm ? "ON" : "OFF");
        } else {
            lv_label_set_text(ui_acceLabelx, "N/A");
            lv_label_set_text(ui_acceLabely, "N/A");
            lv_label_set_text(ui_acceLabelz, "N/A");
            lv_label_set_text(ui_stateLabel, "N/A");
            lv_label_set_text(ui_alarmLabel, "OFF");
        }

        if (!tmpdata.accel_valid || tmpdata.motion_state == MOTION_STATE_NORMAL) {
            lv_obj_set_style_text_color(ui_stateLabel, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(ui_alarmLabel, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if (tmpdata.motion_state == MOTION_STATE_SHAKE) {
            lv_obj_set_style_text_color(ui_stateLabel, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(ui_alarmLabel, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_text_color(ui_stateLabel, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(ui_alarmLabel, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        {
            char canbuff[64];

            sprintf(canbuff, "%s  HB:%u",
                    tmpdata.can_f103_online ? "ONLINE" : "OFFLINE",
                    tmpdata.can_f103_hb);
            lv_label_set_text(ui_LabelF103, canbuff);
            if (tmpdata.can_f103_online) {
                lv_obj_set_style_text_color(ui_LabelF103, lv_palette_main(LV_PALETTE_GREEN),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            } else {
                lv_obj_set_style_text_color(ui_LabelF103, lv_palette_main(LV_PALETTE_RED),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            }

            sprintf(canbuff, "%s  HB:%u",
                    tmpdata.can_f407_online ? "ONLINE" : "OFFLINE",
                    tmpdata.can_f407_hb);
            lv_label_set_text(ui_LabelF407, canbuff);
            if (tmpdata.can_f407_online) {
                lv_obj_set_style_text_color(ui_LabelF407, lv_palette_main(LV_PALETTE_GREEN),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            } else {
                lv_obj_set_style_text_color(ui_LabelF407, lv_palette_main(LV_PALETTE_RED),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }

    refresh_weather_roller_if_needed();
}

void *lvgl_thread(void *arg)
{
    lv_timer_t *timer = NULL;

    (void)arg;

    /*LittlevGL init*/
    lv_init();

    /*Linux frame buffer device init*/
    fbdev_init();

    /*A small buffer for LittlevGL to draw the screen's content*/
    static lv_color_t buf[DISP_BUF_SIZE];

    /*Initialize a descriptor for the buffer*/
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    /*Initialize and register a display driver*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    lv_disp_drv_register(&disp_drv);

    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

    /*This function will be called periodically (by the library) to get the mouse position and state*/
    indev_drv_1.read_cb = evdev_read;
    lv_indev_t *mouse_indev = lv_indev_drv_register(&indev_drv_1);

    /*Set a cursor for the mouse*/
    LV_IMG_DECLARE(mouse_cursor_icon)
    lv_obj_t *cursor_obj = lv_img_create(lv_scr_act());
    lv_img_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(mouse_indev, cursor_obj);

    /*Create a Demo*/
    //lv_demo_widgets();
    ui_init();
    lv_obj_add_event_cb(ui_weatherDateDropdown, weather_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    refresh_weather_roller_if_needed();

    /* Create a periodic UI refresh timer */
    timer = lv_timer_create(my_timer, 1000, NULL);
    (void)timer;

    /*Handle LitlevGL tasks (tickless mode)*/
    while (1) {
        lv_timer_handler();
        usleep(5000);
    }

    ui_destroy();

    return NULL;
}

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    return (uint32_t)(now_ms - start_ms);
}
