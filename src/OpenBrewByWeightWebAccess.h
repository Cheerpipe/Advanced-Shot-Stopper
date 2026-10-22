#pragma once

// Optional Web UI login: browser sessions, the Home Assistant token and its
// pairing request. Pure state with no I/O, clock or randomness of its own:
// the network task owns it under dataMux_, feeds times and random bytes, and
// persists `stored`. The login is effective only while the device password is
// not the published factory default; the network applies that rule.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openbrewbyweight {

constexpr size_t WEB_ACCESS_TOKEN_BYTES = 16;
constexpr size_t WEB_ACCESS_TOKEN_TEXT = WEB_ACCESS_TOKEN_BYTES * 2 + 1;
constexpr uint8_t WEB_ACCESS_REMEMBERED_SLOTS = 8;
constexpr uint8_t WEB_ACCESS_BROWSER_SLOTS = 4;
// Remembered sessions age on the wall clock and survive power cycles; the
// last-use stamp moves at most daily to bound flash writes. Without a synced
// clock a remembered session does not expire by age.
constexpr uint32_t WEB_ACCESS_REMEMBER_SEC = 90UL * 24UL * 3600UL;
constexpr uint32_t WEB_ACCESS_RENEW_SEC = 24UL * 3600UL;
constexpr uint32_t WEB_ACCESS_BROWSER_MS = 12UL * 3600UL * 1000UL;
constexpr uint32_t WEB_ACCESS_PAIRING_MS = 120000;
// A decided or expired request stays readable this long; no new request may
// start meanwhile, which also rate-limits pairing attempts.
constexpr uint32_t WEB_ACCESS_PAIRING_RESULT_MS = 30000;
constexpr uint32_t WEB_ACCESS_MAGIC = 0x57414343U;  // "WACC"
constexpr uint16_t WEB_ACCESS_VERSION = 0;

struct WebAccessRemembered {
  uint8_t token[WEB_ACCESS_TOKEN_BYTES] = {};
  uint32_t usedUtc = 0;  // 0 = never stamped on a synced clock
};

// One NVS blob. Missing or invalid means "login off, nothing remembered".
struct WebAccessStored {
  uint32_t magic = WEB_ACCESS_MAGIC;
  uint16_t version = WEB_ACCESS_VERSION;
  uint16_t structureSize = sizeof(WebAccessStored);
  uint8_t enabled = 0;
  uint8_t noticeDismissed = 0;
  uint8_t homeAssistant = 0;
  uint8_t rememberedMask = 0;
  uint32_t homeAssistantSinceUtc = 0;
  uint8_t homeAssistantToken[WEB_ACCESS_TOKEN_BYTES] = {};
  WebAccessRemembered remembered[WEB_ACCESS_REMEMBERED_SLOTS] = {};
  uint32_t checksum = 0;
};
static_assert(sizeof(WebAccessStored) == 196, "webAccess blob layout changed");

enum class WebAccessPairing : uint8_t { NONE, PENDING, APPROVED, DENIED, EXPIRED };

struct WebAccessState {
  WebAccessStored stored = {};
  struct Browser {
    uint8_t token[WEB_ACCESS_TOKEN_BYTES] = {};
    uint32_t untilMs = 0;
    bool live = false;
  } browser[WEB_ACCESS_BROWSER_SLOTS] = {};
  struct Pairing {
    WebAccessPairing state = WebAccessPairing::NONE;
    uint32_t id = 0;
    uint32_t untilMs = 0;
    char code[5] = {};
    uint8_t token[WEB_ACCESS_TOKEN_BYTES] = {};
  } pairing = {};
  bool dirty = false;
};

inline uint32_t webAccessChecksum(const WebAccessStored &stored) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&stored);
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < offsetof(WebAccessStored, checksum); ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

inline void finalizeWebAccessStored(WebAccessStored &stored) {
  stored.magic = WEB_ACCESS_MAGIC;
  stored.version = WEB_ACCESS_VERSION;
  stored.structureSize = sizeof(WebAccessStored);
  stored.checksum = webAccessChecksum(stored);
}

inline bool validWebAccessStored(const WebAccessStored &stored) {
  return stored.magic == WEB_ACCESS_MAGIC &&
         stored.version == WEB_ACCESS_VERSION &&
         stored.structureSize == sizeof(WebAccessStored) &&
         stored.enabled <= 1 && stored.noticeDismissed <= 1 &&
         stored.homeAssistant <= 1 &&
         stored.checksum == webAccessChecksum(stored);
}

// Tokens travel as exactly 32 lowercase hex digits.
inline bool parseWebAccessToken(const char *text,
                                uint8_t token[WEB_ACCESS_TOKEN_BYTES]) {
  if (text == nullptr) return false;
  for (size_t index = 0; index < WEB_ACCESS_TOKEN_BYTES * 2; ++index) {
    const char c = text[index];
    const int nibble = c >= '0' && c <= '9' ? c - '0'
                       : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (nibble < 0) return false;
    if ((index & 1U) == 0) token[index / 2] = static_cast<uint8_t>(nibble << 4);
    else token[index / 2] |= static_cast<uint8_t>(nibble);
  }
  return text[WEB_ACCESS_TOKEN_BYTES * 2] == '\0';
}

inline void formatWebAccessToken(const uint8_t token[WEB_ACCESS_TOKEN_BYTES],
                                 char text[WEB_ACCESS_TOKEN_TEXT]) {
  static const char digits[] = "0123456789abcdef";
  for (size_t index = 0; index < WEB_ACCESS_TOKEN_BYTES; ++index) {
    text[index * 2] = digits[token[index] >> 4];
    text[index * 2 + 1] = digits[token[index] & 0x0F];
  }
  text[WEB_ACCESS_TOKEN_BYTES * 2] = '\0';
}

inline bool webAccessTokenEqual(const uint8_t *left, const uint8_t *right) {
  uint8_t difference = 0;
  for (size_t index = 0; index < WEB_ACCESS_TOKEN_BYTES; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

inline bool webAccessRememberedLive(const WebAccessStored &stored, uint8_t slot,
                                    uint32_t nowUtc) {
  if ((stored.rememberedMask & (1U << slot)) == 0) return false;
  // A stamp ahead of the clock (NTP moved it back) counts as fresh.
  const uint32_t used = stored.remembered[slot].usedUtc;
  return nowUtc == 0 || nowUtc <= used || nowUtc - used < WEB_ACCESS_REMEMBER_SEC;
}

inline uint8_t webAccessRememberedCount(const WebAccessState &state,
                                        uint32_t nowUtc) {
  uint8_t count = 0;
  for (uint8_t slot = 0; slot < WEB_ACCESS_REMEMBERED_SLOTS; ++slot) {
    count += webAccessRememberedLive(state.stored, slot, nowUtc) ? 1 : 0;
  }
  return count;
}

// nowUtc is 0 while the wall clock is not synced.
inline bool webAccessSessionValid(WebAccessState &state,
                                  const uint8_t token[WEB_ACCESS_TOKEN_BYTES],
                                  uint32_t nowMs, uint32_t nowUtc) {
  for (auto &browser : state.browser) {
    if (browser.live && webAccessTokenEqual(browser.token, token)) {
      if (static_cast<int32_t>(nowMs - browser.untilMs) < 0) return true;
      browser = WebAccessState::Browser{};
      return false;
    }
  }
  WebAccessStored &stored = state.stored;
  for (uint8_t slot = 0; slot < WEB_ACCESS_REMEMBERED_SLOTS; ++slot) {
    if ((stored.rememberedMask & (1U << slot)) == 0 ||
        !webAccessTokenEqual(stored.remembered[slot].token, token)) {
      continue;
    }
    if (!webAccessRememberedLive(stored, slot, nowUtc)) {
      stored.rememberedMask &= static_cast<uint8_t>(~(1U << slot));
      stored.remembered[slot] = WebAccessRemembered{};
      state.dirty = true;
      return false;
    }
    uint32_t &used = stored.remembered[slot].usedUtc;
    if (nowUtc != 0 && (used == 0 || (nowUtc > used && nowUtc - used >= WEB_ACCESS_RENEW_SEC))) {
      used = nowUtc;
      state.dirty = true;
    }
    return true;
  }
  return false;
}

// Full tables evict the session closest to expiry.
inline void webAccessIssueSession(WebAccessState &state,
                                  const uint8_t token[WEB_ACCESS_TOKEN_BYTES],
                                  bool remember, uint32_t nowMs,
                                  uint32_t nowUtc) {
  if (!remember) {
    WebAccessState::Browser *target = &state.browser[0];
    for (auto &browser : state.browser) {
      if (!browser.live) { target = &browser; break; }
      if (static_cast<int32_t>(browser.untilMs - target->untilMs) < 0) target = &browser;
    }
    memcpy(target->token, token, WEB_ACCESS_TOKEN_BYTES);
    target->untilMs = nowMs + WEB_ACCESS_BROWSER_MS;
    target->live = true;
    return;
  }
  WebAccessStored &stored = state.stored;
  uint8_t target = 0;
  for (uint8_t slot = 0; slot < WEB_ACCESS_REMEMBERED_SLOTS; ++slot) {
    if (!webAccessRememberedLive(stored, slot, nowUtc)) { target = slot; break; }
    if (stored.remembered[slot].usedUtc < stored.remembered[target].usedUtc) target = slot;
  }
  memcpy(stored.remembered[target].token, token, WEB_ACCESS_TOKEN_BYTES);
  stored.remembered[target].usedUtc = nowUtc;
  stored.rememberedMask |= static_cast<uint8_t>(1U << target);
  state.dirty = true;
}

// Revokes Web UI sessions: just `token` when `only`, otherwise every one but
// `token` (nullable). The Home Assistant token is separate and survives.
inline void webAccessRevokeSessions(WebAccessState &state, const uint8_t *token,
                                    bool only = false) {
  const auto revoke = [&](const uint8_t *candidate) {
    return only == (token != nullptr && webAccessTokenEqual(candidate, token));
  };
  for (auto &browser : state.browser) {
    if (browser.live && revoke(browser.token)) browser = WebAccessState::Browser{};
  }
  WebAccessStored &stored = state.stored;
  for (uint8_t slot = 0; slot < WEB_ACCESS_REMEMBERED_SLOTS; ++slot) {
    if ((stored.rememberedMask & (1U << slot)) != 0 &&
        revoke(stored.remembered[slot].token)) {
      stored.rememberedMask &= static_cast<uint8_t>(~(1U << slot));
      stored.remembered[slot] = WebAccessRemembered{};
      state.dirty = true;
    }
  }
}

inline bool webAccessHomeAssistantValid(const WebAccessState &state,
                                        const uint8_t token[WEB_ACCESS_TOKEN_BYTES]) {
  return state.stored.homeAssistant != 0 &&
         webAccessTokenEqual(state.stored.homeAssistantToken, token);
}

// One integration slot: a new token replaces the previous one.
inline void webAccessSetHomeAssistant(WebAccessState &state,
                                      const uint8_t token[WEB_ACCESS_TOKEN_BYTES],
                                      uint32_t nowUtc) {
  memcpy(state.stored.homeAssistantToken, token, WEB_ACCESS_TOKEN_BYTES);
  state.stored.homeAssistant = 1;
  state.stored.homeAssistantSinceUtc = nowUtc;
  state.dirty = true;
}

inline void webAccessForgetHomeAssistant(WebAccessState &state) {
  if (state.stored.homeAssistant == 0) return;
  memset(state.stored.homeAssistantToken, 0, WEB_ACCESS_TOKEN_BYTES);
  state.stored.homeAssistant = 0;
  state.stored.homeAssistantSinceUtc = 0;
  state.dirty = true;
}

inline void webAccessTickPairing(WebAccessState &state, uint32_t nowMs) {
  auto &pairing = state.pairing;
  if (pairing.state == WebAccessPairing::NONE ||
      static_cast<int32_t>(nowMs - pairing.untilMs) < 0) {
    return;
  }
  if (pairing.state == WebAccessPairing::PENDING) {
    pairing.state = WebAccessPairing::EXPIRED;
    pairing.untilMs = nowMs + WEB_ACCESS_PAIRING_RESULT_MS;
    return;
  }
  pairing = WebAccessState::Pairing{};
}

// One request at a time; `code` is shown both in Home Assistant and in the
// Web UI prompt so the person approves the request they started.
inline bool webAccessStartPairing(WebAccessState &state, uint32_t id,
                                  uint16_t code, uint32_t nowMs) {
  webAccessTickPairing(state, nowMs);
  if (state.pairing.state != WebAccessPairing::NONE || id == 0) return false;
  auto &pairing = state.pairing;
  pairing.state = WebAccessPairing::PENDING;
  pairing.id = id;
  pairing.untilMs = nowMs + WEB_ACCESS_PAIRING_MS;
  for (int index = 3; index >= 0; --index) {
    pairing.code[index] = static_cast<char>('0' + code % 10);
    code = static_cast<uint16_t>(code / 10);
  }
  pairing.code[4] = '\0';
  return true;
}

inline bool webAccessDecidePairing(WebAccessState &state, uint32_t id, bool allow,
                                   const uint8_t token[WEB_ACCESS_TOKEN_BYTES],
                                   uint32_t nowMs, uint32_t nowUtc) {
  webAccessTickPairing(state, nowMs);
  auto &pairing = state.pairing;
  if (pairing.state != WebAccessPairing::PENDING || pairing.id != id) return false;
  pairing.state = allow ? WebAccessPairing::APPROVED : WebAccessPairing::DENIED;
  pairing.untilMs = nowMs + WEB_ACCESS_PAIRING_RESULT_MS;
  if (allow) {
    memcpy(pairing.token, token, WEB_ACCESS_TOKEN_BYTES);
    webAccessSetHomeAssistant(state, token, nowUtc);
  }
  return true;
}

// An approved token is handed out once; the slot then clears.
inline WebAccessPairing webAccessPairingStatus(
    WebAccessState &state, uint32_t id, uint32_t nowMs,
    uint8_t token[WEB_ACCESS_TOKEN_BYTES]) {
  webAccessTickPairing(state, nowMs);
  auto &pairing = state.pairing;
  if (pairing.state == WebAccessPairing::NONE || pairing.id != id) {
    return WebAccessPairing::NONE;
  }
  const WebAccessPairing result = pairing.state;
  if (result == WebAccessPairing::APPROVED) {
    memcpy(token, pairing.token, WEB_ACCESS_TOKEN_BYTES);
    pairing = WebAccessState::Pairing{};
  }
  return result;
}

// Login off: every session, the Home Assistant token and a pending request
// go; only the notice choice is kept.
inline void webAccessDisable(WebAccessState &state) {
  webAccessRevokeSessions(state, nullptr);
  webAccessForgetHomeAssistant(state);
  state.pairing = WebAccessState::Pairing{};
  if (state.stored.enabled != 0) {
    state.stored.enabled = 0;
    state.dirty = true;
  }
}

}  // namespace openbrewbyweight
