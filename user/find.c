#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

char* fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  buf[strlen(p)] = 0;
  return buf;
}

void find(char *path, char *name)
{
    int fd;
    if((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: open failed\n");
        return;
    }
    struct stat st;
    struct dirent de;
    char buf[512];
    char *p;
    // 拿到文件信息
    if(fstat(fd, &st) < 0)
    {
        close(fd);
        fprintf(2, "find: fstat failed\n");
        return;
    }
    switch (st.type)
    {
    case T_DIR:
        strcpy(buf, path);
        p = buf + strlen(path);
        *(p++) = '/';
        // printf("%s\n", buf);
        while(read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if(de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            
            strcpy(p, de.name);
            p[strlen(de.name)] = 0;
            if(strcmp(de.name, name) == 0)
            {
                printf("%s\n", buf);
            }
            // printf("%s\n", buf);
            find(buf, name);
        }
        break;
    default:
        break;
    }
    close(fd);
}

int main(int argc, void *argv[])
{
    if(argc < 3)
    {
        fprintf(2, "usage: find path name\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}