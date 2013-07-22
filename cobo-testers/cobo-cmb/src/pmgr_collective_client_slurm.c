#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/file.h> /* flock() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sched.h>

#include "pmgr_collective_client.h"
#include "pmgr_collective_ranges.h"
#include "pmgr_collective_client_common.h"
#include "pmgr_collective_client_tree.h"
#include "pmgr_collective_client_slurm.h"

#ifndef MPIRUN_SLURM_SHM_PREFIX
#define MPIRUN_SLURM_SHM_PREFIX ("/dev/shm")
#endif

#ifndef MPIRUN_SLURM_SHM_PORTS
//#define MPIRUN_SLURM_SHM_PORTS ("10000-10250")
//#define MPIRUN_SLURM_SHM_PORTS ("10000-10050")
#define MPIRUN_SLURM_SHM_PORTS ("10000-10025")
//#define MPIRUN_SLURM_SHM_PORTS ("10000-10010")
#endif

extern int mpirun_open_timeout;

/* wait until file reaches expected size, read in data, and return number of bytes read */
static int pmgr_slurm_wait_check_in(const char* file, int max_local, int precise, int* num_checked_in)
{
    double timelimit = 3.0; /* seconds */

    /* if each local process writes an int, this is the maximum file size */
    size_t max_size = max_local * sizeof(int);

    /* wait until the file becomes the right size (or until the timelimit expires) */
    off_t size = -1;
    struct timeval start, end;
    pmgr_gettimeofday(&start); 
    double secs = 0.0;
    while (size != (off_t) max_size) {
        /* issue a stat() to get the current filesize */
        struct stat filestat;
        stat(file, &filestat);
        size = filestat.st_size;

        /* if the count of local tasks is only a maximum and not a precise number,
         * then we limit how long we wait since our node may not actually have that
         * many tasks trying to check in */
        if (!precise) {
            pmgr_gettimeofday(&end);
            secs = pmgr_getsecs(&end, &start);
            if (secs > timelimit) {
                break;
            }
        }
    }

    /* count the number of ranks actually checked in */
    *num_checked_in = size / sizeof(int);

    return PMGR_SUCCESS;
}

/* write our rank (a single integer value) to the end of the file */
static int pmgr_slurm_check_in(const char* file, int local, int rank, int* rank_checked_in)
{
    int rc = PMGR_SUCCESS;

    int fd = -1;

    /* open file */
    /* we're careful to set permissions so only the current user can access the file,
     * which prevents another user from attaching to the file while we have it open */
    if (local == 0) {
        /* only the leader creates the file */
        fd = open(file, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
    } else {
        /* don't try to open until we see the file */
        while (access(file, F_OK) < 0) { ; }

        /* we either timed out, or the file now exists */
        if (access(file, F_OK) == 0) {
            /* the file exists, so try to open it */
            fd = open(file, O_RDWR);
        }
    }

    /* check that we got a file */
    if (fd < 0) {
        return PMGR_FAILURE;
    }

    /* wait for an exclusive lock */
    if (flock(fd, LOCK_EX) != 0) {
        pmgr_error("Failed to acquire file lock on %s: flock(%d, %d) errno=%d %m @ %s:%d",
            file, fd, LOCK_EX, errno, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* seek to end */
    off_t pos = lseek(fd, 0, SEEK_END);

    /* append our data to file */
    write(fd, &rank, sizeof(rank));

    /* fsync */
    fsync(fd);

    /* unlock the file */
    if (flock(fd, LOCK_UN) != 0) {
        pmgr_error("Failed to release file lock on %s: flock(%d, %d) errno=%d %m @ %s:%d",
            file, fd, LOCK_UN, errno, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* close */
    close(fd);

    /* get our rank according to the order that we checked in */
    *rank_checked_in = (int) (pos / sizeof(int));

    return rc;
}

/* parses SLURM_TASKS_PER_NODE to determine how many tasks are on the
 * current node, sets precise=1 if the number is exact, or 0 if just a maximum */
static int pmgr_slurm_get_max_local(const char* tasks_per_node, int* out_max, int* out_precise)
{
    /* this string comes in formats like the following:
     *   12
     *   12(x300)
     *   12(x300),1(x2),3 */
    int max = -1;
    int precise = 1;
    char* scratch = strdup(tasks_per_node);
    if (scratch != NULL) {
        const char* p = tasks_per_node;
        while (*p != '\0') {
            /* pull off the num tasks per node */
            int count = 0;
            while(*p >= '0' && *p <= '9') {
                scratch[count] = *p;
                count++;
                p++;
            }
            scratch[count] = '\0';

            /* check the value against the current max */
            if (count > 0) {
                int value = atoi(scratch);
                if (value > max) {
                    max = value;
                }
            }

            /* pull off any (x#) component */
            if (*p == '(') {
                p++;
                while (*p != ')' && *p != '\0') {
                    p++;
                }
                if (*p == ')') {
                    p++;
                }
            }

            /* pull off any commas */
            if (*p == ',') {
                /* found one comma, meaning there is more than one value,
                 * so it's not precise */
                precise = 0;

                /* remove any commans */
                while (*p == ',') {
                    p++;
                }
            }
        }

        free(scratch);
        scratch = NULL;
    }

    /* update output values with what we found */
    *out_max = max;
    *out_precise = precise;

    /* only return success if found at least one value */
    if (max > 0) {
        return PMGR_SUCCESS;
    }
    return PMGR_FAILURE;
}

/* initialize the memory region we use to execute barriers,
 * this must be done before any procs try to issue a barrier */
static void pmgr_slurm_barrier_init(void* buf, int ranks, int rank)
{
    int i;
    int* mem = buf;
    for (i = 0; i < ranks*2; i++) {
        mem[i] = 0;
    }
}

/* rank 0 waits until all procs have set their signal field to 1,
 * it then sets all signal fields back to 0 for next barrier call */
static void pmgr_slurm_barrier_signal(volatile void* buf, int ranks, int rank)
{
    int i;
    volatile int* mem = buf;
    if (rank == 0) {
        /* wait until all procs have set their field to 1 */
        for (i = 1; i < ranks; i++) { 
            while (mem[i] == 0) {
                /* make room for another process to run in case we're oversubscribed */
                sched_yield();

                /* MEM_READ_SYNC */
            }
        }

        /* set all fields back to 0 for next iteration */
        for (i = 1; i < ranks; i++){ 
            mem[i] = 0;
        }
    } else {
        /* just need to set our field to 1 */
        mem[rank] = 1;
    }
    /* MEM_WRITE_SYNC */
}

/* all ranks wait for rank 0 to set their wait field to 1,
 * each rank then sets its wait field back to 0 for next barrier call */
static int pmgr_slurm_barrier_wait(volatile void* buf, int ranks, int rank)
{
    int rc = PMGR_SUCCESS;

    int i;
    volatile int* mem = buf;
    if (rank == 0) {
        /* change all flags to 1 */
        for (i = 1; i < ranks; i++){ 
            mem[i] = 1;
        }
    } else {
        /* wait until leader flips our flag to 1 (or timelimit expires) */
        while (mem[rank] == 0) {
            /* make room for another process to run in case we're oversubscribed */
            sched_yield();

            /* MEM_READ_SYNC */
        }

        /* set our flag back to 0 for the next iteration */
        mem[rank] = 0;
    }
    /* MEM_WRITE_SYNC */

    return rc;
}

/* execute a shared memory barrier, note this is a two phase process
 * to prevent procs from escaping ahead */
static int pmgr_slurm_barrier(void* buf, int max_ranks, int ranks, int rank)
{
    pmgr_slurm_barrier_signal(buf, ranks, rank);
    int rc = pmgr_slurm_barrier_wait(buf + max_ranks * sizeof(int), ranks, rank);
    return rc;
}

/* attach to the shared memory segment, rank 0 should do this before any others */
static void* pmgr_slurm_attach_shm_segment(size_t size, const char* file, int rank)
{
    /* open the file on all processes,
     * we're careful to set permissions so only the current user can access the file,
     * which prevents another user from attaching to the file while we have it open */
    int fd = -1;
    if (rank == 0) {
        fd = open(file, O_RDWR | O_CREAT, S_IRWXU);
    } else {
        fd = open(file, O_RDWR);
    }
    if (fd < 0) {
        return NULL;
    }

    /* set the size */
    if (rank == 0) {
        ftruncate(fd, 0);
        ftruncate(fd, (off_t) size);
        lseek(fd, 0, SEEK_SET);
    }

    /* mmap the file on all tasks */
    void* ptr = mmap(0, (off_t) size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        pmgr_error("Failed to map shared memory segment (errno=%d %m) @ file %s:%d",
            errno, __FILE__, __LINE__
        );
        return NULL;
    }

    /* close the file descriptor */
    int rc = close(fd);
    if (rc == -1) {
//      return NULL;
    }

    return ptr;
}

static int pmgr_slurm_open_leaders(const char* nodelist, int node, const char* portrange, int portoffset, const char* auth, pmgr_tree_t* t)
{
    /* count number of nodes */
    int nodes;
    pmgr_range_nodelist_size(nodelist, &nodes);

    /* prepare our tree */
//    pmgr_tree_init_binomial(&node_tree, nodes, node);
    pmgr_tree_init_binary(t, nodes, node);

    /* if we're the only node in town, take a short cut */
    if (nodes == 1) {
        return PMGR_SUCCESS;
    }

    /* set up our authentication text to verify connections */
    char auth_text[1024];
    size_t auth_len = snprintf(auth_text, sizeof(auth_text), "%s::%s", auth, "LEADERS") + 1;
    if (auth_len > sizeof(auth_text)) {
        pmgr_error("Authentication text too long, %d bytes exceeds limit %d @ file %s:%d",
            auth_len, sizeof(auth_text), __FILE__, __LINE__
        );
        exit(1);
    }

    /* open a socket within port range */
    int sockfd = -1;
    struct in_addr ip;
    short port;
    if (pmgr_open_listening_socket(portrange, portoffset, &sockfd, &ip, &port) != PMGR_SUCCESS) {
        pmgr_error("Creating listening socket @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    /* open the tree */
    pmgr_tree_open_nodelist_scan(t, nodelist, portrange, portoffset, sockfd, auth_text);

    /* close our listening socket */
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }

    return PMGR_SUCCESS;
}

static int pmgr_slurm_exchange_leaders(pmgr_tree_t* t, const void* sendbuf, size_t sendcount, void* recvbuf, size_t max_recvcount, size_t* recvcount)
{
    /* if we're the only node in town, take a short cut */
    if (t->ranks == 1) {
        /* issue a collect */
        memcpy(recvbuf, sendbuf, sendcount);
        *recvcount = sendcount;
        return PMGR_SUCCESS;
    }

    /* issue a collect */
    int64_t sendcount64 = (int64_t) sendcount;
    int64_t recvcount64 = (int64_t) recvcount;
    int64_t written;
    pmgr_tree_aggregate(t, sendbuf, sendcount64, recvbuf, recvcount64, &written);
    *recvcount = (size_t) written;

    return PMGR_SUCCESS;
}

/* assumptions:
 *   - each process knows the job and job step numbers
 *   - each process on the node knows the number of procs on the node
 *   - each process on the node knows its local rank on the node
 *   - each process knows its node number
 *   - each process knows the set of nodes used in the job
 */

int pmgr_tree_open_slurm(pmgr_tree_t* t, int ranks, int rank, const char* auth)
{
    int rc = PMGR_SUCCESS;
    char* value;

    /* start timer to measure entire operation */
    struct timeval total_start, total_end;
    pmgr_gettimeofday(&total_start); 

    char* prefix = NULL;
    char* portrange = NULL;
    char* slurm_step_nodelist = NULL;
    char* slurm_step_tasks_per_node = NULL;
    int slurm_jobid   = -1;
    int slurm_stepid  = -1;
    int slurm_nodeid  = -1;
    int slurm_localid = -1;

    if ((value = getenv("MPIRUN_SLURM_SHM_PREFIX")) != NULL) {
        prefix = strdup(value);
    } else {
        prefix = strdup(MPIRUN_SLURM_SHM_PREFIX);
    }

    if ((value = getenv("MPIRUN_SLURM_SHM_PORTS")) != NULL) {
        portrange = strdup(value);
    } else {
        portrange = strdup(MPIRUN_SLURM_SHM_PORTS);
    }

    /* read SLURM environment variables */
    if ((value = getenv("SLURM_JOBID")) != NULL) {
        slurm_jobid = atoi(value);
    } else {
        pmgr_error("Failed to read SLURM_JOBID @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    if ((value = getenv("SLURM_STEPID")) != NULL) {
        slurm_stepid = atoi(value);
    } else {
        pmgr_error("Failed to read SLURM_STEPID @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    if ((value = getenv("SLURM_NODEID")) != NULL) {
        slurm_nodeid = atoi(value);
    } else {
        pmgr_error("Failed to read SLURM_NODEID @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    if ((value = getenv("SLURM_LOCALID")) != NULL) {
        slurm_localid = atoi(value);
    } else {
        pmgr_error("Failed to read SLURM_LOCALID @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    if ((value = getenv("SLURM_STEP_NODELIST")) != NULL) {
        slurm_step_nodelist = strdup(value);
    } else {
        pmgr_error("Failed to read SLURM_STEP_NODELIST @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    if ((value = getenv("SLURM_STEP_TASKS_PER_NODE")) != NULL) {
        slurm_step_tasks_per_node = strdup(value);
    } else {
        pmgr_error("Failed to read SLURM_STEP_TASKS_PER_NODE @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    /* extract number of procs on node from slurm_step_tasks_per_node */
    int max_local, precise;
    if (pmgr_slurm_get_max_local(slurm_step_tasks_per_node, &max_local, &precise) != PMGR_SUCCESS) {
        pmgr_error("Failed to parse SLURM_TASKS_PER_NODE '%s'@ file %s:%d",
            slurm_step_tasks_per_node, __FILE__, __LINE__
        );
        exit(1);
    }

    /* build file name: jobid.stepid.checkin */
    char file_check_in[1024];
    if (snprintf(file_check_in, sizeof(file_check_in), "%s/%d.%d.checkin",
        prefix, slurm_jobid, slurm_stepid) >= sizeof(file_check_in))
    {
        pmgr_error("Filename too long %s/%d.%d.checkin @ file %s:%d",
            prefix, slurm_jobid, slurm_stepid, __FILE__, __LINE__
        );
        exit(1);
    }

    /* build file name: jobid.stepid.table */
    char file_table[1024];
    if (snprintf(file_table, sizeof(file_table), "%s/%d.%d.table",
        prefix, slurm_jobid, slurm_stepid) >= sizeof(file_table))
    {
        pmgr_error("Filename too long %s/%d.%d.table @ file %s:%d",
            prefix, slurm_jobid, slurm_stepid, __FILE__, __LINE__
        );
        exit(1);
    }

    /* number of bytes for each address we'll store in table */
    size_t addr_size  = sizeof(struct in_addr) + sizeof(short); /* IP,port */
    size_t entry_size = sizeof(int) + addr_size;                /* rank,IP,port */

    /* compute the size of the shared memory segment, which will contain:
     *   buffer space to implement a shared memory barrier - 2 ints for each proc on node
     *   a table holding (rank,IP,port) info for each task on the node
     *   an integer to record the total number of ranks found in the job
     *   a table holding (IP,port) info for each task in the job, ordered by rank */
    void* segment = NULL;
    size_t barrier_offset = 0;
    size_t node_offset    = barrier_offset + max_local * 2 * sizeof(int);
    size_t count_offset   = node_offset    + max_local * entry_size;
    size_t table_offset   = count_offset   + sizeof(int);
    off_t  segment_size   = (off_t) (table_offset + ranks * addr_size);

    /* have proc with slurm_localid == 0 create and initialize shared memory segment *before* checking in */
    if (slurm_localid == 0) {
        /* create the shm segment */
        segment = pmgr_slurm_attach_shm_segment(segment_size, file_table, slurm_localid);

        /* intialize the shared memory for communication */
        if (segment != NULL) {
            /* initialize space for the barrier messages */
            pmgr_slurm_barrier_init(segment + barrier_offset, max_local, slurm_localid);
        }
    }

    /* each process writes its rank to the file via exclusive lock to check in,
     * and each process is assigned a rank depending on the order it checked in,
     * procs with slurm_localid != 0 wait on proc with slurm_localid == 0 to create file,
     * this synchronization ensures that the shared memory segment has been initialized */
    int rank_checked_in;
    if (pmgr_slurm_check_in(file_check_in, slurm_localid, rank, &rank_checked_in) != PMGR_SUCCESS) {
        pmgr_error("Failed to write rank to file @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    /* now attach everyone else to the shared memory segment */
    if (slurm_localid != 0) {
        segment = pmgr_slurm_attach_shm_segment(segment_size, file_table, slurm_localid);
    }

    /* from now on, we use rank_checked_in as the local rank of each process,
     * note that the process with slurm_localid==0 may not have gotten rank_checked_in==0 */

    /* open a tree across the leader processes */
    pmgr_tree_t leader_tree;
    pmgr_tree_init_null(&leader_tree);
    if (rank_checked_in == 0) {
        /* start timer for opening leader tree */
        struct timeval leader_open_start, leader_open_end;
        pmgr_gettimeofday(&leader_open_start); 

        /* TODO: loop over this attempt since it can timeout before completing */
        /* open our tree of leader procs */
        pmgr_slurm_open_leaders(slurm_step_nodelist, slurm_nodeid, portrange, slurm_stepid,
            auth, &leader_tree
        );

        /* stop timer for opening leader tree */
        pmgr_gettimeofday(&leader_open_end);
        pmgr_debug(2, "Leader tree open time %f seconds",
            pmgr_getsecs(&leader_open_end, &leader_open_start)
        );
    }

    /* start late check in loop, if a process is late to check in, we may need several iterations
     * until everyone is accounted for */
    int sockfd = -1;
    int have_table = 0;
    int ranks_checked_in = 0;
    while (! have_table) {
        /* check whether we've exceeded the time to try to connect everything */
        if (pmgr_have_exceeded_open_timeout()) {
            if (rank == 0) {
                pmgr_error("Exceeded time limit to startup @ file %s:%d",
                    __FILE__, __LINE__
                );
            }
            exit(1);
        }

        /* proc with rank_checked_in == 0 waits for all procs to check in,
         * only this process will know how many others are actually checked in,
         * this may return early with a count that is too small if some procs are slow to check in */
        if (rank_checked_in == 0) {
            /* wait until all procs have written to file (or until time limit expires) */
            pmgr_slurm_wait_check_in(file_check_in, max_local, precise, &ranks_checked_in);
        }

        /* at this point, we can use shared memory barriers to sync processes that have checked in */

        /* if a process checked in late, it will get stuck on the barrier below until the proc
         * with rank_checked_in == 0 eventually circles back to re-read the checkin file above,
         * only ranks which are known to be checked in will pass through */

        /* issue a barrier to signal that procs can open their listening sockets,
         * we delay this open to avoid procs accepting connections from leaders during port scans
         * when opening the leader tree */
        pmgr_slurm_barrier(segment + barrier_offset, max_local, ranks_checked_in, rank_checked_in);

        /* TODO: want to create a new listening socket each loop? */

        /* create a socket to accept connection from parent and enter it into shared memory table */
        if (sockfd == -1) {
          struct in_addr ip;
          short port;
          if (pmgr_open_listening_socket(NULL, 0, &sockfd, &ip, &port) != PMGR_SUCCESS) {
              pmgr_error("Creating listening socket @ file %s:%d",
                  __FILE__, __LINE__
              );
              exit(1);
          }

          /* write to our globalrank,ip,port info to shared memory, using the slot we got when checking in */
          void* entry_offset = segment + node_offset + rank_checked_in * entry_size;
          memcpy(entry_offset, &rank, sizeof(rank));
          entry_offset += sizeof(rank);
          memcpy(entry_offset, &ip, sizeof(ip));
          entry_offset += sizeof(ip);
          memcpy(entry_offset, &port, sizeof(port));
        }

        /* signal to leader that all procs have written their IP:port info to shared memory */
        pmgr_slurm_barrier(segment + barrier_offset, max_local, ranks_checked_in, rank_checked_in);

        /* exchange data with other leaders and record all entries in table */
        if (rank_checked_in == 0) {
            /* start timer for leader excahgne */
            struct timeval exchange_start, exchange_end;
            pmgr_gettimeofday(&exchange_start); 

            /* allocate space to hold entries from all processes on all nodes */
            size_t max_data_all_size = ranks * entry_size;
            void* data_all = pmgr_malloc(max_data_all_size, "Buffer to receive data from each node");

            /* exchange data with other leaders (use our SLURM_STEPID as a port offset value) */
            size_t recvsize;
            size_t sendsize = ranks_checked_in * entry_size;
            pmgr_slurm_exchange_leaders(&leader_tree, segment + node_offset, sendsize,
                data_all, max_data_all_size, &recvsize
            );

            /* write ip:port values to table in shared memory, ordered by global rank */
            size_t offset = 0;
            int num_ranks = 0;
            while (offset < recvsize) {
                int current_rank = *(int*) (data_all + offset);
                memcpy(segment + table_offset + current_rank * addr_size, data_all + offset + sizeof(int), addr_size);
                offset += entry_size;
                num_ranks++;
            }

            /* set the count of the total number of tasks found across all nodes */
            memcpy(segment + count_offset, &num_ranks, sizeof(int));

            /* free the buffer used to collect data */
            pmgr_free(data_all);

            /* stop timer for leader excahgne and print cost */
            pmgr_gettimeofday(&exchange_end);
            pmgr_debug(2, "Leader exchange and copy time %f seconds",
                pmgr_getsecs(&exchange_end, &exchange_start)
            );
        }

        /* signal to local procs to indicate that leader exchange is complete */
        pmgr_slurm_barrier(segment + barrier_offset, max_local, ranks_checked_in, rank_checked_in);

        /* check that number of entries matches number of ranks */
        int table_ranks = *(int*) (segment + count_offset);
        if (table_ranks == ranks) {
            /* break late check in loop */
            have_table = 1;
        } else {
            /* try again, maybe some procs were just late to check in */
            if (rank == 0) {
                pmgr_debug(1, "Missing some processes after check in, have %d expected %d @ file %s:%d",
                    table_ranks, ranks, __FILE__, __LINE__
                );
            }
        }
    }

    /* if we make it here, we now have the full IP:port table for all procs */

    /* delete the shared memory files, note the shared memory segment will
     * still exist even after deleting the files */
    if (slurm_localid == 0) {
        unlink(file_check_in);
        unlink(file_table);
    }

    /* start time to measure cost to open tree */
    struct timeval table_start, table_end;
    pmgr_gettimeofday(&table_start); 

    /* TODO: loop over this attempt since it can timeout before completing */
    /* now that we have our table, open our tree */
    pmgr_tree_open_table(t, ranks, rank, segment + table_offset, sockfd, auth);

    /* stop timer to measure cost to open tree and print cost */
    pmgr_gettimeofday(&table_end);
    pmgr_debug(2, "Open tree by table time %f seconds for %d procs",
        pmgr_getsecs(&table_end, &table_start), ranks
    );

    /* close our listening socket */
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }

    /* close the leader tree */
    if (rank_checked_in == 0) {
        /* done with the leader tree, so close it down */
        pmgr_tree_close(&leader_tree);
    }

    /* issue barrier to signal that shared memory files can be deleted */
    pmgr_slurm_barrier(segment + barrier_offset, max_local, ranks_checked_in, rank_checked_in);

    /* unmap shared memory segment */
    munmap(segment, (size_t) segment_size);

    /* free off strdup'd strings */
    if (prefix != NULL) {
        free(prefix);
        prefix = NULL;
    }

    if (portrange != NULL) {
        free(portrange);
        portrange = NULL;
    }

    if (slurm_step_nodelist != NULL) {
        free(slurm_step_nodelist);
        slurm_step_nodelist = NULL;
    }

    if (slurm_step_tasks_per_node != NULL) {
        free(slurm_step_tasks_per_node);
        slurm_step_tasks_per_node = NULL;
    }

    /* print cost of entire operation */
    pmgr_gettimeofday(&total_end);
    pmgr_debug(2, "Exiting pmgr_tree_open_slurm, took %f seconds for %d procs",
        pmgr_getsecs(&total_end, &total_start), ranks
    );
    return rc;
}
