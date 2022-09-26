#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int 
main(int argc, char *argv[]) 
{
    int pid;
    if(argc < 2){
        printf(2, "Usage : ps [pid]\n");
        exit();
    }
    else if(argv[1]<0){
        printf(2, "[pid] should be positive number\n");
        exit();
    }
    pid = atoi(argv[1]);
    getnice(pid);
    exit();
}
