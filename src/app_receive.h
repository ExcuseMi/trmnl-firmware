#pragma once
#ifdef BOARD_TRMNL_X

#include <app_upload.h>

// The pieces of receiving an app over the USB serial port (app_receive.cpp), shared by the Receive over USB screen and
// the cable update (app_cable_x.cpp)

// "TAPP <text>\n" on the port
void apps_reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// After "TAPP PUT" was parsed: READY, the bytes into APP_UPLOAD_TMP with ACKs, size and CRC checked. The old app is
// untouched; a failure removes the temp file
app_upload_result_t apps_receive_tmp(const app_upload_header_t &h);
// The complete APP_UPLOAD_TMP becomes /apps/<name>.elf (ELF check, .bak, rename; app_install_commit)
app_upload_result_t apps_install_tmp(const app_upload_header_t &h, char *reason, size_t reason_len);

#endif
