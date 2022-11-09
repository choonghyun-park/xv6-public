#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int 
main(int argc, char *argv[]) 
{
    int addr,length, prot, flags, fd, offset;
    if(argc < 7){
        printf(2, "Usage : mmap [addr] [length] [prot] [flags] [fd] [offset]\n");
        exit();
    }
    addr = atoi(argv[1]);
    length = atoi(argv[2]);
    prot = atoi(argv[3]);
    flags = atoi(argv[4]);
    fd = atoi(argv[5]);
    offset = atoi(argv[6]);
    
    mmap(addr, length, prot, flags, fd, offset);
    exit();
}
