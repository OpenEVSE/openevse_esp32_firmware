#include "http_etag.h"

#include <string.h>

// Strip one optional weak marker (`W/`) and one optional pair of quotes,
// yielding the opaque tag itself. Both are advisory decoration: for a weak
// comparison neither participates in the match.
static void unwrap(const char *&p, size_t &len)
{
  if(len >= 2 && 'W' == p[0] && '/' == p[1]) {
    p += 2;
    len -= 2;
  }
  if(len >= 2 && '"' == p[0] && '"' == p[len - 1]) {
    p += 1;
    len -= 2;
  }
}

bool http_etag_matches(const char *header, size_t header_len, const char *etag)
{
  if(NULL == header || 0 == header_len || NULL == etag) {
    return false;
  }

  const char *want = etag;
  size_t want_len = strlen(etag);
  unwrap(want, want_len);
  if(0 == want_len) {
    return false;
  }

  size_t i = 0;
  while(i < header_len)
  {
    // Skip separators and leading whitespace.
    while(i < header_len && (',' == header[i] || ' ' == header[i] || '\t' == header[i])) {
      i++;
    }
    size_t start = i;

    // A tag runs to the next comma. Quotes cannot contain one: the opaque part
    // of an entity-tag excludes both DQUOTE and, in practice, the comma we
    // split on, so a plain scan is enough here.
    while(i < header_len && ',' != header[i]) {
      i++;
    }
    size_t end = i;

    // Trim trailing whitespace.
    while(end > start && (' ' == header[end - 1] || '\t' == header[end - 1])) {
      end--;
    }
    if(end == start) {
      continue;
    }

    const char *got = header + start;
    size_t got_len = end - start;

    if(1 == got_len && '*' == got[0]) {
      return true;
    }

    unwrap(got, got_len);

    if(got_len == want_len && 0 == memcmp(got, want, want_len)) {
      return true;
    }
  }

  return false;
}

bool http_etag_quote(const char *etag, char *out, size_t out_len)
{
  if(NULL == out || 0 == out_len) {
    return false;
  }
  out[0] = '\0';

  if(NULL == etag) {
    return false;
  }

  const char *tag = etag;
  size_t tag_len = strlen(etag);
  unwrap(tag, tag_len);
  if(0 == tag_len) {
    return false;
  }

  if(tag_len + 3 > out_len) {
    return false;
  }

  out[0] = '"';
  memcpy(out + 1, tag, tag_len);
  out[tag_len + 1] = '"';
  out[tag_len + 2] = '\0';
  return true;
}
