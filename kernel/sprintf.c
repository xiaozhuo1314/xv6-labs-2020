#include <stdarg.h>

#include "types.h"
#include "riscv.h"
#include "defs.h"

static char digits[] = "0123456789abcdef";

static int sputc(char *s, char c)
{
    *s = c;
    return 1;
}

/* 
 * buf指的是存放的数据存放的地方 
 * num指的要存放的数字
 * base指的进制
 * max_sz指的buf中最多还可以放多少个字符
 */
static int sprintint(char *buf, int num, int base, int max_sz)
{
    char num_buf[16];
    int idx = 0, len = 0;
    uint unum = num < 0 ? -num : num;
    
    // 将数据从后往前按照进位搞到num_buf中
    do
    {
        num_buf[idx++] = digits[unum % base];
        unum /= base;

    } while (unum != 0);
    if(num < 0)
        num_buf[idx++] = '-';
    if(idx > max_sz)
        return -1;
    // 将数据复制回buf中
    while(--idx >= 0)
        len += sputc(buf + len, num_buf[idx]);
    return len;
}

/*
 * buf指的是存放的数据存放的地方 
 * src指的是数据来源
 * sz指的是要拷贝多少字符到buf
 * max_sz指的buf中最多还可以放多少个字符 
 */
static int sprintstr(char *buf, char *src, int sz, int max_sz)
{
    if(src == 0)
    {
        src = "(null)";
        sz = 6;
    }
    if(sz > max_sz)
        return -1;
    int len = 0;
    for(; *src && sz > 0; src++, sz--)
        len += sputc(buf + len, *src);
    return len;
}

int snprintf(char *buf, int size, char *fmt, ...)
{
    va_list ap;
    char c;
    int idx, ret, len = 0; // len表示往buf拷贝了多少个字符,根据Linux中snprintf的定义,len最多为size - 1
    char *str;

    if(fmt == 0)
        panic("fmt is null");
    
    va_start(ap, fmt);
    for(idx = 0; len < size && (c = (fmt[idx] & 0xff)) != 0;)
    {
        if(c != '%')
        {
            len += sputc(buf + len, c);
            continue;
        }
        // 此时的c为%,跳过%,看下一个字符是什么
        if((c = (fmt[++idx] & 0xff)) == 0)
            break;
        // 判断字符
        switch (c)
        {
        case 'd':
            ret = sprintint(buf + len, va_arg(ap, int), 10, size - 1 - len);
            break;
        case 'x':
            ret = sprintint(buf + len, va_arg(ap, int), 16, size - 1 - len);
            break;
        case 'c':
        case 's':
            str = va_arg(ap, char*);
            ret = sprintstr(buf + len, str, strlen(str), size - 1 - len);
            break;
        case '%':
            ret = sputc(buf + len, '%');
            break;
        default:
            ret = sputc(buf + len, '%');
            ret += sputc(buf + len + 1, c);
            break;
        }
        if(ret > 0)
            len += ret;
    }
    return len;
}