#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

/*
 * Write a simple version of UNIX find program: find all the files in a directory tree
 * with a specific name. 
 *
 * */

/*
* steal from ls.c
*/
char*
fmtname(char *path)
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
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

/*
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};

*/

void find(char *path, char *name) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if (read(fd, &de, sizeof(de)) != sizeof(de)) {
        fprintf(2, "find: cannot read dirent %d\n", fd);
        exit(0);
    }

    switch (st.type) {
    case T_FILE:
        if (strcmp(de.name, name) == 0) {
            printf("%s/%s\n", path, name);
        }
        break;
    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
                continue;
            }
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0) {
                printf("find: cannot stat %s\n", buf);
                continue;
            }

            switch (st.type) {
            case T_FILE:
                if (strcmp(de.name, name) == 0) {
                    printf("%s\n", buf);
                }
                break;
            case T_DIR:
                find(buf, name);
                break;
            }
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "Usage: find directory name");
        exit(0);
    }

    char *path = argv[1];
    char *name = argv[2];
    find(path, name);
    exit(0);
}
