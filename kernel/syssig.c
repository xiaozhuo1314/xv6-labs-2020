#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

/*
 * user add: sig return
 * 用于定时任务返回的一些事项
 */
uint64 sys_sigreturn(void)
{
    return 0;
}

/*
 * user add: sig alarm
 * 用于启动用户程序的定时任务
 */
uint64 sys_sigalarm(void)
{
    int n;
    uint64 handler;
    if(argint(0, &n) < 0 || argaddr(1, &handler) < 0 || n < 1)
        return -1;
    struct proc *p = myproc();
    p->alarminvoker.gap = n;
    p->alarminvoker.handler = (alarm_handler)handler;
    return 0;
}
