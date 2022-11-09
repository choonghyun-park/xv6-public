#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int 
main(int argc, char *argv[]) 
{
    int addr;
    if(argc < 2){
        printf(2, "Usage : mmap [addr]\n");
        exit();
    }
    addr = atoi(argv[1]);
    
    munmap(addr);
    exit();
}
