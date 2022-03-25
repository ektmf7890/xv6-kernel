#include "types.h"
#include "user.h"
#include "stat.h"

int main(int argc, char* argv[])
{
    int rc = fork();
    
    if(rc < 0){
        printf(1, "fork failed\n");
        exit();
    }
    else if (rc == 0){
        int i;
        for(i=0;i<10;i++){
            printf(1, "Child\n");
            yield();
        }
        exit();
    }
    else{
        int i;
        for(i=0;i<10;i++){
            printf(1, "Parent\n");
            yield();
        }
        exit();
    }
}
