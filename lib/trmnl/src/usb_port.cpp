#include "usb_port.h"

#include <string.h>

void usb_roles_init(usb_roles_t *r) { r->role = USB_ROLE_SERIAL; }

bool usb_role_take(usb_roles_t *r, usb_role_t want) {
  if (want == USB_ROLE_SERIAL) return false;
  if (r->role != USB_ROLE_SERIAL && r->role != want) return false;
  r->role = want;
  return true;
}

bool usb_role_give(usb_roles_t *r, usb_role_t had) {
  if (had == USB_ROLE_SERIAL || r->role != had) return false;
  r->role = USB_ROLE_SERIAL;
  return true;
}

const char *usb_role_str(usb_role_t role) {
  switch (role) {
  case USB_ROLE_SERIAL: return "serial";
  case USB_ROLE_HOST: return "USB host";
  case USB_ROLE_DRIVE: return "USB drive";
  }
  return "?";
}

void usb_port_init(usb_port_t *p) {
  memset(p, 0, sizeof(*p));
  p->mode = USB_PORT_DEVICE;
  p->why = USB_WHY_NOT_WANTED;
}

static usb_port_mode_t device(usb_port_t *p, usb_port_why_t why) {
  p->mode = USB_PORT_DEVICE;
  p->why = why;
  p->boost = false;
  return p->mode;
}

static usb_port_mode_t host(usb_port_t *p, uint32_t now, bool boost, usb_port_why_t why) {
  p->mode = USB_PORT_HOST;
  p->boost = boost;
  p->why = why;
  p->host_since = now;
  return p->mode;
}

// Outside power (or an unreadable charger): never the boost; one host try with it off per outside power and app ask
static usb_port_mode_t step_powered(usb_port_t *p, uint32_t now, const usb_port_input_t *in) {
  usb_port_why_t why = in->power == USB_POWER_YES ? USB_WHY_INPUT_POWER : USB_WHY_POWER_UNKNOWN;
  p->clear = false;
  p->retry_wait = false; // the next time on battery is a new chance
  if (!p->powered) {
    p->powered = true;
    p->powered_since = now;
  }
  if (!in->wanted) {
    p->outside_tried = false; // the app's next ask is a new try
    return device(p, why);
  }
  if (in->port_taken) return device(p, why);
  if (p->mode == USB_PORT_HOST && !p->boost) {
    if (in->keyboard) {
      p->why = USB_WHY_OUTSIDE_DEVICE;
      return p->mode;
    }
    if (p->why == USB_WHY_OUTSIDE_DEVICE || (uint32_t)(now - p->host_since) >= USB_PORT_OUTSIDE_PROBE_MS)
      return device(p, USB_WHY_OUTSIDE_NONE); // a computer, or the device left: serial again
    return p->mode;
  }
  if (p->mode == USB_PORT_HOST) return device(p, why); // the boost is on: off in this step, never against outside 5 V
  if (p->outside_tried) return device(p, USB_WHY_OUTSIDE_NONE);
  if ((uint32_t)(now - p->powered_since) < USB_PORT_CLEAR_MS) return device(p, why);
  p->outside_tried = true;
  return host(p, now, false, USB_WHY_OUTSIDE_PROBING);
}

usb_port_mode_t usb_port_step(usb_port_t *p, uint32_t now, const usb_port_input_t *in) {
  if (in->power != USB_POWER_NO) return step_powered(p, now, in);
  p->powered = false;
  p->outside_tried = false; // the outside power went away: the next one is a new try
  if (!p->clear) {
    p->clear = true;
    p->clear_since = now;
  }
  if (!in->wanted) {
    p->retry_wait = false;
    return device(p, USB_WHY_NOT_WANTED);
  }
  if (in->port_taken) return device(p, USB_WHY_PORT_TAKEN); // never pull the PHY from the drive
  if (p->mode == USB_PORT_HOST && !p->boost) return device(p, USB_WHY_WAIT_CLEAR); // outside power gone: boost later
  if (p->mode == USB_PORT_HOST) {
    if (in->keyboard) {
      p->why = USB_WHY_KEYBOARD;
      return p->mode;
    }
    if (p->why == USB_WHY_KEYBOARD) { // removed: host mode stays for it to enumerate again (a tablet that dropped off)
      p->why = USB_WHY_REJOIN;
      p->gone_since = now;
      return p->mode;
    }
    // while the app wants the port: no stop and start of the stack (usb_port.h USB_WHY_REJOIN)
    if (p->why == USB_WHY_REJOIN) return p->mode;
    if ((uint32_t)(now - p->host_since) >= USB_PORT_PROBE_MS) { // the probe found none
      p->retry_wait = true;
      p->retry_at = now + USB_PORT_RETRY_MS;
      return device(p, USB_WHY_NO_KEYBOARD);
    }
    return p->mode; // probing
  }
  if ((uint32_t)(now - p->clear_since) < USB_PORT_CLEAR_MS) return device(p, USB_WHY_WAIT_CLEAR);
  if (p->retry_wait && (int32_t)(now - p->retry_at) < 0) return device(p, USB_WHY_NO_KEYBOARD);
  p->retry_wait = false;
  return host(p, now, true, USB_WHY_PROBING);
}

const char *usb_port_why_str(usb_port_why_t why) {
  switch (why) {
  case USB_WHY_NOT_WANTED: return "no app wants USB keys";
  case USB_WHY_INPUT_POWER: return "input power: device mode";
  case USB_WHY_POWER_UNKNOWN: return "charger unreadable: device mode";
  case USB_WHY_WAIT_CLEAR: return "no input power, checking";
  case USB_WHY_PROBING: return "host mode: looking for a keyboard";
  case USB_WHY_KEYBOARD: return "host mode: keyboard";
  case USB_WHY_NO_KEYBOARD: return "no keyboard: device mode, trying again later";
  case USB_WHY_REJOIN: return "host mode: the device left, waiting for it to come back";
  case USB_WHY_PORT_TAKEN: return "the USB drive has the port";
  case USB_WHY_OUTSIDE_PROBING: return "host with outside power (boost off): looking for a device";
  case USB_WHY_OUTSIDE_DEVICE: return "host with outside power (boost off): device";
  case USB_WHY_OUTSIDE_NONE: return "no device, back to serial (until the outside power goes away)";
  }
  return "?";
}

void usb_power_reading_init(usb_power_reading_t *r) {
  r->last = USB_POWER_UNKNOWN;
  r->at = 0;
}

usb_power_t usb_power_update(usb_power_reading_t *r, uint32_t now, bool got, bool input_power) {
  if (got) {
    r->last = input_power ? USB_POWER_YES : USB_POWER_NO;
    r->at = now;
  } else if ((uint32_t)(now - r->at) > USB_POWER_STALE_MS) {
    r->last = USB_POWER_UNKNOWN;
  }
  return r->last;
}

uint32_t usb_power_lock_wait_ms(const usb_power_reading_t *r, uint32_t now) {
  return (uint32_t)(now - r->at) >= USB_POWER_WAIT_AFTER_MS ? USB_POWER_LOCK_WAIT_MS : 0;
}

// ---- the boost's switch-on ----

void usb_boost_init(usb_boost_t *b, uint32_t now_ms) {
  b->on = false;
  b->tries = 0;
  b->next_at = now_ms;
}

bool usb_boost_due(const usb_boost_t *b, uint32_t now_ms) { return !b->on && (int32_t)(now_ms - b->next_at) >= 0; }

usb_boost_log_t usb_boost_result(usb_boost_t *b, uint32_t now_ms, bool ok) {
  if (ok) {
    b->on = true;
    return b->tries ? USB_BOOST_LOG_ON_LATE : USB_BOOST_LOG_NONE;
  }
  if (b->tries != 0xFF) b->tries++;
  b->next_at = now_ms + (b->tries < USB_BOOST_FAST_TRIES ? USB_BOOST_FAST_MS : USB_BOOST_SLOW_MS);
  return b->tries == USB_BOOST_FAST_TRIES ? USB_BOOST_LOG_FAILED : USB_BOOST_LOG_NONE;
}

const usb_stop_step_t usb_stop_plan[USB_STOP_STEPS] = {USB_STOP_CLOSE_HANDLES, USB_STOP_PORT_OFF, USB_STOP_CLIENTS_OFF,
                                                        USB_STOP_WAIT_GONE,     USB_STOP_FREE_DEVICES, USB_STOP_HOST_OFF,
                                                        USB_STOP_BOOST_OFF};
