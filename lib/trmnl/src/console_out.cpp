#include "console_out.h"

#include <string.h>

void console_out_init(console_out_t *c) { memset(c, 0, sizeof(*c)); }

static void keep(console_out_t *c, const uint8_t *b, size_t n) {
  if (n >= CONSOLE_OUT_BYTES) { // only its newest bytes fit
    c->dropped += (uint32_t)(c->len + n - CONSOLE_OUT_BYTES);
    memcpy(c->buf, b + n - CONSOLE_OUT_BYTES, CONSOLE_OUT_BYTES);
    c->len = CONSOLE_OUT_BYTES;
    return;
  }
  if (c->len + n > CONSOLE_OUT_BYTES) { // the oldest go
    size_t drop = c->len + n - CONSOLE_OUT_BYTES;
    memmove(c->buf, c->buf + drop, c->len - drop);
    c->len -= drop;
    c->dropped += (uint32_t)drop;
  }
  memcpy(c->buf + c->len, b, n);
  c->len += n;
}

// One move into the ring: all that waits when it fits, else up to the last line end that fits (a reply written in
// between does not land inside a log line), else nothing (room comes) unless the first line is too long to wait for
static void move(console_out_t *c, const serial_out_port_t *p) {
  while (c->len) {
    int room = p->room(p->ctx);
    if (room <= 0) return;
    size_t k = (size_t)room < c->len ? (size_t)room : c->len;
    if (k < c->len) {
      size_t nl = k;
      while (nl && c->buf[nl - 1] != '\n') nl--;
      if (nl) k = nl;
      else if (!memchr(c->buf, '\n', c->len < CONSOLE_OUT_LINE_MAX ? c->len : CONSOLE_OUT_LINE_MAX)) {
        // a line longer than CONSOLE_OUT_LINE_MAX (or one not ended yet that long): in parts
      } else return;
    }
    size_t took = p->write(p->ctx, c->buf, k);
    if (took > k) took = k;
    if (!took) return;
    memmove(c->buf, c->buf + took, c->len - took);
    c->len -= took;
  }
}

bool console_out_pump(console_out_t *c, const serial_out_port_t *p, bool reader, bool held) {
  if (reader && !held) move(c, p);
  if (!c->len) c->stalled = false;
  return !c->len;
}

size_t console_out_write(console_out_t *c, const serial_out_port_t *p, bool reader, bool held, const uint8_t *b,
                         size_t n) {
  if (!n) return 0;
  keep(c, b, n);
  if (!reader || held) return n;
  uint32_t since = p->now_ms(p->ctx);
  while (!console_out_pump(c, p, reader, held)) {
    if (c->stalled || (uint32_t)(p->now_ms(p->ctx) - since) >= CONSOLE_OUT_WAIT_MS) {
      c->stalled = true;
      break;
    }
    p->wait_ms(p->ctx, 1);
  }
  return n;
}

uint32_t console_out_dropped_take(console_out_t *c) {
  if (c->len) return 0;
  uint32_t d = c->dropped;
  c->dropped = 0;
  return d;
}
