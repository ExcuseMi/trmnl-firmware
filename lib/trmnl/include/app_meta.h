#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <app_api.h>

// What the Apps menu shows of an app without loading it: the display name (app_info) and the optional 64x64 icon
// (APP_ICON, docs/apps.md), read from the ELF file through a reader, a few small bounded reads. Anything wrong with
// a section is ignored: the name falls back to the file's name, the icon to none (the menu draws the first letter).
typedef struct {
  char name[32]; // display name, printable only; never empty
  bool has_icon;
  uint8_t icon[APP_ICON_BYTES];
} app_meta_t;

// Reads `len` bytes at `off` into buf; 0 on success
typedef int (*app_meta_read_t)(void *ctx, uint32_t off, void *buf, size_t len);

// `file_len`: the file's size. `fallback`: the file's name without ".elf" ("my_app"). Always fills *out.
void app_meta_read(app_meta_read_t read, void *ctx, uint32_t file_len, const char *fallback, app_meta_t *out);

// The name made from a file name: first letter capital, '_' and '-' as spaces ("my_app" -> "My app")
void app_meta_fallback_name(const char *file_name, char *out, size_t out_len);

// The letter the generic icon shows: the first letter or digit of the name, upper case; '?' if none
char app_meta_letter(const char *name);
