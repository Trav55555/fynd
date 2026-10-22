/* Dedicated pure parser/matcher cases; no discovery or filesystem calls. */
#include "../fuzz/fuzz_ignore.c"
int main(void)
{
    CuArena a;
    IgnoreFile f;
    IgnoreWork w;
    char exact[] = {'a','\n','!','a','\r','\n','b','/'};
    unsigned char input[4];
    size_t i, j, k;
    int number;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            for (k = 0; k < 4; ++k) {
                input[0] = 1; input[1] = (unsigned char)i;
                input[2] = (unsigned char)j; input[3] = (unsigned char)k;
                assert(LLVMFuzzerTestOneInput(input, sizeof input) == 0);
            }
    cu_arena_init(&a);
    assert(ignore_parse(&a, cu_str_n(exact, sizeof exact), &f, &number));
    assert(f.count == 3 && f.rules[1].negate && f.rules[2].dir_only);
    w.left = 1000000UL; w.failed = 0;
    assert(ignore_file_verdict(&f, "a", 1, 0, &w) == -1);
    assert(ignore_file_verdict(&f, "b", 1, 1, &w) == 1);
    assert(ignore_file_verdict(&f, "b", 1, 0, &w) == 0);
    assert(ignore_glob("a/**/b", 6, "a/b", 3, 1, &w));
    assert(ignore_glob("a/**/b", 6, "a/c/d/b", 7, 1, &w));
    assert(!ignore_glob("[z-a]", 5, "z", 1, 1, &w));
    assert(!ignore_glob("[abc", 4, "a", 1, 1, &w));
    assert(!w.failed);
    cu_arena_release(&a);
    return 0;
}
