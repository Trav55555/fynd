#include "../fuzz/stream_probe.h"
int main(void)
{
    /* Three exact entries: FILE "A", DIR {128,255}, OTHER {255,1,97}.
     * Zero input name bytes become 1; the arena copy appends the NUL.
     * The OOM seed fails the third real name allocation, after two views. */
    static const unsigned char seed[] = {
        3, 0, 2,
        0, 0, 65, 88, 89,
        1, 1, 128, 255, 90,
        2, 3, 255, 0, 97
    };
    unsigned char data[sizeof seed];
    size_t i;
    check_stream_bytes(NULL, 0);
    for (i = 0; i < sizeof seed; ++i)
        data[i] = seed[i];
    check_stream_bytes(data, sizeof data); /* All three, then sticky EOF. */
    data[1] = STREAM_PROBE_EIO;
    check_stream_bytes(data, sizeof data); /* All three, then sticky EIO. */
    data[1] = STREAM_PROBE_OOM;
    check_stream_bytes(data, sizeof data); /* Normalized terminal ENOMEM. */
    data[1] = STREAM_PROBE_RELEASE;
    data[2] = 3;
    check_stream_bytes(data, sizeof data); /* Release a live third view. */
    return 0;
}
