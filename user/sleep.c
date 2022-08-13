#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        fprintf(2, "usage: sleep seconds\n");
        exit(1);
    }
    int timeout = atoi(argv[1]);
    if(timeout <= 0)
    {
        fprintf(2, "number should be more than zero\n");
        exit(1);
    }
    sleep(timeout);
    exit(0);
}