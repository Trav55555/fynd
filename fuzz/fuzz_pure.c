#include "pure_probe.h"
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    size_t cut;
    size_t a;
    size_t b;
    size_t i;
    char text[513];
    if (size == 0 || size > 512) return 0;
    cut = (size_t)data[0] % size;
    check_match(data + 1, cut, data + 1 + cut, size - 1 - cut);
    a = 0;
    b = 0;
    for (i = 1; i < size && i <= sizeof(size_t); ++i)
        a = (a << 8) | (size_t)data[i];
    for (; i < size && i <= 2 * sizeof(size_t); ++i)
        b = (b << 8) | (size_t)data[i];
    check_size(a, b, data[0] & 1);
    check_size((size_t)-1 - (size_t)data[0], b, data[0] & 1);
    memcpy(text, data, size);
    text[size] = 0;
    check_depth(text);
    return 0;
}
