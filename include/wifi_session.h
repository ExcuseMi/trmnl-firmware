#pragma once

/**
 * Boot-time Wi-Fi session: hostname, optional X modem prep, connect with
 * saved credentials, or captive portal.
 *
 * Call order from bl_init:
 *   1. wifiSessionInit()     — early (before SF_ADD_WIFI / portal use)
 *   2. wifiSessionConnect()  — after display/filesystem/battery init
 *
 * On connection failure, wifiSessionConnect shows WIFI_FAILED (saved credentials: only
 * when should_show_error_now) and deep-sleeps
 * via wifiErrorDeepSleep (does not return on that path).
 */

/** Set captive-portal hostname from preferences / friendly ID. */
void wifiSessionInit(void);

/**
 * Prepare modem (X), set STA mode, then auto-connect or start captive portal.
 * Blocks until connected or sleep is entered on failure.
 * @param should_show_error_now show WIFI_FAILED right away when saved credentials fail
 *        (false on timer wakes: the quiet retries leave the current image up)
 */
void wifiSessionConnect(bool should_show_error_now);

#ifdef BOARD_TRMNL_X
class Modem;
/**
 * The ESP32-C5 modem for an app's net_up on a saved 5 GHz network: the one TRMNL's session set up, or set up now
 * (once per boot). nullptr when no 5 GHz network is saved or the modem does not answer.
 */
Modem *wifiSessionModem(void);
#endif
