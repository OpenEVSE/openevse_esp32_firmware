// src/http_etag.h — RFC 7232 conditional-request helpers for the embedded
// static-file handler.
//
// The static files carry a content hash as their entity-tag. Getting the
// comparison wrong is silent and expensive: the handler simply falls through to
// the 200 path and re-sends the whole body, so a browser that caches perfectly
// still re-downloads the entire UI bundle on every page load.
//
// Pure (no Arduino, no Mongoose) so it can be host-tested.
#ifndef HTTP_ETAG_H
#define HTTP_ETAG_H

#include <stddef.h>

// The stored etags are 64-char hex; +2 quotes +NUL.
#define HTTP_ETAG_QUOTED_MAX 80

// True if an If-None-Match header value matches `etag`, per RFC 7232 section 3.2.
//
// `header` is a raw header value (NOT necessarily NUL-terminated -- pass its
// length) and may be a comma-separated list, may quote its tags, and may mark
// them weak with a `W/` prefix. `etag` is the stored tag, with or without
// quotes. Comparison is weak: a `W/` prefix on either side is ignored, which is
// what a cache revalidation wants.
//
// Returns false when `header` is empty or `etag` is null/empty. A bare `*`
// matches anything, as the RFC requires.
bool http_etag_matches(const char *header, size_t header_len, const char *etag);

// Write `etag` to `out` as a quoted entity-tag, adding quotes if it has none.
// Browsers echo back exactly what we send, and an unquoted tag is not a valid
// entity-tag, so sending one invites the client to normalise it into a form our
// own comparison then has to cope with. Returns false (and writes an empty
// string) if the result would not fit.
bool http_etag_quote(const char *etag, char *out, size_t out_len);

#endif // HTTP_ETAG_H
