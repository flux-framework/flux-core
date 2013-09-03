#include <unistd.h>
#include <stdio.h>

int main (int argc, char *argv[])
{
    sleep (120);
    fprintf (stdout, 
        "woke up after sleeping 120 secs\n");
    return 0;       
}
