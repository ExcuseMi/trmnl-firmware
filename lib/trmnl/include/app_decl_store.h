#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_settings_form.h"

// Each app's settings declaration as a small firmware file (docs/apps.md "The X's page"), so the phone page can show
// an app's form while the app does not run. The firmware writes it when the app declares (settings_declare) and the
// text differs; the page reads it back through the same checks as a declaration (asf_declare), so a damaged or
// hand-edited file can never put a field on the page that an app could not declare. Pure: no I/O.

#define ADS_FORMAT 1
#define ADS_DIR "/xset/decl"
// one line per field: key, kind, min, max, label, choices, tabs between; plus the format line
#define ADS_FILE_MAX (32 + ASF_FIELDS_MAX * (ASF_KEY_MAX + ASF_LABEL_MAX + ASF_CHOICES_BYTES + 32))

// The form as the file's text into out: the bytes written, 0 when cap is too small (nothing usable written)
size_t ads_encode(const asf_form_t *form, char *out, size_t cap);
// The file's text (len bytes) back into *out. scratch: a second form the parser uses (big, so the caller's).
// 0; APP_ENOTSUP: another format version (written by other firmware: the app declares again); APP_EINVAL: a damaged
// file or one asf_declare refuses. *out is cleared on an error
int ads_decode(const char *buf, size_t len, asf_form_t *out, asf_form_t *scratch);
// "/xset/decl/<app>.txt" for an app name of letters, digits, '-' and '_' (1..24); false for any other name
bool ads_path(const char *app, char *out, size_t cap);
