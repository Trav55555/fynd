/* Pure-memory cursor protocol: real arena lifetimes, independent byte oracle.
 * Filesystem ordering/races are covered separately by test_stream and CLI tests. */
#include "../fuzz/stream_probe.h"

int main(void)
{
    unsigned char data[512];
    size_t i;
    unsigned int count;
    unsigned int mode;
    unsigned int shape;
    check_stream_bytes(NULL, 0);
    for (shape = 0; shape < 4; ++shape) {
        for (i = 0; i < sizeof data; ++i)
            data[i] = (unsigned char)((i * 37U + shape * 63U) & 255U);
        for (count = 0; count <= 32; ++count)
            for (mode = 0; mode < 4; ++mode) {
                data[0] = (unsigned char)count;
                data[1] = (unsigned char)mode;
                data[2] = (unsigned char)(shape * 7U);
                check_stream_bytes(data, sizeof data);
            }
    }
    puts("PASS cursor byte/kind oracle, live release, sticky EOF/EIO/ENOMEM and cleanup");
    return 0;
}
