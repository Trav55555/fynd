#include "stream_probe.h"
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    if (size <= 512)
        check_stream_bytes(data, size);
    return 0;
}
