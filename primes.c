#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/*

Write a concurrent version of prime sieve using pipes. 
This idea is due to Doug McIlroy, inventor of Unix pipes. 
The picture halfway down this page and the surrounding text explain how to do it. 
Your solution should be in the file user/primes.c.

*/

void process(int *p) {
    int p2[2];
    int pid, x, rt, num;

    pipe(p2);
    close(p[1]);

    if ((rt = read(p[0], &x, sizeof(x))) == sizeof(x)) {
        printf("prime %d\n", x);
        if (read(p[0], &num, sizeof(num)) == 0) {
            return;
        }
        if ((pid = fork()) == 0) {
            process(p2);
        } else {  
            write(p2[1], &num, sizeof(num));  
            while (read(p[0], &num, sizeof(num)) == sizeof(num)) {
                if (num % x == 0) {
                    continue;
                }
                write(p2[1], &num, sizeof(num));        
            }
            close(p2[1]);
        }        
    } else if (rt < 0) {
        fprintf(2, "read failed\n");
        exit(0);
    } else {
        return;
    }

}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(2, "Usage: primes\n");
        exit(0);
    }

    int p[2];
    int pid;
    pipe(p);
    
    if ((pid = fork()) < 0) {
        fprintf(2, "fork failed\n");
        exit(0);
    } else if (pid == 0) {
        process(p);
    } else {
        close(p[0]);
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof(i));
        }
        close(p[1]);
    }
    wait(0);
    
    exit(0);
}