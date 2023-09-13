#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* Write a program that uses UNIX system calls to ''ping-pong'' 
a byte between two processes over a pair of pipes, one for each direction. 
The parent should send a byte to the child; the child should print "<pid>: received ping", 
where <pid> is its process ID, write the byte on the pipe to the parent, and exit; 
the parent should read the byte from the child, 
print "<pid>: received pong", and exit. Your solution should be in the file user/pingpong.c. 
*/

int main(int argc, char *argv[]) {

    if (argc != 1) {
        fprintf(2, "Usage: pingpong\n");
        exit(0);
    }

    int p2c[2], c2p[2]; // 0-read 1-write
    char ch = 'c', rc;
    int pid;
    pipe(p2c);
    pipe(c2p);

    if ((pid = fork()) < 0) {
        fprintf(2, "fork panic\n");
        exit(0);
    } else if (pid == 0) { // child proc
        close(c2p[0]);
        close(p2c[1]);

        if (read(p2c[0], &rc, sizeof(rc)) == -1) {
            fprintf(2, "child read failed\n");
            exit(0);
        }
        
        printf("%d: received ping\n", getpid());
        
        if (write(c2p[1], &rc, sizeof(rc)) == -1) {
            fprintf(2, "child write failed\n");
            exit(0);
        }
        
        close(c2p[1]);
        close(p2c[0]);
    } else {    // parent
        close(c2p[1]);
        close(p2c[0]);

        if (write(p2c[1], &ch, sizeof(ch)) == -1) {
            fprintf(2, "parent write failed\n");
            exit(0);
        }

        wait(0);

        if (read(c2p[0], &rc, sizeof(rc)) == -1) {
            fprintf(2, "parent read failed\n");
            exit(0);
        }
        printf("%d: received pong\n", getpid());

        close(c2p[0]);
        close(p2c[1]);
    }

    exit(0);
}