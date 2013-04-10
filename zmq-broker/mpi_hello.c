#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
        int id, ntasks;
        char hostname[64];

        MPI_Init(&argc, &argv);
        MPI_Comm_rank(MPI_COMM_WORLD, &id);
        MPI_Comm_size(MPI_COMM_WORLD, &ntasks);
        if (id == 0) {
                printf("0: completed MPI_Init.  There are %d tasks\n", ntasks);
                fflush(stdout);
        }

        MPI_Barrier(MPI_COMM_WORLD);
        if (id == 0) {
                printf("0: completed first barrier\n");
                fflush(stdout);
        }

        MPI_Finalize();
        if (id == 0) {
                printf("0: completed MPI_Finalize\n");
                fflush(stdout);
        }
        return 0;
}
