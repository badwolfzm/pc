#include "../../crates/warden-kernel/user/rofs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static const uint8_t index_data[] = "Hello Warden";
static const uint8_t secret_data[] = "private";
static const struct rofs_entry files[] = {
    {"/site/index.html", index_data, sizeof index_data - 1},
    {"/site/empty", 0, 0},
    {"/siteevil/private", secret_data, sizeof secret_data - 1},
    {"/internal/secret", secret_data, sizeof secret_data - 1}
};
static const struct rofs_grant site = {"/site", 5};
static const struct rofs_grant internal = {"/internal", 9};
static const struct rofs_grant root = {"/", 1};
static unsigned checks;
#define CHECK(expr) do { assert(expr); ++checks; } while (0)
static int lookup(const struct rofs_grant *g, const char *p, const struct rofs_entry **out) {
    return rofs_lookup(files, sizeof files / sizeof files[0], g,
                       (const uint8_t *)p, strlen(p), out);
}

int main(void) {
    const struct rofs_entry *e = 0;
    CHECK(lookup(&site, "/site/index.html", &e) == ROFS_OK);
    CHECK(e == &files[0]);
    CHECK(lookup(&site, "/site/empty", &e) == ROFS_OK);
    CHECK(e == &files[1]);
    CHECK(lookup(&site, "/site/missing", &e) == ROFS_ENOENT);
    CHECK(e == 0);
    CHECK(lookup(&site, "/siteevil/private", &e) == ROFS_EACCES);
    CHECK(e == 0);
    CHECK(lookup(&site, "/internal/secret", &e) == ROFS_EACCES);
    CHECK(lookup(&internal, "/internal/secret", &e) == ROFS_OK);
    CHECK(lookup(&root, "/internal/secret", &e) == ROFS_OK);
    CHECK(lookup(&site, "/site", &e) == ROFS_ENOENT);
    CHECK(lookup(&site, "/site/index.html/", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "/site//index.html", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "/site/./index.html", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "/site/../internal/secret", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "/site/..", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "site/index.html", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "/site\\index.html", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "/site/\nsecret", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "", &e) == ROFS_EINVAL);
    CHECK(lookup(&site, "/", &e) == ROFS_EACCES);
    CHECK(lookup(&root, "/", &e) == ROFS_ENOENT);
    const uint8_t embedded_nul[] = {'/', 's', 'i', 't', 'e', 0, '/', 'x'};
    CHECK(rofs_lookup(files, 4, &site, embedded_nul, sizeof embedded_nul, &e) == ROFS_EINVAL);
    uint8_t big[ROFS_MAX_PATH + 1] = {'/'};
    for (size_t i = 1; i < sizeof big; ++i) big[i] = 'a';
    CHECK(rofs_lookup(files, 4, &root, big, sizeof big, &e) == ROFS_EINVAL);
    CHECK(rofs_lookup(files, 4, &site, (const uint8_t *)"/site/empty", 11, 0) == ROFS_EINVAL);
    const struct rofs_grant malformed = {"/site/../internal", 17};
    CHECK(lookup(&malformed, "/site/index.html", &e) == ROFS_EINVAL);
    const struct rofs_grant forged_len = {"/site", 400};
    CHECK(lookup(&forged_len, "/site/index.html", &e) == ROFS_EINVAL);
    const struct rofs_entry missing_bytes = {"/site/x", 0, 1};
    CHECK(rofs_lookup(&missing_bytes, 1, &site, (const uint8_t *)"/site/x", 7, &e) == ROFS_EINVAL);
    CHECK(rofs_lookup(files, ROFS_MAX_ENTRIES + 1, &site, (const uint8_t *)"/site/x", 7, &e) == ROFS_EINVAL);
    CHECK(rofs_lookup(files, 4, 0, (const uint8_t *)"/site/index.html", 16, &e) == ROFS_EINVAL);
    CHECK(rofs_lookup(files, 4, &root, 0, 0, &e) == ROFS_EINVAL);
    CHECK(rofs_path_valid((const uint8_t *)"/a/b", 4) == ROFS_OK);
    CHECK(rofs_path_valid((const uint8_t *)"/a/.", 4) == ROFS_EINVAL);
    CHECK(rofs_path_valid((const uint8_t *)"/a/..", 5) == ROFS_EINVAL);

    size_t copied = 99;
    uint8_t out[32] = {0};
    CHECK(rofs_read(&files[0], 0, out, 5, &copied) == ROFS_OK);
    CHECK(copied == 5 && memcmp(out, "Hello", 5) == 0);
    CHECK(rofs_read(&files[0], 6, out, sizeof out, &copied) == ROFS_OK);
    CHECK(copied == 6 && memcmp(out, "Warden", 6) == 0);
    CHECK(rofs_read(&files[0], (size_t)-1, out, 5, &copied) == ROFS_OK);
    CHECK(copied == 0);
    CHECK(rofs_read(&files[1], 0, out, sizeof out, &copied) == ROFS_OK);
    CHECK(copied == 0);
    CHECK(rofs_read(&files[0], 0, 0, 1, &copied) == ROFS_EINVAL);
    CHECK(rofs_read(&files[0], 0, out, 5, 0) == ROFS_EINVAL);
    CHECK(rofs_read(0, 0, out, 5, &copied) == ROFS_EINVAL);
    CHECK(rofs_read(&files[0], 0, out, 0, &copied) == ROFS_OK && copied == 0);
    /* Exhaustive short path corpus: every response must be bounded. */
    static const unsigned char alphabet[] = {'/', '.', 0, 'x', '\\', 127, '\n'};
    for (size_t a = 0; a < sizeof alphabet; ++a)
        for (size_t b = 0; b < sizeof alphabet; ++b)
            for (size_t c = 0; c < sizeof alphabet; ++c) {
                uint8_t p[] = {'/', alphabet[a], alphabet[b], alphabet[c]};
                int rc = rofs_lookup(files, 4, &site, p, sizeof p, &e);
                CHECK(rc == ROFS_EINVAL || rc == ROFS_EACCES || rc == ROFS_ENOENT);
                CHECK(e == 0);
            }
    printf("PASS rofs: %u assertions\n", checks);
    return 0;
}
