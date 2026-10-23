#include "../fuzz/parser_probe.h"
int main(void)
{
    char *defaults[] = {"fynd", NULL};
    char *options[] = {"fynd", "-i", "-i", "-H", "-0", "-t", "f", "-t", "d",
        "-e", "C", "-e", "h", "--max-depth", "0", "--max-depth", "256", "x", "a", "b", NULL};
    char *literal[] = {"fynd", "--", "-i", "-H", NULL};
    char *bad[] = {"fynd", "--max-depth", "257", NULL};
    char *empty[] = {"fynd", "", "", NULL};
    char *operand[] = {"fynd", "x", "--unknown", NULL};
    char *jobs[] = {"fynd", "--no-ignore", "-j", "01", "-j32", "--threads", "2", NULL};
    char *job_values[] = {"", "0", "33", "-1", "+2", "2x", "999999999999999999999"};
    char *job_invalid[4];
    unsigned int i;
    check_parser(7, jobs);
    job_invalid[0] = "fynd";
    job_invalid[1] = "--threads";
    job_invalid[3] = NULL;
    for (i = 0; i < sizeof(job_values) / sizeof(job_values[0]); ++i) {
        job_invalid[2] = job_values[i];
        check_parser(3, job_invalid);
    }
    {
        char *invalid[] = {"fynd", "--threads=2", NULL};
        check_parser(2, invalid);
    }
    check_parser(1, defaults);
    check_parser(20, options);
    check_parser(4, literal);
    check_parser(3, bad);
    check_parser(3, empty);
    check_parser(3, operand);
    check_depth("00256");
    check_match((const unsigned char *)"Ab", 2, (const unsigned char *)"a", 1);
    check_size(3, 4, 1);
    return 0;
}
