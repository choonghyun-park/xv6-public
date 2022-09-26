#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int 
main(int argc, char *argv[]) 
{
    int pid, value;
    if(argc < 3){
        printf(2, "Usage : ps [pid] [value]\n");
        exit();
    }
    pid = atoi(argv[1]);
    value = atoi(argv[2]);
    if (value < 0 || value > 39){
        printf(2, "Invalid value (0-39)\n");
        exit();
    }
    setnice(pid, value);
    exit();
}
