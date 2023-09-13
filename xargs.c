#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
/*

Write a simple version of the Unix xargs program: read lines from the standard input and run a command for each line,
supplyying the line as arguments to the command, Your solution should be in the file user/xargs.c

*/
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs param...\n");
        exit(0);
    }
    if (argc > MAXARG) {
        fprintf(2, "xargs: too many params\n");
        exit(0);
    }
    
    char *params[MAXARG];
    for (int i = 1; i < argc; i++) {
        params[i - 1] = (char*)malloc(strlen(argv[i]) + 1);
        strcpy(params[i - 1], argv[i]);
    }

    char buf[2048];
    int idx = 0;
    int fargc = argc - 1, flag = 0;
    while (read(0, buf + idx, sizeof(char))) {
        if (buf[idx] == '\n') {
            flag = 1;
            buf[idx] = 0;
        }
        if (++idx >= 2048) {
            fprintf(2, "xargs: too many params\n");
            exit(0);
        }
        if (flag) {
            int i = 0;
            while (i < idx) {
                while (i < idx && buf[i] == ' ') {
                    ++i;
                }
                if (i >= idx) {
                    break;
                }
                int j = i;
                while (j < idx && buf[j] != ' ') {
                    ++j;
                }
                buf[j++] = 0;
                if (params[fargc] != 0) {
                    free(params[fargc]);
                    params[fargc] = 0;
                }
                params[fargc] = (char*)malloc(j - i);
                strcpy(params[fargc++], buf + i);
                i = j;
            }
            params[fargc] = 0;
            if (fork() == 0) {
                exec(params[0], params);
                exit(0);
            } else {
                wait(0);
                idx = flag = 0;
                fargc = argc - 1;
            }
        }
    }

    for (int i = 0; i < MAXARG; i++) {
        if (params[i] != 0) {
            free(params[i]);
            params[i] = 0;
        }
    }

    exit(0);

}