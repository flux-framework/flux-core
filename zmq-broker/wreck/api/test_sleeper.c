#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int sleeptime = 120;

int main (int argc, char *argv[])
{
    if (argc > 1) {
        sleeptime = atoi(argv[1]);
    }
    sleep (sleeptime);
    fprintf (stdout, 
        "woke up after sleeping %d secs\n", sleeptime);
    return 0;       
}
