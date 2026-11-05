#ifndef WARDEN_ENTROPY_BROKER_H
#define WARDEN_ENTROPY_BROKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Standalone host prototype; NOT a production entropy source or guest-memory API. */
#define WARDEN_ENTROPY_MAX_REQUEST 256u
#define WARDEN_ENTROPY_CLIENTS 8u
#define WARDEN_ENTROPY_MAX_BYTES_PER_CLIENT 1024u
#define WARDEN_ENTROPY_MAX_CALLS_PER_CLIENT 16u
typedef enum {
    WARDEN_ENTROPY_OK = 0, WARDEN_ENTROPY_DENIED,
    WARDEN_ENTROPY_UNAVAILABLE, WARDEN_ENTROPY_INVALID,
    WARDEN_ENTROPY_LIMIT, WARDEN_ENTROPY_PROVIDER_FAILED
} warden_entropy_status;
typedef bool (*warden_entropy_fill)(void *context, uint8_t *dst, size_t length);
typedef struct {
    uint64_t badge;
    uint32_t bytes, calls;
    bool used;
} warden_entropy_client;
typedef struct {
    warden_entropy_fill fill;
    void *provider_context;
    bool trusted_ready; /* Trusted integration boundary only. */
    warden_entropy_client clients[WARDEN_ENTROPY_CLIENTS];
} warden_entropy_broker;
void warden_entropy_init(warden_entropy_broker *broker);
void warden_entropy_attach(warden_entropy_broker *broker,
    warden_entropy_fill provider, void *context, bool trusted_ready);
void warden_entropy_degrade(warden_entropy_broker *broker);
warden_entropy_status warden_entropy_request(warden_entropy_broker *broker,
    uint64_t attested_badge, bool capability_allowed, bool output_validated,
    uint8_t *destination, size_t length);
#endif
