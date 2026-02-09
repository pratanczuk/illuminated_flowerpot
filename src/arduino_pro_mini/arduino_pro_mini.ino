#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <string.h>
#include <ctype.h>

#define PIN 6
#define MAX_LEDS 120
#define DEFAULT_LEDS 40

enum ProfileId : uint8_t {
  PROFILE_CUSTOM = 0,
  PROFILE_WARM = 1,
  PROFILE_PLANT = 2
};

struct Settings {
  uint16_t magic;
  uint8_t version;
  uint8_t led_count;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t brightness;
  uint8_t day_enabled;
  uint16_t day_start_min;
  uint16_t day_end_min;
  uint8_t day_profile;
  uint8_t night_profile;
  uint16_t boot_time_min;
  uint8_t checksum;
};

static const uint16_t SETTINGS_MAGIC = 0xA5C3;
static const uint8_t SETTINGS_VERSION = 1;

Adafruit_NeoPixel strip(1, PIN, NEO_GRB + NEO_KHZ800);
Settings settings;
char input_buf[80];
uint8_t input_len = 0;
unsigned long last_tick_ms = 0;

static uint8_t checksum_settings(const Settings &s) {
  const uint8_t *p = (const uint8_t *)&s;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(Settings) - 1; i++) {
    sum ^= p[i];
  }
  return sum;
}

static void set_defaults(Settings &s) {
  s.magic = SETTINGS_MAGIC;
  s.version = SETTINGS_VERSION;
  s.led_count = DEFAULT_LEDS;
  s.r = 255;
  s.g = 160;
  s.b = 60;   // warm white
  s.brightness = 255;
  s.day_enabled = 1;
  s.day_start_min = 7 * 60;
  s.day_end_min = 21 * 60;
  s.day_profile = PROFILE_WARM;
  s.night_profile = PROFILE_CUSTOM;
  s.boot_time_min = 0;
  s.checksum = checksum_settings(s);
}

static bool load_settings(Settings &s) {
  EEPROM.get(0, s);
  if (s.magic != SETTINGS_MAGIC || s.version != SETTINGS_VERSION) {
    return false;
  }
  return s.checksum == checksum_settings(s);
}

static void save_settings(Settings &s) {
  s.checksum = checksum_settings(s);
  EEPROM.put(0, s);
}

static void ensure_led_count(uint8_t count) {
  if (count < 1) count = 1;
  if (count > MAX_LEDS) count = MAX_LEDS;
  strip.updateLength(count);
  strip.begin();
  strip.show();
}

static void apply_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  uint16_t rr = (uint16_t)r * brightness / 255;
  uint16_t gg = (uint16_t)g * brightness / 255;
  uint16_t bb = (uint16_t)b * brightness / 255;
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(rr, gg, bb));
  }
  strip.show();
}

static void profile_color(ProfileId profile, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (profile == PROFILE_WARM) {
    r = 255; g = 160; b = 60;
  } else if (profile == PROFILE_PLANT) {
    r = 255; g = 0; b = 80;
  } else {
    r = settings.r; g = settings.g; b = settings.b;
  }
}

static uint16_t current_minutes() {
  unsigned long minutes = (millis() / 60000UL) % 1440UL;
  return (settings.boot_time_min + minutes) % 1440;
}

static bool is_daytime() {
  if (!settings.day_enabled) return true;
  uint16_t now = current_minutes();
  if (settings.day_start_min == settings.day_end_min) return true;
  if (settings.day_start_min < settings.day_end_min) {
    return now >= settings.day_start_min && now < settings.day_end_min;
  }
  return now >= settings.day_start_min || now < settings.day_end_min;
}

static void apply_output() {
  ProfileId profile = is_daytime() ? (ProfileId)settings.day_profile
                                   : (ProfileId)settings.night_profile;
  uint8_t r, g, b;
  profile_color(profile, r, g, b);
  apply_color(r, g, b, settings.brightness);
}

static void print_help() {
  Serial.println(F("Commands:"));
  Serial.println(F("  HELP"));
  Serial.println(F("  SHOW"));
  Serial.println(F("  LEDS <1-120>"));
  Serial.println(F("  COLOR <r> <g> <b>"));
  Serial.println(F("  BRIGHT <0-255>"));
  Serial.println(F("  PROFILE <CUSTOM|WARM|PLANT>"));
  Serial.println(F("  DAYON | DAYOFF"));
  Serial.println(F("  DAYSTART <HH:MM>"));
  Serial.println(F("  DAYEND <HH:MM>"));
  Serial.println(F("  DAYPROFILE <CUSTOM|WARM|PLANT>"));
  Serial.println(F("  NIGHTPROFILE <CUSTOM|WARM|PLANT>"));
  Serial.println(F("  NOW <HH:MM>"));
  Serial.println(F("  SAVE"));
  Serial.println(F("  DEFAULTS"));
}

static bool parse_time(const char *s, uint16_t &out_min) {
  int h = -1, m = -1;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return false;
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  out_min = (uint16_t)(h * 60 + m);
  return true;
}

static bool equals_ignore_case(const char *a, const char *b) {
  while (*a && *b) {
    char ca = tolower(*a++);
    char cb = tolower(*b++);
    if (ca != cb) return false;
  }
  return *a == '\0' && *b == '\0';
}

static ProfileId parse_profile(const char *s, bool &ok) {
  ok = true;
  if (equals_ignore_case(s, "CUSTOM")) return PROFILE_CUSTOM;
  if (equals_ignore_case(s, "WARM")) return PROFILE_WARM;
  if (equals_ignore_case(s, "PLANT")) return PROFILE_PLANT;
  ok = false;
  return PROFILE_CUSTOM;
}

static void show_settings() {
  Serial.println(F("Settings:"));
  Serial.print(F("  LEDs: ")); Serial.println(settings.led_count);
  Serial.print(F("  Color: ")); Serial.print(settings.r); Serial.print(',');
  Serial.print(settings.g); Serial.print(','); Serial.println(settings.b);
  Serial.print(F("  Brightness: ")); Serial.println(settings.brightness);
  Serial.print(F("  Day enabled: ")); Serial.println(settings.day_enabled ? "yes" : "no");
  Serial.print(F("  Day start: ")); Serial.print(settings.day_start_min / 60);
  Serial.print(':'); Serial.println(settings.day_start_min % 60);
  Serial.print(F("  Day end: ")); Serial.print(settings.day_end_min / 60);
  Serial.print(':'); Serial.println(settings.day_end_min % 60);
  Serial.print(F("  Day profile: ")); Serial.println(settings.day_profile);
  Serial.print(F("  Night profile: ")); Serial.println(settings.night_profile);
  Serial.print(F("  Now: ")); Serial.println(current_minutes());
}

static void handle_command(char *line) {
  char *cmd = strtok(line, " \t");
  if (!cmd) return;

  if (equals_ignore_case(cmd, "HELP")) {
    print_help();
    return;
  }

  if (equals_ignore_case(cmd, "SHOW")) {
    show_settings();
    return;
  }

  if (equals_ignore_case(cmd, "LEDS")) {
    char *arg = strtok(NULL, " \t");
    if (!arg) return;
    int v = atoi(arg);
    if (v < 1) v = 1;
    if (v > MAX_LEDS) v = MAX_LEDS;
    settings.led_count = (uint8_t)v;
    ensure_led_count(settings.led_count);
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "COLOR")) {
    char *a = strtok(NULL, " \t");
    char *b = strtok(NULL, " \t");
    char *c = strtok(NULL, " \t");
    if (!a || !b || !c) return;
    settings.r = (uint8_t)constrain(atoi(a), 0, 255);
    settings.g = (uint8_t)constrain(atoi(b), 0, 255);
    settings.b = (uint8_t)constrain(atoi(c), 0, 255);
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "BRIGHT")) {
    char *a = strtok(NULL, " \t");
    if (!a) return;
    settings.brightness = (uint8_t)constrain(atoi(a), 0, 255);
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "PROFILE")) {
    char *a = strtok(NULL, " \t");
    if (!a) return;
    bool ok;
    ProfileId p = parse_profile(a, ok);
    if (!ok) return;
    settings.day_profile = p;
    settings.night_profile = p;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "DAYPROFILE")) {
    char *a = strtok(NULL, " \t");
    if (!a) return;
    bool ok;
    ProfileId p = parse_profile(a, ok);
    if (!ok) return;
    settings.day_profile = p;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "NIGHTPROFILE")) {
    char *a = strtok(NULL, " \t");
    if (!a) return;
    bool ok;
    ProfileId p = parse_profile(a, ok);
    if (!ok) return;
    settings.night_profile = p;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "DAYON")) {
    settings.day_enabled = 1;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "DAYOFF")) {
    settings.day_enabled = 0;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "DAYSTART")) {
    char *a = strtok(NULL, " \t");
    if (!a) return;
    uint16_t mins;
    if (!parse_time(a, mins)) return;
    settings.day_start_min = mins;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "DAYEND")) {
    char *a = strtok(NULL, " \t");
    if (!a) return;
    uint16_t mins;
    if (!parse_time(a, mins)) return;
    settings.day_end_min = mins;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "NOW")) {
    char *a = strtok(NULL, " \t");
    if (!a) return;
    uint16_t mins;
    if (!parse_time(a, mins)) return;
    settings.boot_time_min = mins;
    apply_output();
    return;
  }

  if (equals_ignore_case(cmd, "SAVE")) {
    save_settings(settings);
    Serial.println(F("Saved to EEPROM"));
    return;
  }

  if (equals_ignore_case(cmd, "DEFAULTS")) {
    set_defaults(settings);
    ensure_led_count(settings.led_count);
    apply_output();
    Serial.println(F("Defaults loaded"));
    return;
  }
}

void setup() {
  Serial.begin(115200);
  if (!load_settings(settings)) {
    set_defaults(settings);
    save_settings(settings);
  }
  ensure_led_count(settings.led_count);
  apply_output();
  Serial.println(F("LED controller ready. Type HELP"));
}

void loop() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      input_buf[input_len] = '\0';
      handle_command(input_buf);
      input_len = 0;
    } else if (input_len < sizeof(input_buf) - 1) {
      input_buf[input_len++] = ch;
    }
  }

  unsigned long now = millis();
  if (now - last_tick_ms >= 1000) {
    last_tick_ms = now;
    apply_output();
  }
}
