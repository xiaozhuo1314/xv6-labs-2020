#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "sysinfo.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_sysinfo(void)
{
    uint64 st;
    struct sysinfo info;
    if(argaddr(0, &st) < 0) // 获取用户结构体参数的地址
        return -1;
    // 计算内存空闲亮和进程数
    uint64 freecnt, proccnt;
    freecount(&freecnt);
    proccount(&proccnt);
    info.freemem = freecnt;
    info.nproc = proccnt;
    if(copyout(myproc()->pagetable, st, (char *)&info, sizeof(info)) < 0) // 拷贝失败返回-1,因为有可能将地址拷贝到可用内存范围外了
        return -1;
    return 0;
}
