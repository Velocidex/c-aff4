#include <time.h>
#include <sys/time.h>


int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    struct timeval tv;
    clk_id++;

    int res = gettimeofday(&tv, NULL);
    if (res == 0) {
        tp->tv_sec = tv.tv_sec;
        tp->tv_nsec = tv.tv_usec / 1000;
    }
    return res;
}
