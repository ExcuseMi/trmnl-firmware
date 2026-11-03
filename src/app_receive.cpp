// Receives apps over the USB serial port into /apps (apps.h, docs/apps.md "Copy an app to the X"): the
// protocol of app_upload.h, host side apps/upload.py. Only /apps/<name>.elf and a temporary file are
// written; the rest of the file system (cached screens, the modem file) is left alone.
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <LittleFS.h>
#include <app_install.h>
#include <app_space.h>
#include <app_upload.h>
#include <serial_hold.h>
#include <serial_out.h>
#include <esp_heap_caps.h>
#include <trmnl_crc32.h>
#include <apps.h>
#include <trmnl_log.h>

#include <algorithm>
#include <string.h>

#include "app_receive.h"
#include "evlog.h"
#include "serial_out_hw.h"
#include "task_watch_x.h"
#include "webshare.h" // webshare_fs_root

#define APP_UPLOAD_STALL_MAX_MS 30000u // a TX ring that never empties still ends the upload (serial_hold.h)
#define RECEIVE_IDLE_MS (10u * 60u * 1000u) // no upload started for this long: back to the menu

void apps_reply(const char *fmt, ...) {
  char line[160]; // "ERR bad header: got" and 32 bytes in hex fit
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  // paced, no flush(): HWCDC's flush() drops the whole TX ring after 100 ms without progress (serial_out.h)
  serial_out_printf("TAPP %s\n", line);
}

#define KEEP_TAIL 8 // bytes of an over-long line kept (the start of "TAPP " may be among them)

// A line from the port into `line`; false on a cancel or after `idle_ms` without a byte
static bool read_line(char *line, size_t cap, uint32_t idle_ms, bool (*cancel)(void)) {
  size_t len = 0;
  uint32_t last = millis();
  while (true) {
    int b = Serial.read();
    if (b < 0) {
      if (cancel && cancel()) return false;
      if (millis() - last >= idle_ms) return false;
      delay(5);
      continue;
    }
    last = millis();
    if (b == '\r') continue;
    if (b == '\n') {
      line[len] = '\0';
      return true;
    }
    if (len + 1 >= cap) { // a line too long to be a header (noise): keep the last bytes, a header may follow it
      memmove(line, line + len - KEEP_TAIL, KEEP_TAIL);
      len = KEEP_TAIL;
    }
    line[len++] = (char)b;
  }
}

app_upload_result_t apps_parse_header(const char *header, app_upload_header_t *h, char *reason, size_t reason_len) {
  reason[0] = '\0';
  app_upload_result_t r = app_upload_parse(header, UINT64_MAX, h); // the size first, the room for it next
  if (r == APP_UPLOAD_BAD_HEADER) app_upload_got_text(header, 32, reason, reason_len); // what it got
  if (r != APP_UPLOAD_OK) return r;
  const uint32_t size = h->size;
  int dropped = 0;
  uint64_t free_bytes = app_space_make_room(webshare_fs_root(), size, &dropped);
  if (dropped) Log_event("Apps: %d .bak copies removed to make room for %s", dropped, h->path);
  r = app_upload_parse(header, free_bytes, h);
  if (r == APP_UPLOAD_NO_SPACE) app_space_no_room_text(free_bytes, size, reason, reason_len);
  return r;
}

// The complete upload in APP_UPLOAD_TMP becomes the app the same way as an Install via Wi-Fi: app_install_commit
app_upload_result_t apps_install_tmp(const app_upload_header_t &h, char *reason, size_t reason_len) {
  uint8_t *buf = (uint8_t *)heap_caps_malloc(h.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  size_t free_mem = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  app_install_result_t rc = app_install_commit(webshare_fs_root(), APP_UPLOAD_TMP + 1, h.path + 1, h.size, buf,
                                               buf ? h.size : 0, free_mem, reason, reason_len);
  heap_caps_free(buf);
  if (rc == APP_INSTALL_OK) return APP_UPLOAD_OK;
  return rc == APP_INSTALL_REFUSED ? APP_UPLOAD_REFUSED : rc == APP_INSTALL_NO_SPACE ? APP_UPLOAD_NO_SPACE
                                                                                      : APP_UPLOAD_WRITE_FAILED;
}

// The data of one upload into APP_UPLOAD_TMP, size and CRC checked; the old app is untouched. On a failure the
// temporary file is gone and the log says why
app_upload_result_t apps_receive_tmp(const app_upload_header_t &h) {
  LittleFS.remove(APP_UPLOAD_TMP);
  File f = LittleFS.open(APP_UPLOAD_TMP, "w");
  if (!f) return APP_UPLOAD_WRITE_FAILED;
  apps_reply("READY");
  static uint8_t buf[APP_UPLOAD_BLOCK];
  uint32_t got = 0, crc = 0, in_block = 0, last = millis(), tx_waiting = last;
  bool write_failed = false;
  while (got < h.size && !serial_hold_rx_gave_up(millis(), last, tx_waiting, APP_UPLOAD_IDLE_MS, APP_UPLOAD_STALL_MAX_MS)) {
    task_watch_x_feed(); // the loop receives while an app runs: ended by the upload's own idle and stall limits
    if (Serial.availableForWrite() < SERIAL_TX_RING_BYTES) tx_waiting = millis(); // our ACK not out yet
    int avail = Serial.available();
    if (avail <= 0) {
      delay(1);
      continue;
    }
    size_t want = std::min<size_t>({(size_t)avail, h.size - got, APP_UPLOAD_BLOCK - in_block});
    size_t n = Serial.read(buf, want);
    if (!n) continue;
    last = millis();
    if (!write_failed && f.write(buf, n) != n) write_failed = true; // keeps reading: the host gets its reply
    crc = trmnl_crc32(crc, buf, n);
    got += n;
    in_block += n;
    if (in_block == APP_UPLOAD_BLOCK && got < h.size) {
      apps_reply("ACK %lu", (unsigned long)got);
      in_block = 0;
    }
  }
  f.close();
  app_upload_result_t r = write_failed ? APP_UPLOAD_WRITE_FAILED : app_upload_check(&h, got, crc);
  if (r != APP_UPLOAD_OK) {
    LittleFS.remove(APP_UPLOAD_TMP);
    Log_error("Apps: upload of %s failed: %s (%lu of %lu bytes)", h.path, app_upload_result_str(r), (unsigned long)got,
              (unsigned long)h.size);
  }
  return r;
}

// Menu mode: the data, then installed. `reason`: why the ELF check refused it
static app_upload_result_t receive_data(const app_upload_header_t &h, char *reason, size_t reason_len) {
  reason[0] = '\0';
  app_upload_result_t r = apps_receive_tmp(h);
  if (r != APP_UPLOAD_OK) return r;
  r = apps_install_tmp(h, reason, reason_len);
  if (r != APP_UPLOAD_OK) Log_error("Apps: install of %s failed: %s %s", h.path, app_upload_result_str(r), reason);
  return r;
}

int apps_receive(bool (*cancel)(void)) {
  int received = 0;
  Serial.begin(115200);
  if (!LittleFS.exists(APPS_DIR)) LittleFS.mkdir(APPS_DIR);
  LittleFS.remove(APP_UPLOAD_TMP); // left by a restart during an upload
  Log_info("Apps: waiting for an upload on the USB serial port");
  char line[APP_UPLOAD_LINE_MAX];
  while (read_line(line, sizeof(line), RECEIVE_IDLE_MS, cancel)) {
    if (!line[0]) continue;
    const char *header = app_upload_find_header(line);
    char got[APP_UPLOAD_LINE_MAX];
    app_upload_got_text(line, 32, got, sizeof(got));
    if (!header) { // noise, not a header: no reply (the host would take it for one), the log says what it was
      Log_info("Apps: ignored a line without \"TAPP \": %s", got);
      continue;
    }
    if (header != line) Log_info("Apps: %u bytes before \"TAPP \" dropped: %s", (unsigned)(header - line), got);
    app_upload_header_t h;
    char reason[112] = "";
    serial_hold_set(true); // no log text between the replies (serial_hold.h); ends after the last one
    app_upload_result_t r = apps_parse_header(header, &h, reason, sizeof(reason));
    if (r == APP_UPLOAD_OK) r = receive_data(h, reason, sizeof(reason));
    if (r != APP_UPLOAD_OK) {
      Log_info("Apps: upload refused: %s%s%s", app_upload_result_str(r), reason[0] ? ": " : "", reason);
      apps_reply("ERR %s%s%s", app_upload_result_str(r), reason[0] ? ": " : "", reason);
      serial_hold_set(false);
      continue;
    }
    received++;
    apps_reply("OK %s", h.path + sizeof(APPS_DIR));
    serial_hold_set(false);
    Log_info("Apps: received %s, %lu bytes", h.path, (unsigned long)h.size);
  }
  Log_info("Apps: upload mode ended, %d received", received);
  return received;
}

#endif // BOARD_TRMNL_X
