//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 'c':
      consputc((int)va_arg(ap, int));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&pr.lock);
}

void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}

/* 下面是snprintf的内容 */

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
    int c, idx, ret, len = 0; // len表示往buf拷贝了多少个字符,根据Linux中snprintf的定义,len最多为size - 1
    char *str;

    if(fmt == 0)
        panic("fmt is null");
    
    va_start(ap, fmt);
    for(idx = 0; len < size && (c = (fmt[idx] & 0xff)) != 0; idx++)
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
          ret = sputc(buf + len, va_arg(ap, int));
          break;
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
    va_end(ap);
    buf[len] = 0;
    return len;
}