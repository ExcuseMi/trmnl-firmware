#include "app_meta.h"

#include <ctype.h>
#include <string.h>

// ELF32 little-endian offsets (the layout app_elf_check.cpp reads in full)
#define EH_SHOFF 32
#define EH_SHENTSIZE 46
#define EH_SHNUM 48
#define EH_SHSTRNDX 50
#define EH_SIZE 52
#define SH_SIZE 40
#define SH_NAME 0
#define SH_TYPE 4
#define SH_OFFSET 16
#define SH_SECSIZE 20
#define SHT_PROGBITS_ 1
#define SHNUM_MAX 96 // sections read at most; an app has about 20

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static bool in_file(uint32_t off, uint32_t size, uint32_t len) { return off <= len && size <= len - off; }

void app_meta_fallback_name(const char *file_name, char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  size_t n = 0;
  for (; file_name && file_name[n] && n + 1 < out_len; n++) {
    char c = file_name[n];
    out[n] = (c == '_' || c == '-') ? ' ' : c;
  }
  out[n] = '\0';
  if (n) out[0] = (char)toupper((unsigned char)out[0]);
}

char app_meta_letter(const char *name) {
  for (; name && *name; name++) {
    if (isalnum((unsigned char)*name)) return (char)toupper((unsigned char)*name);
  }
  return '?';
}

// The printable part of a name, 31 characters at most; false if nothing is left
static bool clean_name(const char *in, size_t in_max, char *out) {
  size_t n = 0;
  for (size_t i = 0; i < in_max && in[i] && n < 31; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c >= 0x20 && c != 0x7f) out[n++] = (char)c; // UTF-8 bytes (>= 0x80) pass as they are
  }
  while (n && out[n - 1] == ' ') n--;
  out[n] = '\0';
  size_t s = 0;
  while (out[s] == ' ') s++;
  if (s) memmove(out, out + s, n - s + 1);
  return out[0] != '\0';
}

void app_meta_read(app_meta_read_t read, void *ctx, uint32_t file_len, const char *fallback, app_meta_t *out) {
  memset(out, 0, sizeof(*out));
  app_meta_fallback_name(fallback, out->name, sizeof(out->name));
  if (out->name[0] == '\0') strcpy(out->name, "App");
  uint8_t eh[EH_SIZE];
  if (!read || file_len < EH_SIZE || read(ctx, 0, eh, EH_SIZE) != 0) return;
  if (memcmp(eh, "\x7f" "ELF", 4) != 0 || eh[4] != 1 || eh[5] != 1) return;
  const uint32_t shoff = le32(eh + EH_SHOFF);
  const uint16_t shnum = le16(eh + EH_SHNUM), shstrndx = le16(eh + EH_SHSTRNDX);
  if (le16(eh + EH_SHENTSIZE) != SH_SIZE || shnum == 0 || shnum > SHNUM_MAX || shstrndx >= shnum) return;
  if (!in_file(shoff, (uint32_t)shnum * SH_SIZE, file_len)) return;
  uint8_t sh[SH_SIZE];
  if (read(ctx, shoff + (uint32_t)shstrndx * SH_SIZE, sh, SH_SIZE) != 0) return;
  const uint32_t names_off = le32(sh + SH_OFFSET), names_size = le32(sh + SH_SECSIZE);
  if (!in_file(names_off, names_size, file_len)) return;

  bool info_done = false, icon_done = false;
  for (uint16_t i = 0; i < shnum && !(info_done && icon_done); i++) {
    if (read(ctx, shoff + (uint32_t)i * SH_SIZE, sh, SH_SIZE) != 0) return;
    const uint32_t name_at = le32(sh + SH_NAME), type = le32(sh + SH_TYPE);
    const uint32_t off = le32(sh + SH_OFFSET), size = le32(sh + SH_SECSIZE);
    if (name_at >= names_size || type != SHT_PROGBITS_ || !in_file(off, size, file_len)) continue;
    char sec[sizeof(APP_ICON_SECTION) + 1] = {};
    size_t want = names_size - name_at < sizeof(sec) - 1 ? names_size - name_at : sizeof(sec) - 1;
    if (read(ctx, names_off + name_at, sec, want) != 0) continue;
    if (!info_done && strcmp(sec, APP_INFO_SECTION) == 0) {
      info_done = true; // the last of a name would win in app_elf_check; an app has one
      app_info_t info;
      if (size < sizeof(info) || read(ctx, off, &info, sizeof(info)) != 0) continue;
      if (info.magic != APP_INFO_MAGIC) continue;
      char name[32];
      if (clean_name(info.name, sizeof(info.name), name)) memcpy(out->name, name, sizeof(name));
    } else if (!icon_done && strcmp(sec, APP_ICON_SECTION) == 0) {
      icon_done = true;
      if (size == APP_ICON_BYTES && read(ctx, off, out->icon, APP_ICON_BYTES) == 0) out->has_icon = true;
    }
  }
}
