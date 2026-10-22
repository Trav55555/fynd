#include "parser_probe.h"
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    char bytes[513];
    char saved[513];
    char *argv[514];
    size_t i;
    int argc;
    if (size > 512) return 0;
    if (size != 0) memcpy(bytes, data, size);
    bytes[size] = 0;
    memcpy(saved, bytes, size + 1);
    argc = 1;
    argv[0] = "fynd";
    if (size != 0) {
        argv[argc++] = bytes;
        for (i = 0; i + 1 < size; ++i)
            if (bytes[i] == 0) argv[argc++] = bytes + i + 1;
    }
    argv[argc] = NULL;
    check_parser(argc, argv);
    assert(memcmp(bytes, saved, size + 1) == 0);
    check_depth(bytes);
    check_match(data, size, data, size);
    check_size(size, size, 1);
    return 0;
}
