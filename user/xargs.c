#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXLEN 1024

int main(int argc, char *argv[])
{
    /* 
     * 如命令echo hello world | xargs echo bye
     * argc为3
     * argv[0]为xargs,argv[1]为echo,argv[2]为bye
     * 剩下的hello world都是在标准输入里面,需要用read去读取
     */
    // 读取标准输入
    char *params[MAXARG] = {0};
    char buf[MAXLEN] = {0};
    char readbuf[MAXLEN / 2] = {0}; //read函数的缓冲区
    char *p;
    // 将xargs的参数写入到params中,因为在执行exec时,例如上面的echo命令,他的第一个参数就是echo命令本身,所以i需要从1开始
    for(int i = 1; i < argc; ++i)
    {
        params[i - 1] = argv[i];
    }
    int pid, n, idx = 0, argcnt = argc - 1, cnt = 0;
    while((n = read(0, (void *)readbuf, MAXLEN / 2)) > 0)
    {
        p = readbuf;
        for(int i = 0; i < n; ++i, ++p)
        {
            if(*p == '\n')
            {
                // printf("%d\n", p - readbuf);
                buf[idx++] = 0;
                params[argcnt++] = buf + (idx - cnt - 1);
                cnt = 0;
                if((pid = fork()) == 0)
                {
                    exec(argv[1], params);
                    printf("never be here\n");
                    exit(1);
                }
                else if(pid > 0)
                {
                    wait(&pid);
                    argcnt = argc - 1;
                    idx = 0;
                }
                else
                {
                    fprintf(2, "xargs fork failed\n");
                    exit(1);
                }
            }
            else if(*p == ' ')
            {
                buf[idx++] = 0;
                params[argcnt++] = buf + (idx - cnt - 1);
                cnt = 0;
            }
            else
            {
                ++cnt;
                buf[idx++] = *p;
            }
        }
    }
    exit(0);
}