// aux1_protocol.h
//
// Flymo 1200R / Husqvarna Automower (G3 platform) AUX1 UART helpers for
// ESPHome lambdas. Included via `esphome: includes:` in automower.yaml.
//
// Frame:  STX(0x02) | CMD | LEN | DATA[LEN] | CRC8 | ETX(0x03)
// CRC8:   Dallas/1-Wire (reflected poly 0x8C, init 0x00) over CMD..DATA
// Reply:  CMD+1, DATA[0] = status byte, payload follows (resp[4] onward)
//
// Protocol reference: docs/aux1_protocol.md in this repo.

#pragma once
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace aux1 {

static const char *const TAG = "aux1";

// ---------------------------------------------------------------------------
// Traffic gate
// ---------------------------------------------------------------------------
// AUX1 traffic is only allowed when ALL of these are true:
//   * AUX1 is powered (pin 5 sense on GPIO4 is high) - no point talking to a
//     mower in standby.
//   * At least STARTUP_DELAY_MS has passed since AUX1 power came up. The
//     ESP32 is battery-fed and does NOT reboot with the mower, so the
//     hold-off is timed from the AUX1 power edge, not ESP32 boot. This keeps
//     traffic away from the first-boot wizard after a battery swap / reset
//     (AUX1 traffic during the wizard has corrupted it before).
//   * Polling is enabled (the "AUX1 Polling" switch). It is turned off
//     automatically on Tools/EXP menu entry - AUX1 traffic while that menu
//     is open causes spurious status 0x03 replies and PIN re-prompts.
// update_gate() is called once a second from automower.yaml.
static const uint32_t STARTUP_DELAY_MS = 20000;

static bool g_awake = false;
static bool g_enabled = true;
static uint32_t g_awake_since = 0;

inline void update_gate(bool awake, bool enabled) {
  if (awake && !g_awake) {
    g_awake_since = millis();
    ESP_LOGI(TAG, "AUX1 powered - holding off traffic for %us", (unsigned) (STARTUP_DELAY_MS / 1000));
  } else if (!awake && g_awake) {
    ESP_LOGI(TAG, "AUX1 unpowered (mower standby) - traffic suspended");
  }
  if (enabled != g_enabled) {
    ESP_LOGI(TAG, "AUX1 polling %s", enabled ? "enabled" : "paused");
  }
  g_awake = awake;
  g_enabled = enabled;
}

inline bool awake() { return g_awake; }

inline bool ready() {
  return g_enabled && g_awake && (millis() - g_awake_since >= STARTUP_DELAY_MS);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
inline uint8_t crc8_dallas(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x01) ? (crc >> 1) ^ 0x8C : crc >> 1;
  }
  return crc;
}

inline std::string hex_dump(const uint8_t *data, size_t len) {
  char buf[4];
  std::string s;
  for (size_t i = 0; i < len; i++) {
    snprintf(buf, sizeof(buf), "%02X ", data[i]);
    s += buf;
  }
  return s;
}

// Publish only on change, so a docked mower isn't sending the same values to
// HA every poll. Numeric sensors compare against raw_state (the value before
// filters, e.g. tilt's x0.1), with an optional deadband for noisy analog
// values: publish only when the change is larger than the deadband.
// Binary sensors already deduplicate in ESPHome, so they don't need this.
inline void pub(esphome::sensor::Sensor *s, float v, float deadband = 0) {
  float last = s->get_raw_state();  // .raw_state is deprecated (removed in ESPHome 2026.10)
  if (std::isnan(last) || std::isnan(v) || std::fabs(v - last) > deadband) s->publish_state(v);
}
inline void pub(esphome::text_sensor::TextSensor *s, const std::string &v) {
  if (!s->has_state() || s->state != v) s->publish_state(v);
}

inline uint16_t le16(const uint8_t *p) { return (uint16_t) p[0] | ((uint16_t) p[1] << 8); }
inline int16_t le16s(const uint8_t *p) { return (int16_t) le16(p); }

// resp layout: [STX, CMD, LEN, STATUS, payload..., CRC, ETX]
inline uint8_t status_byte(const uint8_t *resp, size_t len) { return len >= 4 ? resp[3] : 0xFF; }
inline const uint8_t *payload(const uint8_t *resp) { return &resp[4]; }
// Payload bytes after the status byte (LEN counts the status byte too).
inline size_t payload_len(const uint8_t *resp, size_t len) {
  if (len < 4 || resp[2] < 1) return 0;
  size_t n = resp[2] - 1;
  return (4 + n <= len) ? n : 0;  // guard against truncated frames
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------
// Sends one frame and reads one reply. Returns bytes captured into resp (0 on
// gate closed / timeout / wrong reply CMD). resp must be >= 32 bytes.
//
// Logging: the routine polls (0x12 status, 0x14 sensors, 0x18 stats) log the
// TX/RX pair at DEBUG only when the reply differs from the last reply of the
// same kind (reply CMD + LEN, which also separates the 0x14 selectors);
// unchanged replies log at VERBOSE. Every other command (login, mode, config
// writes, raw queries) always logs at DEBUG. Errors always log at WARN.
static std::map<uint16_t, std::string> g_last_rx;

inline bool is_poll_cmd(uint8_t cmd) { return cmd == 0x12 || cmd == 0x14 || cmd == 0x18; }

inline size_t send_command(esphome::uart::UARTComponent *uart, uint8_t cmd, const uint8_t *data,
                           uint8_t data_len, uint8_t *resp, size_t resp_size, uint32_t timeout_ms = 500) {
  memset(resp, 0, resp_size);
  if (!ready()) {
    ESP_LOGV(TAG, "Gate closed - not sending cmd 0x%02X", cmd);
    return 0;
  }

  uint8_t frame[64];
  size_t idx = 0;
  frame[idx++] = 0x02;
  frame[idx++] = cmd;
  frame[idx++] = data_len;
  for (uint8_t i = 0; i < data_len && idx < sizeof(frame) - 2; i++) frame[idx++] = data[i];
  frame[idx] = crc8_dallas(&frame[1], idx - 1);
  idx++;
  frame[idx++] = 0x03;

  uint8_t junk;
  while (uart->available()) uart->read_byte(&junk);  // flush stale bytes

  std::string tx_hex = hex_dump(frame, idx);
  ESP_LOGV(TAG, "TX: %s", tx_hex.c_str());
  uart->write_array(frame, idx);

  // Read by LEN, never by scanning for ETX: there is no byte stuffing, so
  // 0x03 can legitimately appear inside the payload.
  size_t n = 0;
  uint32_t start = millis();
  bool saw_stx = false;
  while (millis() - start < timeout_ms && !saw_stx) {
    uint8_t b;
    if (uart->available() && uart->read_byte(&b) && b == 0x02) {
      resp[n++] = b;
      saw_stx = true;
    }
  }
  if (!saw_stx) {
    ESP_LOGW(TAG, "RX: timeout waiting for STX (TX: %s)", tx_hex.c_str());
    return 0;
  }

  size_t need = 2;  // CMD + LEN
  bool have_len = false;
  while (need > 0 && millis() - start < timeout_ms) {
    uint8_t b;
    if (uart->available() && uart->read_byte(&b)) {
      if (n < resp_size) resp[n++] = b;
      if (--need == 0 && !have_len) {
        have_len = true;
        need = (size_t) resp[2] + 2;  // DATA (incl. status) + CRC + ETX
      }
    }
  }
  if (need > 0) ESP_LOGW(TAG, "RX: timeout mid-frame (%u bytes, cmd 0x%02X)", (unsigned) n, cmd);

  std::string rx_hex = hex_dump(resp, n);
  bool log_it = !is_poll_cmd(cmd) || need > 0 || n < 3;
  if (!log_it) {
    uint16_t key = ((uint16_t) resp[1] << 8) | resp[2];
    auto it = g_last_rx.find(key);
    if (it == g_last_rx.end() || it->second != rx_hex) {
      g_last_rx[key] = rx_hex;
      log_it = true;
    }
  }
  if (log_it) {
    ESP_LOGD(TAG, "TX: %s-> RX: %s", tx_hex.c_str(), rx_hex.c_str());
  } else {
    ESP_LOGV(TAG, "RX (unchanged): %s", rx_hex.c_str());
  }

  // Reply CMD must be request+1, otherwise it's a stale frame from an
  // earlier transaction.
  if (n >= 2 && resp[1] != (uint8_t) (cmd + 1)) {
    ESP_LOGW(TAG, "CMD mismatch: sent 0x%02X, got reply 0x%02X - discarding", cmd, resp[1]);
    return 0;
  }
  return n;
}

inline bool login(esphome::uart::UARTComponent *uart, uint16_t pin) {
  uint8_t data[4] = {0x00, 0x01, (uint8_t) (pin & 0xFF), (uint8_t) (pin >> 8)};
  uint8_t resp[32];
  size_t len = send_command(uart, 0x0C, data, sizeof(data), resp, sizeof(resp));
  uint8_t st = status_byte(resp, len);
  if (st == 0x00) {
    ESP_LOGI(TAG, "Login OK");
    return true;
  }
  if (st == 0x0A) ESP_LOGW(TAG, "Login failed: locked out (too many wrong PINs, ~5 min)");
  else ESP_LOGW(TAG, "Login failed: status 0x%02X", st);
  return false;
}

// Sends a command with automatic re-login on status 0x05 and one retry on
// timeout / CMD mismatch / wrong frame length.
// expected_total_len (optional, 0 = don't check): whole-frame length. Needed
// for 0x14 because every selector replies with CMD 0x15, so a stale battery
// frame (26 bytes) could otherwise pass as a tilt reply (17 bytes).
// The status byte is checked BEFORE the length check: a short 0x05 reply is
// a legitimate "log in first", not a bad frame.
inline size_t send_command_authed(esphome::uart::UARTComponent *uart, uint8_t cmd, const uint8_t *data,
                                  uint8_t data_len, uint8_t *resp, size_t resp_size, uint16_t pin,
                                  size_t expected_total_len = 0) {
  if (!ready()) {
    memset(resp, 0, resp_size);
    return 0;
  }

  for (int attempt = 0; attempt < 2; attempt++) {
    size_t len = send_command(uart, cmd, data, data_len, resp, resp_size);
    uint8_t st = status_byte(resp, len);

    if (st == 0x0A) {
      ESP_LOGW(TAG, "Locked out - not retrying");
      return len;
    }
    if (st == 0x05) {
      ESP_LOGI(TAG, "Unauthorised - logging in");
      if (!login(uart, pin)) return len;
      len = send_command(uart, cmd, data, data_len, resp, resp_size);
    }

    bool len_ok = len > 0 && (expected_total_len == 0 || len == expected_total_len);
    if (len_ok) return len;
    if (len > 0) {
      ESP_LOGW(TAG, "Length mismatch on cmd 0x%02X: expected %u, got %u", cmd, (unsigned) expected_total_len,
               (unsigned) len);
    }
    delay(50);
  }
  memset(resp, 0, resp_size);
  return 0;
}

// ---------------------------------------------------------------------------
// Mode control (0x0E)
// ---------------------------------------------------------------------------
// NB: stop works from AUX1 alone. start/auto/manual are AND-gated with the
// keypad - AUX1 sets the intent, a Start key press is also needed. A clean
// 0x00 status does NOT mean the mower will move.
inline int mode_from_name(const std::string &s) {
  if (s == "stop") return 0x00;
  if (s == "start") return 0x01;
  if (s == "home") return 0x02;
  if (s == "manual") return 0x03;
  if (s == "auto") return 0x04;
  if (s == "search") return 0x05;
  return -1;
}

inline uint8_t set_mode(esphome::uart::UARTComponent *uart, uint8_t mode, uint16_t pin) {
  uint8_t data[1] = {mode};
  uint8_t resp[32];
  size_t len = send_command_authed(uart, 0x0E, data, 1, resp, sizeof(resp), pin);
  uint8_t st = status_byte(resp, len);
  ESP_LOGI(TAG, "Set mode 0x%02X -> status 0x%02X", mode, st);
  return st;
}


// ---------------------------------------------------------------------------
// Long status frame (0x12 with DATA [01 01]) - confirmed 2 Oct
// ---------------------------------------------------------------------------
// Payload: 0-5 normal 0x12 status (b0..b5), 6-9 next scheduled start, 10 flag
// (02 = start set), 11-14 mower clock, 15-16 total hours, 17 battery % (likely),
// 18-21 unknown. Times are the mower's LOCAL time stored as a Unix epoch, so
// format them with gmtime (no time-zone conversion). Next start is zero while
// inside a work period. The last good read is kept in g_long for scripts.
struct LongStatus {
  bool ok = false;
  uint8_t st[6] = {0, 0, 0, 0, 0, 0};
  uint32_t next = 0;
  bool next_set = false;
  uint32_t clock = 0;
  uint16_t hours = 0;
  uint8_t batt = 0;
  uint32_t read_ms = 0;  // millis() of the read
};
static LongStatus g_long;

inline uint8_t timer_bits(const LongStatus &l) { return l.st[3] & 0x03; }   // 0 none, 1 in period, 2 waiting
inline uint8_t mode_bits(const LongStatus &l) { return l.st[3] & 0x30; }    // 00 AUTO, 10 MAN, 20 HOME

inline LongStatus read_long_status(esphome::uart::UARTComponent *uart, uint16_t pin) {
  LongStatus l;
  uint8_t q[2] = {0x01, 0x01};
  uint8_t r[64];
  size_t len = send_command_authed(uart, 0x12, q, sizeof(q), r, sizeof(r), pin);
  const uint8_t *p = payload(r);
  size_t n = payload_len(r, len);
  if (status_byte(r, len) != 0x00 || n < 18) return l;
  ESP_LOGI("next", "0x12 long: %s", hex_dump(p, n).c_str());
  memcpy(l.st, p, 6);
  l.next = (uint32_t) p[6] | (uint32_t) p[7] << 8 | (uint32_t) p[8] << 16 | (uint32_t) p[9] << 24;
  l.next_set = l.next != 0 && p[10] != 0x00;
  l.clock = (uint32_t) p[11] | (uint32_t) p[12] << 8 | (uint32_t) p[13] << 16 | (uint32_t) p[14] << 24;
  l.hours = le16(&p[15]);
  l.batt = p[17];
  l.ok = true;
  l.read_ms = millis();
  g_long = l;
  return l;
}

// "Mon 05 Oct 11:00" from a mower local-time epoch.
inline std::string fmt_mower_time(uint32_t t) {
  time_t tt = (time_t) t;
  struct tm tm;
  gmtime_r(&tt, &tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%a %d %b %H:%M", &tm);
  return buf;
}

// Wraps a menu sequence so it works in any mower state (keypad_and_menus.md):
// "[r" + seq + "rs]" = lid open (wakes it), Home hold (screen on, home
// screen), the settings, Home hold, Start, lid closed. Sequences already
// starting with '[' are left alone; a leading '!' sends the rest raw.
inline std::string wrap_sequence(const std::string &in) {
  std::string s;
  for (char c : in) if (c != ' ') s += c;
  if (s.empty()) return s;
  if (s[0] == '!') return s.substr(1);
  if (s[0] == '[') return s;
  return "[r" + s + "rs]";
}

}  // namespace aux1
