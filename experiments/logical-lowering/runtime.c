#define _DEFAULT_SOURCE

#include <libproc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

#define LEN (1 << 18)
#define PASSES 40
#define RUNS 9

typedef int64_t Run(const int64_t *x, const int64_t *y, int64_t n, int64_t m, int64_t len);

Run run_slot, run_value, run_value_fused, run_branch;

static uint64_t
cycles(void)
{
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    return ri.ri_cycles;
}

static double
median(double *v, int n)
{
    for (int i = 1; i < n; i++) {
        double x = v[i];
        int j = i;
        for (; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    return v[n / 2];
}

static uint64_t state = 0x9E3779B97F4A7C15ull;

static uint64_t
next(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static void
fill(int64_t *v, int percent)
{
    for (int i = 0; i < LEN; i++) {
        v[i] = (int64_t)(next() % 100) < percent ? 0 : 200;
    }
}

int
main(void)
{
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    int64_t *x = malloc(LEN * sizeof *x);
    int64_t *y = malloc(LEN * sizeof *y);
    static const struct
    {
        const char *name;
        Run *run;
    } forms[] = {
        { "A  hidden local", run_slot },
        { "B  if with a value", run_value },
        { "B' B, compare fused with if", run_value_fused },
        { "C  branches", run_branch },
    };
    static const struct
    {
        const char *name;
        int px, py;
    } data[] = {
        { "both true (predictable)", 100, 100 },
        { "90% true each", 90, 90 },
        { "50% true each (unpredictable)", 50, 50 },
    };

    printf("cycles per `if x < n and y < m { count += 1 }`, locals in frame slots\n");
    printf("%-30s", "");
    for (size_t d = 0; d < sizeof data / sizeof data[0]; d++) {
        printf("  %-30s", data[d].name);
    }
    printf("\n");

    double result[4][3];
    int64_t check[3] = { -1, -1, -1 };
    for (size_t d = 0; d < sizeof data / sizeof data[0]; d++) {
        fill(x, data[d].px);
        fill(y, data[d].py);
        for (size_t f = 0; f < sizeof forms / sizeof forms[0]; f++) {
            double per[RUNS];
            for (int r = 0; r < RUNS; r++) {
                int64_t total = 0;
                uint64_t c0 = cycles();
                for (int p = 0; p < PASSES; p++) {
                    total += forms[f].run(x, y, 100, 100, LEN);
                }
                uint64_t c1 = cycles();
                per[r] = (double)(c1 - c0) / ((double)LEN * PASSES);
                if (check[d] < 0) {
                    check[d] = total;
                } else if (check[d] != total) {
                    fprintf(
                        stderr,
                        "%s computed %lld, expected %lld\n",
                        forms[f].name,
                        (long long)total,
                        (long long)check[d]
                    );
                    return 1;
                }
            }
            result[f][d] = median(per, RUNS);
        }
    }

    for (size_t f = 0; f < sizeof forms / sizeof forms[0]; f++) {
        printf("%-30s", forms[f].name);
        for (size_t d = 0; d < sizeof data / sizeof data[0]; d++) {
            printf("  %-30.2f", result[f][d]);
        }
        printf("\n");
    }
    return 0;
}
