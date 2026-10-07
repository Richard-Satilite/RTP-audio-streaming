#include "ac/ring_buffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ac_ring_buffer_t ring;
    uint8_t input[32];
    uint8_t output[32];
    size_t i;
    for (i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)i;
    assert(ac_ring_buffer_init(&ring, 17) == 0);
    assert(ac_ring_buffer_write(&ring, input, 10) == 10);
    assert(ac_ring_buffer_available(&ring) == 10);
    assert(ac_ring_buffer_read(&ring, output, 6) == 6);
    assert(memcmp(output, input, 6) == 0);
    assert(ac_ring_buffer_write(&ring, input + 10, 12) == 12);
    assert(ac_ring_buffer_read(&ring, output, 16) == 16);
    for (i = 0; i < 4; ++i) assert(output[i] == (uint8_t)(i + 6));
    for (i = 4; i < 16; ++i) assert(output[i] == (uint8_t)(i + 6));
    ac_ring_buffer_uninit(&ring);
    puts("test_ring_buffer: OK");
    return 0;
}
