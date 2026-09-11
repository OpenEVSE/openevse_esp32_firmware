#include "notifications_acks.h"
#include <string.h>
#include <stdio.h>

namespace {

// Parse lowercase hex without pulling in strtoul's locale baggage. Returns
// false on an empty or non-hex run, which the caller treats as a malformed
// entry and skips.
bool parse_hex(const char *s, size_t len, uint32_t &out)
{
  if(0 == len || len > 8) {
    return false;
  }
  uint32_t v = 0;
  for(size_t i = 0; i < len; i++) {
    char c = s[i];
    uint32_t d;
    if(c >= '0' && c <= '9')      { d = (uint32_t)(c - '0'); }
    else if(c >= 'a' && c <= 'f') { d = (uint32_t)(c - 'a' + 10); }
    else if(c >= 'A' && c <= 'F') { d = (uint32_t)(c - 'A' + 10); }
    else                          { return false; }
    v = (v << 4) | d;
  }
  out = v;
  return true;
}

} // namespace

size_t notification_acks_decode(const char *s, NotificationAck *out, size_t max)
{
  size_t count = 0;
  if(NULL == s) {
    return 0;
  }

  const char *p = s;
  while(*p && count < max) {
    const char *colon = strchr(p, ':');
    const char *semi  = strchr(p, ';');
    if(NULL == colon || NULL == semi || colon > semi) {
      break;                       // no well-formed entry left
    }

    size_t klen = (size_t)(colon - p);
    uint32_t token = 0;
    // sizeof(key) - 1 so a longer key is rejected outright: silently
    // truncating it could collide with a different rule's key.
    if(klen > 0 && klen <= sizeof(out[count].key) - 1 &&
       parse_hex(colon + 1, (size_t)(semi - colon - 1), token))
    {
      memcpy(out[count].key, p, klen);
      out[count].key[klen] = '\0';
      out[count].token = token;
      count++;
    }
    p = semi + 1;
  }

  return count;
}

size_t notification_acks_encode(const NotificationAck *acks, size_t count, char *out, size_t out_size)
{
  size_t written = 0;
  size_t used = 0;

  if(NULL == out || 0 == out_size) {
    return 0;
  }
  out[0] = '\0';

  for(size_t i = 0; i < count; i++) {
    char entry[24];
    int n = snprintf(entry, sizeof(entry), "%s:%x;", acks[i].key, (unsigned)acks[i].token);
    if(n < 0 || used + (size_t)n + 1 > out_size) {
      break;                       // leave what fits, still well-formed
    }
    memcpy(out + used, entry, (size_t)n);
    used += (size_t)n;
    out[used] = '\0';
    written++;
  }

  return written;
}

bool notification_acks_is_acked(const NotificationAck *acks, size_t count, const char *key, uint32_t token)
{
  for(size_t i = 0; i < count; i++) {
    if(0 == strcmp(acks[i].key, key)) {
      return acks[i].token == token;
    }
  }
  return false;
}

size_t notification_acks_set(NotificationAck *acks, size_t count, size_t max, const char *key, uint32_t token)
{
  for(size_t i = 0; i < count; i++) {
    if(0 == strcmp(acks[i].key, key)) {
      acks[i].token = token;
      return count;
    }
  }
  if(count >= max || strlen(key) > sizeof(acks[0].key) - 1) {
    return count;
  }
  strncpy(acks[count].key, key, sizeof(acks[count].key) - 1);
  acks[count].key[sizeof(acks[count].key) - 1] = '\0';
  acks[count].token = token;
  return count + 1;
}

size_t notification_acks_prune(NotificationAck *acks, size_t count, const Notification *live, size_t live_count)
{
  size_t kept = 0;
  for(size_t i = 0; i < count; i++) {
    bool still_live = false;
    for(size_t j = 0; j < live_count; j++) {
      if(0 == strcmp(acks[i].key, live[j].key)) {
        still_live = true;
        break;
      }
    }
    if(still_live) {
      if(kept != i) {
        acks[kept] = acks[i];
      }
      kept++;
    }
  }
  return kept;
}
