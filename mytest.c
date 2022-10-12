#include "types.h"
#include "user.h"
#include "stat.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    int num_fork;
    int pid;
    uint a=0;

    if(argc<2)
        num_fork = 1;
    else
        num_fork = atoi(argv[1]);
    pid = 0;
    
    for (int i=0;i<num_fork;i++){
        pid = fork();
        if (pid<0){
            printf(1, "%d : Wrong fork!\n ",getpid());
            ps(0);
        }
        else if (pid > 0) {
            printf(1, "Parent %d creating child %d\n",getpid(),pid);
            ps(0);
            wait();
        }
        else{
            printf(1,"Child %d created\n",getpid());
            for (int k=0; k<1000000000;k++){
                a++;
            }
            ps(0);
            break;
            
        }
    }

    // int i;
    // for (i=1; i<11; i++) {
    //     printf(1, "%d: ", i);
    //     if (getpname(i))
    //         printf(1, "Wrong pid\n");
    // }

    exit();
}