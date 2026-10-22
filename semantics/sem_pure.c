#include "../fuzz/pure_probe.h"
int main(void)
{
    static const unsigned char alphabet[] = {0, 65, 97, 255};
    unsigned char h[2] = {0, 0};
    unsigned char p[1];
    size_t i;
    size_t j;
    size_t k;
    size_t n;
    size_t m;
    int slash;
    p[0] = 0;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            for (k = 0; k < 4; ++k) {
                h[0] = alphabet[i]; h[1] = alphabet[j]; p[0] = alphabet[k];
                for (n = 0; n <= 2; ++n)
                    for (m = 0; m <= 1; ++m) check_match(h, n, p, m);
            }
    for (slash = 0; slash <= 1; ++slash) {
        check_size(0, 0, slash);
        check_size((size_t)-1 - (slash ? 2U : 1U), 0, slash);
        check_size((size_t)-1 - (slash ? 1U : 0U), 0, slash);
        check_size(1, (size_t)-1, slash);
    }
    check_depth("0"); check_depth("128"); check_depth("129");
    check_depth("000128"); check_depth(""); check_depth("-1");
    check_depth("12x"); check_depth("999999999999999999999");
    return 0;
}
