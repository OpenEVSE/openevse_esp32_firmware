#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "screens/screen_boot.h"
#include "lcd_common.h"
#include "lvgl.h"  // Add LVGL header

// LVGL objects for the boot screen
static lv_obj_t *container = nullptr;
static lv_obj_t *logo = nullptr;
static lv_obj_t *progress_bar = nullptr;
static lv_obj_t *message_line1 = nullptr;
static lv_obj_t *message_line2 = nullptr;

unsigned long BootScreen::update()
{
  if(_full_update)
  {
    // Create a container for boot screen elements
    container = lv_obj_create(lv_scr_act(), NULL);
    lv_obj_set_size(container, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_local_bg_color(container, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(TFT_OPENEVSE_BACK));

    // Create a container for the logo with white background
    lv_obj_t *logo_container = lv_obj_create(container, NULL);
    lv_obj_set_size(logo_container, 300, 110);
    lv_obj_set_pos(logo_container, 90, 60);
    lv_obj_set_style_local_radius(logo_container, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 15);
    lv_obj_set_style_local_bg_color(logo_container, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    // Create logo image
    logo = lv_img_create(logo_container, NULL);
    lv_img_set_src(logo, "/logo.png");
    lv_obj_align(logo, NULL, LV_ALIGN_IN_LEFT_MID, 14, 0);

    // Create progress bar
    progress_bar = lv_bar_create(container, NULL);
    lv_obj_set_size(progress_bar, BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT);
    lv_obj_set_pos(progress_bar, BOOT_PROGRESS_X, BOOT_PROGRESS_Y);
    lv_obj_set_style_local_bg_color(progress_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_radius(progress_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 8);
    lv_obj_set_style_local_bg_color(progress_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, lv_color_hex(TFT_OPENEVSE_GREEN));
    lv_obj_set_style_local_radius(progress_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 8);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

    // Create text labels for messages
    message_line1 = lv_label_create(container, NULL);
    lv_obj_set_pos(message_line1, 0, 250);
    lv_obj_set_width(message_line1, TFT_SCREEN_WIDTH);
    lv_obj_set_style_local_text_color(message_line1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_text_font(message_line1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &FreeSans9pt7b);
    lv_label_set_align(message_line1, LV_LABEL_ALIGN_CENTER);

    message_line2 = lv_label_create(container, NULL);
    lv_obj_set_pos(message_line2, 0, 270);
    lv_obj_set_width(message_line2, TFT_SCREEN_WIDTH);
    lv_obj_set_style_local_text_color(message_line2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_text_font(message_line2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &FreeSans9pt7b);
    lv_label_set_align(message_line2, LV_LABEL_ALIGN_CENTER);

    _full_update = false;
  }

  // Update progress bar
  if (progress_bar != nullptr) {
    int progress_value = (int)(_boot_progress * 100 / BOOT_PROGRESS_WIDTH);
    lv_bar_set_value(progress_bar, progress_value, LV_ANIM_OFF);
    _boot_progress += 10;
  }

  // Display any message lines
  String line = get_message_line(0);
  if (line.length() > 0 && message_line1 != nullptr) {
    lv_label_set_text(message_line1, line.c_str());
  }

  line = get_message_line(1);
  if (line.length() > 0 && message_line2 != nullptr) {
    lv_label_set_text(message_line2, line.c_str());
  }

  // Process LVGL tasks
  lv_task_handler();

  return 166;
}
