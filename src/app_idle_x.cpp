// The running app's idle limit (app_idle_x.h)
#ifdef BOARD_TRMNL_X

#include "app_idle_x.h"

#include <epd_driver.h>
#include <esp_heap_caps.h>
#include <fonts.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_host.h"
#include "x_devset_x.h"

#define TOAST_ROWS 110

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static app_idle_t timer; // under mux
static uint8_t *under; // the rows the toast covers, while it shows
static int under_y;
static size_t under_len;

void app_idle_x_begin(const char *app) {
  char dev[16] = "", mine[16] = "", key[APP_IDLE_KEY_MAX + 1];
  if (x_devset_get("app_idle_min", dev, sizeof(dev)) < 0) dev[0] = '\0';
  if (app_idle_app_key(app, key, sizeof(key)) && x_devset_get(key, mine, sizeof(mine)) < 0) mine[0] = '\0';
  int min = app_idle_minutes(dev, mine);
  taskENTER_CRITICAL(&mux);
  app_idle_begin(&timer, millis(), min);
  taskEXIT_CRITICAL(&mux);
  Log_info("Apps: %s closes after %d min idle (0: never)", app, min);
}

void app_idle_x_note(void) {
  taskENTER_CRITICAL(&mux);
  app_idle_activity(&timer, millis());
  taskEXIT_CRITICAL(&mux);
}

void app_idle_x_hold(bool on) {
  taskENTER_CRITICAL(&mux);
  app_idle_hold(&timer, on, millis());
  taskEXIT_CRITICAL(&mux);
}

int app_idle_x_minutes(void) { return (int)(timer.limit_ms / 60000u); }

static bool toast_possible(void) {
  return app_host.panel && !app_host.screen_held && bbep.getMode() == BB_MODE_1BPP && bbep.currentBuffer();
}

static void toast_show(void) {
  if (under || !toast_possible()) return;
  const int w = bbep.width(), h = bbep.height(), pitch = (w + 7) >> 3;
  under_len = (size_t)pitch * TOAST_ROWS;
  under_y = h - TOAST_ROWS;
  uint8_t *keep = (uint8_t *)heap_caps_malloc(under_len, MALLOC_CAP_SPIRAM);
  if (!keep) return; // no memory: no warning, the end still comes
  xSemaphoreTake(app_host.panel, portMAX_DELAY);
  memcpy(keep, bbep.currentBuffer() + (size_t)pitch * under_y, under_len);
  under = keep;
  bbep.fillRect(0, under_y, w, TOAST_ROWS, BBEP_WHITE);
  bbep.drawRect(8, under_y + 8, w - 16, TOAST_ROWS - 16, BBEP_BLACK);
  bbep.drawRect(10, under_y + 10, w - 20, TOAST_ROWS - 20, BBEP_BLACK);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  bbep.setFont(Inter_18);
  bbep.drawString("Closing in 30 s, touch to stay", w / 8, under_y + TOAST_ROWS / 2 + 8);
  bbep.partialUpdate(true, under_y, h - 1);
  xSemaphoreGive(app_host.panel);
}

static void toast_hide(void) {
  if (!under) return;
  xSemaphoreTake(app_host.panel, portMAX_DELAY);
  if (bbep.currentBuffer() && !app_host.screen_held) {
    const int pitch = (bbep.width() + 7) >> 3;
    memcpy(bbep.currentBuffer() + (size_t)pitch * under_y, under, under_len);
    bbep.partialUpdate(true, under_y, bbep.height() - 1);
  }
  heap_caps_free(under);
  under = nullptr;
  xSemaphoreGive(app_host.panel);
}

app_idle_event_t app_idle_x_poll(void) {
  taskENTER_CRITICAL(&mux);
  app_idle_event_t ev = app_idle_poll(&timer, millis());
  bool warning = app_idle_warning(&timer);
  taskEXIT_CRITICAL(&mux);
  if (ev == APP_IDLE_WARN) toast_show();
  else if (!warning && under && ev != APP_IDLE_EXPIRED) toast_hide();
  return ev;
}

void app_idle_x_end(void) {
  if (under) { // the app's task is gone, its buffer is TRMNL's next: just let the rows go
    heap_caps_free(under);
    under = nullptr;
  }
}

#endif // BOARD_TRMNL_X
