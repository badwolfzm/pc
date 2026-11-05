/* The static site httpd serves and fetch verifies. A test fixture shared by
 * both sides on purpose: fetch must know what the right answer is. */
#ifndef SITE_H
#define SITE_H
#include <stddef.h>
#include <stdint.h>

static const char SITE_INDEX[] =
    "<!doctype html>\n<html><head><title>Warden</title><link rel=stylesheet href=style.css></head>\n"
    "<body><h1>Served from inside a Warden partition</h1>\n"
    "<p>This page comes from an application that holds exactly one directory grant and one listen grant.</p>\n"
    "</body></html>\n";
static const char SITE_CSS[] = "body { font: 16px sans-serif; margin: 2em; color: #222; }\nh1 { color: #0a5; }\n";
#define SITE_BLOB_LEN 10240
static inline uint8_t site_blob_byte(size_t i) { return (uint8_t)(i * 7 + 3); }

#endif
