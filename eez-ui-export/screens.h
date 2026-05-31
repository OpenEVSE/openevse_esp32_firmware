#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_SCREEN_CHARGE = 1,
    SCREEN_ID_SCREEN_BOOT = 2,
    SCREEN_ID_SCREEN_SLEEPING = 3,
    SCREEN_ID_SCREEN_FAULT = 4,
    _SCREEN_ID_LAST = 4
};

typedef struct _objects_t {
    lv_obj_t *screen_charge;
    lv_obj_t *screen_boot;
    lv_obj_t *screen_sleeping;
    lv_obj_t *screen_fault;
    lv_obj_t *brand_logo;
    lv_obj_t *obj0;
    lv_obj_t *charge_state_label;
    lv_obj_t *charge_wifi_label;
    lv_obj_t *charge_power_ring;
    lv_obj_t *charge_kw_value;
    lv_obj_t *obj1;
    lv_obj_t *stats_card;
    lv_obj_t *obj2;
    lv_obj_t *charge_amps_value;
    lv_obj_t *obj3;
    lv_obj_t *charge_volts_value;
    lv_obj_t *obj4;
    lv_obj_t *charge_energy_value;
    lv_obj_t *obj5;
    lv_obj_t *charge_elapsed_value;
    lv_obj_t *obj6;
    lv_obj_t *charge_temp_value;
    lv_obj_t *obj7;
    lv_obj_t *charge_rate_value;
    lv_obj_t *btn_start_stop;
    lv_obj_t *btn_start_stop_label;
    lv_obj_t *bottom_nav;
    lv_obj_t *obj8;
    lv_obj_t *obj9;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *obj12;
    lv_obj_t *obj13;
    lv_obj_t *boot_status_label;
    lv_obj_t *boot_progress;
    lv_obj_t *sleeping_ring;
    lv_obj_t *obj14;
    lv_obj_t *sleeping_state_label;
    lv_obj_t *obj15;
    lv_obj_t *btn_wake;
    lv_obj_t *obj16;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *obj19;
    lv_obj_t *obj20;
    lv_obj_t *obj21;
    lv_obj_t *obj22;
    lv_obj_t *fault_ring;
    lv_obj_t *obj23;
    lv_obj_t *obj24;
    lv_obj_t *fault_text_label;
    lv_obj_t *obj25;
    lv_obj_t *obj26;
    lv_obj_t *obj27;
    lv_obj_t *obj28;
    lv_obj_t *obj29;
    lv_obj_t *obj30;
    lv_obj_t *obj31;
} objects_t;

extern objects_t objects;

void create_screen_screen_charge();
void tick_screen_screen_charge();

void create_screen_screen_boot();
void tick_screen_screen_boot();

void create_screen_screen_sleeping();
void tick_screen_screen_sleeping();

void create_screen_screen_fault();
void tick_screen_screen_fault();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

// Color themes

enum Themes {
    THEME_ID_NIGHTSHIFT,
    THEME_ID_LIGHT,
};
enum Colors {
    COLOR_ID_SURFACE,
    COLOR_ID_SURFACE2,
    COLOR_ID_SURFACE3,
    COLOR_ID_TEXT,
    COLOR_ID_TEXT_DIM,
    COLOR_ID_ACCENT,
    COLOR_ID_BORDER,
    COLOR_ID_CHARGING,
    COLOR_ID_ERROR,
    COLOR_ID_WARNING,
    COLOR_ID_SLEEP,
    COLOR_ID_SUCCESS,
};
void change_color_theme(uint32_t themeIndex);
extern uint32_t theme_colors[2][12];
extern uint32_t active_theme_index;

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/