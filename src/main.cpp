
#include <Arduino.h>
#include "CANBus_Driver.h"
#include "LVGL_Driver.h"
#include "I2C_Driver.h"
#include "GaugeLogic.h"
#include "gauge_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"
#include <esp_random.h>
#include <cstdio>

// IMAGES
#include "images/tabby_needle.h"
#include "images/tabby_tick.h"

LV_IMG_DECLARE(tabby_needle);
LV_IMG_DECLARE(tabby_tick);
LV_IMG_DECLARE(tabby_mini_paw_bg);

QueueHandle_t canMsgQueue;
#define CAN_QUEUE_LENGTH 32
#define CAN_QUEUE_ITEM_SIZE sizeof(twai_message_t)

// DATA STORE
typedef struct struct_gauge_data {
  int scale_value;
}struct_gauge_data;

struct_gauge_data GaugeData;

// CONTROL CONSTANTS
const int AVERAGE_VALUES      = 10;
const int SCALE_MIN           = GAUGE.min_value * 10;  // tenths, see get_moving_average
const int SCALE_MAX           = GAUGE.max_value * 10;
const int SCALE_TICKS_COUNT   = GAUGE.tick_count;

const uint32_t GAUGE_UPDATE_MS = 100;  // how often the latest reading is sampled and drawn

const bool TESTING            = false; // set to true for needle sweep testing
#ifdef USE_REAL_CAN                    // set by the *_can environments in platformio.ini
const bool SIMULATE_CAN_DATA  = false;
#else
const bool SIMULATE_CAN_DATA  = true;  // feed random values instead of real CAN data
#endif

static_assert(SIMULATE_CAN_DATA || GAUGE.can_id != 0,
              "This gauge has no CAN signal defined yet; add it in gauge_config.h or enable simulation");

lv_obj_t *scale_ticks[SCALE_TICKS_COUNT];

// numeric labels for each tick, in whole units, derived from SCALE_MIN/SCALE_MAX
// (which are in tenths) so they stay correct if the range ever changes
static char scale_label_bufs[SCALE_TICKS_COUNT][8];
static const char *scale_label_src[SCALE_TICKS_COUNT + 1]; // NULL-terminated, per lv_scale_set_text_src

void init_scale_labels(void) {
  for (int i = 0; i < SCALE_TICKS_COUNT; i++) {
    int tenths = SCALE_MIN + (i * (SCALE_MAX - SCALE_MIN)) / (SCALE_TICKS_COUNT - 1);
    int whole = tenths >= 0 ? (tenths + 5) / 10 : -((-tenths + 5) / 10); // round to nearest whole unit
    snprintf(scale_label_bufs[i], sizeof(scale_label_bufs[i]), "%d", whole);
    scale_label_src[i] = scale_label_bufs[i];
  }
  scale_label_src[SCALE_TICKS_COUNT] = NULL;
}

#define TAG "TWAI"

// CONTROL VARIABLE INIT
bool receiving_data           = false; // has the first data been received
bool init_anim_complete       = false; // needle sweep completed
bool status_led               = false; // flashing LED for CAN activity

// ROLLING AVERAGE FOR SMOOTHING
int scale_moving_average  = 0;

// GLOBAL COMPONENTS
lv_obj_t *main_scr;
lv_obj_t *scale;
lv_obj_t *needle_img;

void drivers_init(void) {
  i2c_init();

  Serial.println("Scanning for TCA9554...");
  bool found = false;
  for (int attempt = 0; attempt < 10; attempt++) {
  if (i2c_scan_address(0x20)) { // 0x20 is default for TCA9554
      found = true;
      break;
    }
    delay(50); // wait a bit before retrying
  }

  if (!found) {
    Serial.println("TCA9554 not detected! Skipping expander init.");
  } else {
  tca9554pwr_init(0x00);
  }
  lcd_init();
  canbus_init();
  lvgl_init();
}

static int previous_scale_value = 0;

// update the UI with the latest value
static void set_needle_img_value(void * obj, int32_t v) {
  lv_scale_set_image_needle_value(scale, needle_img, v);
}

void update_scale(void) {
  // use a moving average of the last x values for smoothing
  int averaged_value = get_moving_average(GaugeData.scale_value);
  if (averaged_value == previous_scale_value) return; // needle is already there

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, scale); // 'scale' is your needle object
  lv_anim_set_values(&anim, previous_scale_value, averaged_value);
  lv_anim_set_time(&anim, GAUGE_UPDATE_MS);
  lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)set_needle_img_value);
  lv_anim_start(&anim);

  previous_scale_value = averaged_value;
}

// update parts with incoming values
void update_values(void) {
  update_scale();
}

// mark the loader as complete
void needle_sweep_complete(lv_anim_t *a) {
    init_anim_complete = true;
}

// scroll the needle from 0 to the current value
void needle_to_current(lv_anim_t *a) {
  lv_anim_t anim_scale_img;
  lv_anim_init(&anim_scale_img);
  lv_anim_set_var(&anim_scale_img, scale);
  lv_anim_set_exec_cb(&anim_scale_img, set_needle_img_value);
  lv_anim_set_duration(&anim_scale_img, 1000);  
  lv_anim_path_ease_out(&anim_scale_img);
  lv_anim_set_values(&anim_scale_img, SCALE_MIN, receiving_data ? previous_scale_value : SCALE_MIN);
  lv_anim_set_ready_cb(&anim_scale_img, needle_sweep_complete);
  lv_anim_start(&anim_scale_img);
}

// 0 to max to 0 sweep on load
void needle_sweep() {
  if (TESTING) {
    // back and forth sweep for testing
    lv_anim_t anim_scale_img;
    lv_anim_init(&anim_scale_img);
    lv_anim_set_var(&anim_scale_img, scale);
    lv_anim_set_exec_cb(&anim_scale_img, set_needle_img_value);
    lv_anim_set_duration(&anim_scale_img, 10000);
    lv_anim_set_repeat_count(&anim_scale_img, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_playback_duration(&anim_scale_img, 10000);
    lv_anim_set_values(&anim_scale_img, SCALE_MIN, SCALE_MAX);
    lv_anim_start(&anim_scale_img);
  } else {
    lv_anim_t anim_scale_img;
    lv_anim_init(&anim_scale_img);
    lv_anim_set_var(&anim_scale_img, scale);
    lv_anim_set_exec_cb(&anim_scale_img, set_needle_img_value);
    lv_anim_set_duration(&anim_scale_img, 2000);
    lv_anim_set_repeat_count(&anim_scale_img, 1);
    lv_anim_set_playback_duration(&anim_scale_img, 1000);
    lv_anim_set_values(&anim_scale_img, SCALE_MIN, SCALE_MAX);
    lv_anim_set_ready_cb(&anim_scale_img, needle_to_current);
    lv_anim_start(&anim_scale_img);
  }    
}

void make_scale_ticks(void) {
  for (int i = 0; i < SCALE_TICKS_COUNT; i++) {
    scale_ticks[i] = lv_image_create(main_scr);
    lv_image_set_src(scale_ticks[i], &tabby_tick);
    lv_obj_align(scale_ticks[i], LV_ALIGN_CENTER, 0, 196);
    lv_image_set_pivot(scale_ticks[i], 14, -182);

    lv_obj_set_style_image_recolor_opa(scale_ticks[i], 255, 0);
    lv_obj_set_style_image_recolor(scale_ticks[i], lv_color_make(255,255,255), 0); // TO DO - replace with color from CAN 

    int rotation_angle = (((i) * (240 / (SCALE_TICKS_COUNT - 1))) * 10); // angle calculation

    lv_image_set_rotation(scale_ticks[i], rotation_angle);
  }
}

// create the elements on the main scr
void main_scr_ui(void) {

  // scale used for needle
  scale = lv_scale_create(main_scr);
  lv_obj_set_size(scale, 480, 480);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_obj_set_style_bg_opa(scale, LV_OPA_0, 0);
  lv_scale_set_total_tick_count(scale, SCALE_TICKS_COUNT);
  lv_scale_set_major_tick_every(scale, 1); // every tick is major so it gets a label, matching our custom tick images
  lv_scale_set_label_show(scale, true);
  lv_obj_center(scale);
  lv_scale_set_range(scale, SCALE_MIN, SCALE_MAX);
  lv_scale_set_angle_range(scale, 240);
  lv_scale_set_rotation(scale, 90);

  // hide the built-in tick lines (we draw our own tick images below) but keep the numeric labels
  lv_obj_set_style_length(scale, 0, LV_PART_INDICATOR);
  lv_obj_set_style_length(scale, 0, LV_PART_ITEMS);
  lv_obj_set_style_pad_radial(scale, 70, LV_PART_INDICATOR); // pull labels inward, clear of the tick images
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_INDICATOR);

  init_scale_labels();
  lv_scale_set_text_src(scale, scale_label_src);

  lv_obj_t *lower_arc = lv_arc_create(main_scr);
  lv_obj_set_size(lower_arc, 420, 420);
  lv_arc_set_bg_angles(lower_arc, 90, 90 + (240 / (SCALE_TICKS_COUNT - 1)));
  lv_arc_set_value(lower_arc, 0);
  lv_obj_center(lower_arc);
  lv_obj_set_style_opa(lower_arc, 0, LV_PART_KNOB);
  lv_obj_set_style_arc_color(lower_arc, lv_color_make(87,10,1), LV_PART_MAIN);
  lv_obj_set_style_arc_width(lower_arc, 28, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(lower_arc, false, LV_PART_MAIN);

  lv_obj_t *upper_arc = lv_arc_create(main_scr);
  lv_obj_set_size(upper_arc, 420, 420);
  lv_arc_set_bg_angles(upper_arc, 330 - (240 / (SCALE_TICKS_COUNT - 1)), 330);
  lv_arc_set_value(upper_arc, 0);
  lv_obj_center(upper_arc);
  lv_obj_set_style_opa(upper_arc, 0, LV_PART_KNOB);
  lv_obj_set_style_arc_color(upper_arc, lv_color_make(87,10,1), LV_PART_MAIN);
  lv_obj_set_style_arc_width(upper_arc, 28, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(upper_arc, false, LV_PART_MAIN);
  
  make_scale_ticks();
  
  // needle image
  int needle_center_shift = 40; // how far the center of the needle is shifted from the left edge
  
  needle_img = lv_image_create(scale);
  lv_image_set_src(needle_img, &tabby_needle);
  lv_obj_align(needle_img, LV_ALIGN_CENTER, 108 - needle_center_shift, 0);
  lv_image_set_pivot(needle_img, needle_center_shift, 36);

  lv_obj_set_style_image_recolor_opa(needle_img, 255, 0);
  lv_obj_set_style_image_recolor(needle_img, lv_color_make(255,255,255), 0); // TO DO - replace with color from CAN

  // fluid icon, colored per gauge type
  lv_color_t icon_color = lv_color_make(GAUGE.icon_red, GAUGE.icon_green, GAUGE.icon_blue);
  lv_obj_t *gauge_icon;
  if (GAUGE.icon != nullptr) {
    gauge_icon = lv_image_create(main_scr);
    lv_image_set_src(gauge_icon, GAUGE.icon);
    lv_obj_set_style_image_recolor(gauge_icon, icon_color, 0);
    lv_obj_set_style_image_recolor_opa(gauge_icon, LV_OPA_COVER, 0);
  } else {
    gauge_icon = lv_label_create(main_scr);
    lv_label_set_text(gauge_icon, LV_SYMBOL_TINT);
    lv_obj_set_style_text_font(gauge_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(gauge_icon, icon_color, 0);
    lv_obj_set_style_transform_scale_x(gauge_icon, (int32_t)(LV_SCALE_NONE * 3.0f), 0);
    lv_obj_set_style_transform_scale_y(gauge_icon, (int32_t)(LV_SCALE_NONE * 3.0f), 0);
    // scaling pivots around the transform origin, which defaults to the object's
    // top-left corner rather than its center - pin it to center so zooming
    // doesn't visually drift the icon away from LV_ALIGN_CENTER
    lv_obj_set_style_transform_pivot_x(gauge_icon, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(gauge_icon, lv_pct(50), 0);
  }
  lv_obj_align(gauge_icon, LV_ALIGN_CENTER, 0, 0);

  // caption below the icon
  lv_obj_t *caption = lv_label_create(main_scr);
  lv_label_set_text(caption, GAUGE.caption);
  lv_obj_set_style_text_font(caption, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(caption, icon_color, 0);
  lv_obj_align(caption, LV_ALIGN_CENTER, 0, 90);
}

// build the screens
void screens_init(void) {
  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, lv_color_make(0,0,0), 0);
  lv_screen_load(main_scr);

  main_scr_ui();
}

void receive_can_task(void *arg) {
  while (1) {
    twai_message_t message;
    esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(5)); // lower timeout for faster response
    if (err == ESP_OK) {
      if (xQueueSend(canMsgQueue, &message, 0) != pdPASS) {
        ESP_LOGW(TAG, "CAN queue full, message dropped");
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      // No delay after successful receive
    } else if (err == ESP_ERR_TIMEOUT) {
      // Minimal delay when idle
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      ESP_LOGE(TAG, "Message reception failed: %s", esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void process_can_queue_task(void *arg) {
  twai_message_t message;
  while (1) {
    if (xQueueReceive(canMsgQueue, &message, pdMS_TO_TICKS(1)) == pdPASS) {
      if (GAUGE.decode != nullptr && message.identifier == GAUGE.can_id) {
        GaugeData.scale_value = GAUGE.decode(message.data);
        receiving_data = true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// generates readings across the gauge's range (gradual drift with small jitter,
// rather than jumping randomly) so the gauge can be exercised on the bench
// without a live CAN bus
void simulate_can_task(void *arg) {
  int current = (GAUGE.min_value + GAUGE.max_value) / 2;
  int target = current;

  while (1) {
    // occasionally pick a new target value to drift toward
    if ((esp_random() % 100) < 5) {
      target = GAUGE.min_value + (int)(esp_random() % (GAUGE.max_value - GAUGE.min_value + 1));
    }

    // step current value toward the target, plus a little jitter, so the
    // needle moves smoothly instead of teleporting
    int diff = target - current;
    int step = diff == 0 ? 0 : ((diff > 0 ? 1 : -1) * (1 + (int)(esp_random() % 3)));
    int jitter = (int)(esp_random() % 3) - 1; // -1, 0, or 1
    current += step + jitter;

    if (current < GAUGE.min_value) current = GAUGE.min_value;
    if (current > GAUGE.max_value) current = GAUGE.max_value;

    GaugeData.scale_value = current;
    receiving_data = true;

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void setup(void) {
  Serial.begin(115200);
  Serial.println("begin");
  drivers_init();
  set_backlight(80);
  screens_init();
  needle_sweep();
  set_exio(EXIO_PIN4, Low);
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %d\n", reason);

  if (SIMULATE_CAN_DATA) {
    xTaskCreatePinnedToCore(simulate_can_task, "Simulate_CAN_Task", 4096, NULL, 2, NULL, 1);
  } else {
    // Create CAN message queue
    canMsgQueue = xQueueCreate(CAN_QUEUE_LENGTH, CAN_QUEUE_ITEM_SIZE);
    if (canMsgQueue == NULL) {
      ESP_LOGE(TAG, "Failed to create CAN message queue");
      while (1) vTaskDelay(1000);
    }

    xTaskCreatePinnedToCore(receive_can_task, "Receive_CAN_Task", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(process_can_queue_task, "Process_CAN_Queue_Task", 4096, NULL, 2, NULL, 1);
  }
}

void loop(void) {
  static uint32_t last_update_ms = 0;

  lv_timer_handler();
  // sample at a fixed rate, not only when the reading changes, so the moving
  // average settles on a steady value
  if (receiving_data && millis() - last_update_ms >= GAUGE_UPDATE_MS) {
    last_update_ms = millis();
    update_values();
  }
  vTaskDelay(pdMS_TO_TICKS(1));
}