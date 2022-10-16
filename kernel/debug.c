#include "types.h"
#include "riscv.h"
#include "defs.h"

/* 是否是ascii */
#define is_ascii(x) ((x >= 0) && (x <= 127))
/* 是否是可打印的字符,即不包含回车等特殊字符 */
#define can_print(x) ((x >= 32) && (x <= 126))

/*
  +------+-------------------------------------------------+------------------+
  | 0000 | 68 65 6c 6c 6f 20 77 6f 72 6c 64 20 68 65 6c 6c | hello world hell |
  | 0010 | 6f 20 77 6f 72 6c 64 20 68 65 6c 6c 6f 20 77 6f | o world hello wo |
  | 0020 | 72 6c 64                                        | rld              |
  +------+-------------------------------------------------+------------------+
  hexdump函数要实现上面的效果
  1.数据的每16个字节为一行
 */

void hexdump(void *data, uint size)
{
    int idx, x;
    unsigned char *str = (unsigned char *)data;
    printf("+------+-------------------------------------------------+------------------+\n");
    // 每16个字节为一行
    for(idx = 0; idx < (int)size; idx += 16)
    {
        printf("| "); // 打印每一行最开始的| 
        // 下面要去打印每一行最开始数字,也就是该行最开始的字符属于第几个字节,是十六进制的四个数字,如0010
        if(idx <= 0x0fff)
            printf("0");
        if(idx <= 0x00ff)
            printf("0");
        if(idx <= 0x000f)
            printf("0");
        printf("%x | ", idx);
        // 下面去打印十六个字节,如6f 20 77 6f 72 6c 64 20 68 65 6c 6c 6f 20 77 6f
        for(x = 0; x < 16; ++x)
        {
            if(idx + x < (int)size) // 小于size就需要打印字节
            {
                if(str[idx + x] <= 0x0f) //小于0x0f,前面要补0
                    printf("0");
                printf("%x ", 0xff & str[idx + x]);
            }
            else // 否则就如同例子中的那样,最后一行剩余部分打印空格
            {
                printf("   ");
            }
        }
        printf("| ");
        // 下面去打印字符,如o world hello wo
        for(x = 0; x < 16; ++x)
        {
            if(idx + x < (int)size)
            {
                if(is_ascii(str[idx + x]) && can_print(str[idx + x]))
                {
                    printf("%c", str[idx + x]);
                }
                else
                {
                    printf(".");
                }
            }
            else
            {
                printf(" ");
            }
        }
        printf(" |\n");
    }
    printf("+------+-------------------------------------------------+------------------+\n");
}