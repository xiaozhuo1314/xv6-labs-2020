#include "kernel/types.h"
#include "user/user.h"

/* 实现很糙,后期需要改 */
void elapsed_time(int tick_1, int tick_2)
{
    int seconds = tick_2 % 60;
    int tmp = tick_2 / 60;
    int minutes = tmp % 60;
    int hours = tmp / 60;
    char times[128] = {0};
    int idx = 0;
    // 小时
    if(hours > 9)
    {
        times[idx++] = '0' + hours / 10;
        times[idx++] = '0' + hours % 10;
    }
    else
    {
        times[idx++] = '0';
        times[idx++] = '0' + hours;
    }
    times[idx++] = ':';
    // 分钟
    if(minutes > 9)
    {
        times[idx++] = '0' + minutes / 10;
        times[idx++] = '0' + minutes % 10;
    }
    else
    {
        times[idx++] = '0';
        times[idx++] = '0' + minutes;
    }
    times[idx++] = ':';
    // 秒
    if(seconds > 9)
    {
        times[idx++] = '0' + seconds / 10;
        times[idx++] = '0' + seconds % 10;
    }
    else
    {
        times[idx++] = '0';
        times[idx++] = '0' + seconds;
    }
    if(tick_1 != 0)
    {
        times[idx++] = '.';
        times[idx++] = '0' + tick_1;
    }
    times[idx] = 0;
    printf("System is up on time for %s\n", times);
}


int main(void)
{
    int tick = uptime();
    int tick_1 = tick % 10;
    int tick_2 = tick / 10;
    if(tick_2 == 0) //小于1秒
    {
        printf("System is up on time for 0.%d\n", tick_1);
        exit(0);
    }
    elapsed_time(tick_1, tick_2);
    exit(0);
}