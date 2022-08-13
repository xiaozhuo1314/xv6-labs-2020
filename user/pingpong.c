#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int p2c[2];
    int c2p[2];
    int ret, pid;
    if((ret = pipe(p2c)) != 0)
    {
        fprintf(2, "pingpong: create p2c pipe failed\n");
        exit(1);
    }
    if((ret = pipe(c2p)) != 0)
    {
        fprintf(2, "pingpong: create c2p pipe failed\n");
        exit(1);
    }
    if((pid = fork()) > 0)
    {
        close(p2c[0]);
        close(c2p[1]);
        write(p2c[1], 0, 1);
        if(read(c2p[0], 0, 1))
            printf("%d: received pong\n", getpid());
        close(p2c[1]);
        close(c2p[0]);
    }
    else if(pid == 0)
    {
        close(p2c[1]);
        close(c2p[0]);
        if(read(p2c[0], 0, 1))
            printf("%d: received ping\n", getpid());
        write(c2p[1], 0, 1);
        close(p2c[0]);
        close(c2p[1]);
    }
    else
    {
        fprintf(2, "pingpong: fork failed\n");
        exit(1);
    }
    exit(0);
}