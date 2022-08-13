#include "kernel/types.h"
#include "user/user.h"

void print_primes(int pp)
{
    int p2c[2];
    int p, n, pid, cnt;
    p = pipe(p2c);
    if(p != 0)
        return;
    if((pid = fork()) > 0)
    {
        close(p2c[0]);
        if(pp == -1)
        {
            for(int i = 2; i <= 35; ++i)
            {
                write(p2c[1], (void *)&i, sizeof(int));
            }
            close(p2c[1]);
            wait((int*)0);
        }
        else
        {
            read(pp, (void *)&p, sizeof(int));
            printf("prime %d\n", p);
            for (;;)
            {
                cnt = read(pp, (void *)&n, sizeof(int));
                if(cnt == 0)
                {
                    break;
                }
                if(n % p != 0)
                    write(p2c[1], (void *)&n, sizeof(int));
            }
            close(p2c[1]);
            wait(&pid);
            exit(0);
        }
    }
    else if(pid == 0)
    {
        close(p2c[1]);
        print_primes(p2c[0]);
        exit(0);
    }
    else
    {
        fprintf(2, "print_primes fork failed\n");
        exit(1);
    }
}

int main()
{
    print_primes(-1);
    exit(0);
}