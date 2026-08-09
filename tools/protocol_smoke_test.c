/* Host-side smoke test for the shared framed UART protocol.
 *
 * Windows example (MSYS2/UCRT64):
 *   gcc -std=c11 -Wall -Wextra -Werror -Ishared tools/protocol_smoke_test.c shared/protocol.c -o protocol_smoke_test.exe
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../shared/protocol.h"

int main(void) {
    static const uint8_t crc_vector[] = "123456789";
    assert(proto_crc32_buf(crc_vector, sizeof(crc_vector) - 1) == 0xCBF43926U);

    uint8_t all_bytes[256];
    for (uint16_t i = 0; i < sizeof(all_bytes); ++i) {
        all_bytes[i] = (uint8_t)i;
    }
    assert(proto_crc32_buf(all_bytes, sizeof(all_bytes)) == 0x29058C73U);

    uint8_t payload[PROTO_MAX_PAYLOAD];
    for (uint16_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)i;
    }

    uint8_t frame[PROTO_MAX_FRAME];
    uint16_t frame_len = proto_build_frame(frame, sizeof(frame), CMD_OTA_CHUNK,
                                           payload, sizeof(payload));
    assert(frame_len == PROTO_MAX_FRAME);

    ProtoParser_t parser;
    proto_parser_init(&parser);
    const ProtoFrame_t *decoded = NULL;
    for (uint16_t i = 0; i < frame_len; ++i) {
        const ProtoFrame_t *candidate = proto_parser_feed(&parser, frame[i]);
        if (candidate != NULL) {
            decoded = candidate;
        }
    }
    assert(decoded != NULL);
    assert(decoded->cmd == CMD_OTA_CHUNK);
    assert(decoded->len == sizeof(payload));
    assert(memcmp(decoded->payload, payload, sizeof(payload)) == 0);

    frame[frame_len - 1] ^= 0x01U;
    proto_parser_init(&parser);
    for (uint16_t i = 0; i < frame_len; ++i) {
        assert(proto_parser_feed(&parser, frame[i]) == NULL);
    }

    puts("protocol smoke test passed");
    return 0;
}
