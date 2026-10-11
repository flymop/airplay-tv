// Exercises the actual UxPlay queue; only decryption and logging are stubbed.
#include "raop_buffer.c"

struct aes_ctx_s { int unused; };
aes_ctx_t *aes_cbc_init(const uint8_t *key, const uint8_t *iv, aes_direction_t direction) {
    (void)key; (void)iv; (void)direction;
    return calloc(1, sizeof(aes_ctx_t));
}
void aes_cbc_reset(aes_ctx_t *ctx) { (void)ctx; }
void aes_cbc_destroy(aes_ctx_t *ctx) { free(ctx); }
void aes_cbc_decrypt(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int len) {
    (void)ctx; memcpy(out, in, len);
}
void logger_log(logger_t *logger, int level, const char *fmt, ...) {
    (void)logger; (void)level; (void)fmt;
}
char *utils_data_to_string(const unsigned char *data, int len, int width) {
    (void)data; (void)len; (void)width; return NULL;
}

static const unsigned char key[16] = {0};
static int failed;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failed; } } while (0)

static raop_buffer_t *make_buffer(unsigned int limit) {
    raop_buffer_t *b = raop_buffer_init(NULL, key, key);
    raop_buffer_set_resend_limit(b, limit);
    return b;
}
static void enqueue(raop_buffer_t *b, unsigned short seq) {
    unsigned char packet[13] = {0};
    packet[2] = seq >> 8; packet[3] = seq & 255;
    packet[12] = 42;
    raop_buffer_enqueue(b, packet, sizeof(packet), 1);
}
static int dequeue(raop_buffer_t *b, int no_resend) {
    unsigned short seq;
    unsigned int length;
    uint32_t timestamp;
    void *payload = raop_buffer_dequeue(b, &length, &timestamp, &seq, no_resend);
    if (!payload) return -1;
    CHECK(length == 1 && *(unsigned char *)payload == 42);
    free(payload);
    return seq;
}
static void lost_packet(unsigned short first) {
    raop_buffer_t *b = make_buffer(8);
    enqueue(b, first);
    CHECK(dequeue(b, 0) == first);
    for (int i = 2; i < 8; ++i) {
        enqueue(b, (unsigned short)(first + i));
        CHECK(dequeue(b, 0) == -1); // retain the short retransmission window
    }
    enqueue(b, (unsigned short)(first + 8));
    for (int i = 2; i <= 8; ++i)
        CHECK(dequeue(b, 0) == (unsigned short)(first + i));
    enqueue(b, (unsigned short)(first + 1)); // late retransmission cannot rewind
    CHECK(dequeue(b, 0) == -1);
    enqueue(b, (unsigned short)(first + 9));
    CHECK(dequeue(b, 0) == (unsigned short)(first + 9));
    raop_buffer_destroy(b);
}
static void timely_resend(void) {
    raop_buffer_t *b = make_buffer(8);
    enqueue(b, 100); CHECK(dequeue(b, 0) == 100);
    enqueue(b, 102); CHECK(dequeue(b, 0) == -1);
    enqueue(b, 101);
    CHECK(dequeue(b, 0) == 101);
    CHECK(dequeue(b, 0) == 102);
    enqueue(b, 102); CHECK(dequeue(b, 0) == -1);
    raop_buffer_destroy(b);
}
static void multiple_holes(void) {
    raop_buffer_t *b = make_buffer(8);
    enqueue(b, 100); CHECK(dequeue(b, 0) == 100);
    enqueue(b, 109); // both missing slots have aged out
    enqueue(b, 103); // reordered but valid
    CHECK(dequeue(b, 0) == 103);
    CHECK(dequeue(b, 0) == -1); // later missing slots retain their own wait
    raop_buffer_destroy(b);
}
static void no_resend_skips_holes(void) {
    raop_buffer_t *b = make_buffer(8);
    enqueue(b, 100); CHECK(dequeue(b, 1) == 100);
    enqueue(b, 103); CHECK(dequeue(b, 1) == 103);
    raop_buffer_destroy(b);
}
static void default_music_window(void) {
    raop_buffer_t *b = make_buffer(0);
    enqueue(b, 100); CHECK(dequeue(b, 0) == 100);
    for (int i = 102; i < 120; ++i) {
        enqueue(b, i); CHECK(dequeue(b, 0) == -1);
    }
    enqueue(b, 101); CHECK(dequeue(b, 0) == 101);
    CHECK(dequeue(b, 0) == 102);
    raop_buffer_destroy(b);
}
static void flush_preserves_limit(void) {
    raop_buffer_t *b = make_buffer(8);
    enqueue(b, 100); CHECK(dequeue(b, 0) == 100);
    raop_buffer_flush(b, 500);
    for (int i = 501; i <= 507; ++i) enqueue(b, i);
    // Sequence 500 has expired under the same limit used before FLUSH.
    for (int i = 501; i <= 507; ++i) CHECK(dequeue(b, 0) == i);
    CHECK(dequeue(b, 0) == -1);
    raop_buffer_destroy(b);
}
int main(void) {
    lost_packet(100);
    lost_packet(65532); // RTP sequence wrap
    timely_resend();
    multiple_holes();
    no_resend_skips_holes();
    default_music_window();
    flush_preserves_limit();
    if (failed) return EXIT_FAILURE;
    puts("PASS receive queue: bounded loss wait, wrap, reorder, late/duplicate packets, multiple holes, no-resend, music default, FLUSH");
    return EXIT_SUCCESS;
}
