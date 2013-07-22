/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

/* Copyright (c) 2002-2007, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

/*
 * ==================================================================
 * This file contains the source for a simple MPI process manager
 * used by MVICH.
 * It simply collects the arguments and execs either RSH or SSH
 * to execute the processes on the remote (or local) hosts.
 * Some critical information is passed to the remote processes
 * through environment variables using the "env" utility. 
 *
 * The information passed through the environment variables is:
 *  MPIRUN_HOST = host running this mpirun_rsh command
 *  MPIRUN_PORT = port number mpirun_rsh is listening on for TCP connection
 *  MPIRUN_RANK = numerical MPI rank of remote process
 *  MPIRUN_NPROCS = number of processes in application
 *  MPIRUN_ID   = pid of the mpirun_rsh process
 *
 * The remote processes use this to establish TCP connections to
 * this mpirun_rsh process.  The TCP connections are used to exchange
 * address data needed to establish the VI connections.
 * The TCP connections are also used for a simple barrier syncronization
 * at process termination time.
 *
 * MVICH allows for the specification of certain tuning parameters
 * at run-time.  These parameters are read by mpirun_rsh from a
 * file given on the command line.  Currently, these parameters are
 * passed to the remote processes through environment variables, but 
 * they could be sent as a string over the TCP connection.  It was
 * thought that using environment variables might be more portable
 * to other process managers.
 * ==================================================================
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#ifdef MAC_OSX
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <assert.h>
#include <fcntl.h>

#include "pmgr_collective_mpirun.h"


#define BASE_ENV_LEN 17
typedef enum {
    P_NOTSTARTED,
    P_STARTED,
    P_CONNECTED,
    P_DISCONNECTED,
    P_RUNNING,
    P_FINISHED,
    P_EXITED
} process_state;

typedef struct {
    char *hostname;
    char *device;
    pid_t pid;
    pid_t remote_pid;
    int port;
    int control_socket;
    process_state state;
} process;

typedef struct {
    const char * hostname;
    pid_t * pids;
    size_t npids, npids_allocated;
} process_group;

typedef struct {
    process_group * data;
    process_group ** index;
    size_t npgs, npgs_allocated;
} process_groups;

#define RUNNING(i) ((plist[i].state == P_STARTED ||                 \
            plist[i].state == P_CONNECTED ||                        \
            plist[i].state == P_RUNNING) ? 1 : 0)

/* other information: a.out and rank are implicit. */

process_groups * pglist = NULL;
process * plist = NULL;
int nprocs = 0;
int aout_index, port;
#define MAX_WD_LEN 256
char wd[MAX_WD_LEN];            /* working directory of current process */
#define MAX_HOST_LEN 256
char mpirun_host[MAX_HOST_LEN]; /* hostname of current process */
/* xxx need to add checking for string overflow, do this more carefully ... */

/*
 * Message notifying user of what timed out
 */
static const char * alarm_msg = NULL;

#define COMMAND_LEN 2000
#define SEPARATOR ':'

void free_memory(void);
void pglist_print(void);
void pglist_insert(const char * const, const pid_t const);
void rkill_fast(void);
void rkill_linear(void);
void cleanup_handler(int);
void nostop_handler(int);
void alarm_handler(int);
void child_handler(int);
void usage(void);
int start_process(int i, char *command_name, char *env);
void cleanup(void);
char *skip_white(char *s);
char *read_param_file(char *paramfile,char *env);
void process_termination(void);
void wait_for_errors(int s,struct sockaddr_in *sockaddr,unsigned int sockaddr_len);
int set_fds(fd_set * rfds, fd_set * efds);
static int read_hostfile(char *hostfile_name);

#define RSH_CMD	"/usr/bin/rsh"
#define SSH_CMD	"/usr/bin/ssh"
#define DEFAULT_SHELL "/bin/sh"

#ifndef PARAM_GLOBAL
#define PARAM_GLOBAL "/etc/mvapich.conf"
#endif

#ifndef LD_LIBRARY_PATH_MPI
#define LD_LIBRARY_PATH_MPI "/usr/mvapich/lib/shared"
#endif

#ifdef USE_SSH
int use_rsh = 0;
#else
int use_rsh = 1;
#endif

#define SSH_ARG "-q"
#define SH_ARG "-c"

#define XTERM "/usr/X11R6/bin/xterm"
#define ENV_CMD "/usr/bin/env"
#define TOTALVIEW_CMD "/usr/totalview/bin/totalview"

static struct option option_table[] = {
    {"np", required_argument, 0, 0},
    {"debug", no_argument, 0, 0},
    {"xterm", no_argument, 0, 0},
    {"hostfile", required_argument, 0, 0},
    {"paramfile", required_argument, 0, 0},
    {"show", no_argument, 0, 0},
    {"rsh", no_argument, 0, 0},
    {"ssh", no_argument, 0, 0},
    {"help", no_argument, 0, 0},
    {"v", no_argument, 0, 0},
    {"tv", no_argument, 0, 0},
    {0, 0, 0, 0}
};

#define MVAPICH_VERSION "0.9.9"

#ifndef MVAPICH_BUILDID
#define MVAPICH_BUILDID "custom"
#endif

static void *safe_malloc(size_t size)
{
    void *result = malloc(size);

    if (result == NULL) {
        fprintf(stderr, "mpirun: malloc (%ld): Out of memory\n", size);
        exit(1);
    }
    return result;
}

static void *safe_realloc(void *ptr, size_t size)
{
    void *result = realloc(ptr, size);

    if (result == NULL) {
        fprintf(stderr, "mpirun: realloc (%ld): Out of memory\n", size);
        exit(1);
    }
    return result;
}

static char *safe_strdup(char *str)
{
    char *result = strdup(str);

    if (result == NULL) {
        fprintf(stderr, "mpirun: strdup (%s): Out of memory\n", str);
        exit(1);
    }
    return result;
}

static void show_version(void)
{
    fprintf(stderr,"OSU MVAPICH VERSION %s-SingleRail\n"
            "Build-ID: %s\n", MVAPICH_VERSION, MVAPICH_BUILDID);
}

int debug_on = 0, xterm_on = 0, show_on = 0;
int param_debug = 0;
int use_totalview = 0;
static char display[200];

static void get_display_str()
{
    char *p;
    char str[200];

    if ( (p = getenv( "DISPLAY" ) ) != NULL ) {
	strcpy(str, p ); /* For X11 programs */  
	sprintf(display,"DISPLAY=%s",str); 	
    }
}

int main(int argc, char *argv[])
{
    int i, s, s1, c, option_index;
    int hostfile_on = 0;
#define HOSTFILE_LEN 256
    char hostfile[HOSTFILE_LEN + 1];
    int paramfile_on = 0;
#define PARAMFILE_LEN 256
    char paramfile[PARAMFILE_LEN + 1];
    char *param_env;
    FILE *hf;
    struct sockaddr_in sockaddr;
    unsigned int sockaddr_len = sizeof(sockaddr);
    int addrlen, global_addrlen = 0;

    char *env = "\0";
    int tot_nread = 0;

    int *alladdrs = NULL;
    char *alladdrs_char = NULL; /* for byte location */
    int *out_addrs;
    int out_addrs_len;
    int j;

    char *allpids = NULL;
    int pidlen, pidglen;

    char command_name[COMMAND_LEN];
    char command_name_tv[COMMAND_LEN];
    char totalview_cmd[200];
    char *tv_env;

    int hostidlen, global_hostidlen = 0;
    int *hostids = NULL;

    int    hostname_len = 256;
    totalview_cmd[199] = 0;
    display[0]='\0';	
    pidglen = sizeof(pid_t); 

    /* mpirun [-debug] [-xterm] -np N [-hostfile hfile | h1 h2 h3 ... hN] a.out [args] */

    atexit(free_memory);

    do {
        c = getopt_long_only(argc, argv, "+", option_table, &option_index);
        switch (c) {
        case '?':
        case ':':
            usage();
	    exit(EXIT_FAILURE);
            break;
        case EOF:
            break;
        case 0:
            switch (option_index) {
            case 0:
                nprocs = atoi(optarg);
                if (nprocs < 1) {
                    usage();
		    exit(EXIT_FAILURE);
		}
                break;
            case 1:
                debug_on = 1;
                xterm_on = 1;
                break;
            case 2:
                xterm_on = 1;
                break;
            case 3:
                hostfile_on = 1;
                strncpy(hostfile, optarg, HOSTFILE_LEN);
                if (strlen(optarg) >= HOSTFILE_LEN - 1)
                    hostfile[HOSTFILE_LEN] = '\0';
                break;
            case 4:
                paramfile_on = 1;
                strncpy(paramfile, optarg, PARAMFILE_LEN);
                if (strlen(optarg) >= PARAMFILE_LEN - 1) {
                    paramfile[PARAMFILE_LEN] = '\0';
                }
                break;
            case 5:
                show_on = 1;
                break;
            case 6:
                use_rsh = 1;
                break;
            case 7:
                use_rsh = 0;
                break;
            case 8:
                show_version();
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 9:
                show_version();
                exit(EXIT_SUCCESS);
                break;
 	    case 10:
 		use_totalview = 1;
		debug_on = 1;
 		tv_env = getenv("TOTALVIEW");
 		if(tv_env != NULL) {
		    strcpy(totalview_cmd,tv_env);	
 		} else {
		    fprintf(stderr,
			    "TOTALVIEW env is NULL, use default: %s\n", 
			    TOTALVIEW_CMD);
		    sprintf(totalview_cmd, "%s", TOTALVIEW_CMD);
 		}	
  		break;
            case 11:
                usage();
                exit(EXIT_SUCCESS);
                break;
            default:
                fprintf(stderr, "Unknown option\n");
                usage();
		exit(EXIT_FAILURE);
                break;
            }
            break;
        default:
            fprintf(stderr, "Unreachable statement!\n");
            usage();
	    exit(EXIT_FAILURE);
            break;
        }
    } while (c != EOF);

    if (!hostfile_on) {
        /* get hostnames from argument list */
        if (argc - optind < nprocs + 1)
	    aout_index = optind;
        else
	    aout_index = nprocs + optind;

    } else {
        aout_index = optind;
    }

    /* reading default param file */
    if ( 0 == (access(PARAM_GLOBAL, R_OK))) {
	    env=read_param_file(PARAM_GLOBAL,env);
    }

    /* reading file specified by user env */
    if (( param_env = getenv("MVAPICH_DEF_PARAMFILE")) != NULL ){
	    env = read_param_file(param_env, env);
    }
    if (paramfile_on) {
        /* construct a string of environment variable definitions from
         * the entries in the paramfile.  These environment variables
         * will be available to the remote processes, which
         * will use them to over-ride default parameter settings
         */
        env = read_param_file(paramfile, env);
    }
	    	

    plist = safe_malloc(nprocs * sizeof(process));

    for (i = 0; i < nprocs; i++) {
        plist[i].state = P_NOTSTARTED;
        plist[i].device = NULL;
        plist[i].port = -1;
	plist[i].remote_pid = 0;
    }

    /* grab hosts from command line or file */

    if (hostfile_on) {
        hostname_len = read_hostfile(hostfile);
    } else {
        for (i = 0; i < nprocs; i++) {
            plist[i].hostname = (char *)strndup(argv[optind + i], 100);
    	    hostname_len = hostname_len > strlen(plist[i].hostname) ?
    		    hostname_len : strlen(plist[i].hostname); 
        }
    }

    getcwd(wd, MAX_WD_LEN);
    gethostname(mpirun_host, MAX_HOST_LEN);

    get_display_str();

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    sockaddr.sin_port = 0;
    if (bind(s, (struct sockaddr *) &sockaddr, sockaddr_len) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (getsockname(s, (struct sockaddr *) &sockaddr, &sockaddr_len) < 0) {
        perror("getsockname");
        exit(EXIT_FAILURE);
    }

    port = (int) ntohs(sockaddr.sin_port);
    listen(s, nprocs);


    if (!show_on) {
	struct sigaction signal_handler;
	signal_handler.sa_handler = cleanup_handler;
	sigfillset(&signal_handler.sa_mask);
	signal_handler.sa_flags = 0;

	sigaction(SIGHUP, &signal_handler, NULL);
	sigaction(SIGINT, &signal_handler, NULL);
	sigaction(SIGTERM, &signal_handler, NULL);

	signal_handler.sa_handler = nostop_handler;

	sigaction(SIGTSTP, &signal_handler, NULL);

	signal_handler.sa_handler = alarm_handler;

	sigaction(SIGALRM, &signal_handler, NULL);

	signal_handler.sa_handler = child_handler;
	sigemptyset(&signal_handler.sa_mask);

	sigaction(SIGCHLD, &signal_handler, NULL);
    }

    alarm(1000);
    alarm_msg = "Timeout during client startup.\n";
    /* long timeout for testing, where process may be stopped in debugger */

#ifdef USE_DDD
#define DEBUGGER "/usr/bin/ddd"
#else
#define DEBUGGER "gdb"
#endif

    if (debug_on) {
	char keyval_list[COMMAND_LEN];
	sprintf(keyval_list, "%s", " ");
	/* Take more env variables if present */
	while (strchr(argv[aout_index], '=')) {
	    strcat(keyval_list, argv[aout_index]);
	    strcat(keyval_list, " ");
	    aout_index ++;
	}
	if(use_totalview) {
	    sprintf(command_name_tv, "%s %s %s", keyval_list, 
		    totalview_cmd, argv[aout_index]);
	    sprintf(command_name, "%s %s ", keyval_list, argv[aout_index]);
       	} else {
	    sprintf(command_name, "%s %s %s", keyval_list, 
		    DEBUGGER, argv[aout_index]);
	}
    } else {
        sprintf(command_name, "%s", argv[aout_index]);
    }

    if(use_totalview) {
	/* Only needed for root */
	strcat(command_name_tv, " -a ");
    }

    /* add the arguments */
    for (i = aout_index + 1; i < argc; i++) {
        strcat(command_name, " ");
        strcat(command_name, argv[i]);
    }

    if(use_totalview) {
        /* Complete the command for non-root processes */
        strcat(command_name, " -mpichtv");

        /* Complete the command for root process */
        for (i = aout_index + 1; i < argc; i++) {
            strcat(command_name_tv, " ");
            strcat(command_name_tv, argv[i]);
        }
        strcat(command_name_tv, " -mpichtv");
    }

    /* start all processes */
    for (i = 0; i < nprocs; i++) {
        if((use_totalview) && (i == 0)) {
            if (start_process(i, command_name_tv, env) < 0) {
                fprintf(stderr, 
                        "Unable to start process %d on %s. Aborting.\n", 
                        i, plist[i].hostname);
                cleanup();
            }
        } else {
            if (start_process(i, command_name, env) < 0) {
                fprintf(stderr, 
                        "Unable to start process %d on %s. Aborting.\n", 
                        i, plist[i].hostname);
                cleanup(); 
            } 
        }
    }

    if (show_on)
        exit(EXIT_SUCCESS);
    
    /*Hostid exchange start */
    /* accept incoming connections, read port numbers */
   
    int version;
    for (i = 0; i < nprocs; i++) {
        int rank, nread;
        char pidstr[12];
ACCEPT_HID:
        sockaddr_len = sizeof(sockaddr);
        s1 = accept(s, (struct sockaddr *) &sockaddr, &sockaddr_len);

	alarm_msg = "Timeout during hostid exchange.\n";

        if (s1 < 0) {
            if ((errno == EINTR) || (errno == EAGAIN))
                goto ACCEPT_HID;
            perror("accept");
            cleanup();
        }

        /*
         * protocol:
         *  0. read protocol version number
         *  1. read rank of process
         *  2. read hostid length
         *  3. read hostid itself
         *  4. send array of all addresses
         */

        /* 0. Find out what version of the startup protocol the executable
         * was compiled to use. */

        nread = read(s1, &version, sizeof(version));
  
        if (nread != sizeof(version)) {
            perror("read");
            cleanup();
        }
 
        if (version != PMGR_COLLECTIVE) { 
            fprintf(stderr, "mpirun: executable version %d does not match"
                    " our version %d.\n", version, PMGR_COLLECTIVE);
            cleanup();
        }

        /* 1. Find out who we're talking to */
        nread = read(s1, &rank, sizeof(rank));

        if (nread != sizeof(rank)) {
            perror("read");
            cleanup();
        }

        if (rank < 0 || rank >= nprocs || plist[rank].state != P_STARTED) {
            fprintf(stderr, "mpirun: invalid rank received. \n");
            cleanup();
        }
        plist[rank].control_socket = s1;

    } /* end for loop to accept incoming connections */

    /* cancel the timeout */
    alarm(0);

    /* build up an array of file descriptors for pmgr_processops */
    /* allocate the array */
    int* fds = (int*) malloc(nprocs*sizeof(int));
    if (fds == NULL) {
        perror("allocating temporary array for socket file descriptors");
        cleanup();
    }

    /* fill it in */
    for(i = 0; i < nprocs; i++)
        fds[i] = plist[i].control_socket;

    /* process the collectives */
    pmgr_processops(fds, nprocs);

    /* free it off (processops closes each socket before returning control) */
    free(fds);

    for (i = 0; i < nprocs; i++)
        plist[i].state = P_RUNNING;

    wait_for_errors(s,&sockaddr,sockaddr_len);
    /* this while is unused now. We are in block wait in wait_for_errors */

    while (1) {
        sleep(100);
    }

    close(s);
    exit(EXIT_SUCCESS);
}

int remote_host(char *rhost)
{
    char lhost[64];

    if (!strcmp(rhost, "localhost") || !strcmp(rhost, "127.0.0.1"))
        return 0;
    if (!gethostname(lhost, sizeof(lhost)) && !strcmp(rhost, lhost))
        return 0;
    return 1;
}

char *lookup_shell(void)
{
    char *shell = getenv("SHELL");

    return safe_strdup(shell ? shell : DEFAULT_SHELL);
}

int start_process(int i, char *command_name, char *env)
{
    char *remote_command;
    char *xterm_command;
    char xterm_title[100];
    int use_sh = !remote_host(plist[i].hostname);
    char *sh_cmd = lookup_shell();

    char *ld_library_path;

    char *device_port_env = NULL;
    int id = getpid();

    int str_len, len;
    if (plist[i].device != NULL && strlen(plist[i].device) != 0){
        device_port_env = (char * )safe_malloc(BASE_ENV_LEN + strlen(plist[i].device) + 1);
        sprintf(device_port_env, "VIADEV_DEVICE=%s \0", plist[i].device);
    }
    if (plist[i].port != -1){
        if (device_port_env != NULL){
            device_port_env = (char*)safe_realloc(device_port_env,
                    strlen(device_port_env) + 1 + BASE_ENV_LEN 
                    +sizeof(plist[i].port) + 1);
            sprintf(&device_port_env[strlen(device_port_env)], "VIADEV_DEFAULT_PORT=%d \0",
                    plist[i].port);
        } else {
            device_port_env = (char *) safe_malloc(BASE_ENV_LEN +
                    sizeof(plist[i].port) + 1);
            sprintf(device_port_env, "VIADEV_DEFAULT_PORT=%d \0", plist[i].port);
        }
    }

    if (device_port_env==NULL) {
        device_port_env=safe_strdup("\0");
    }

    if(use_totalview) {
        str_len = strlen(command_name) + strlen(env) + strlen(wd) +
            strlen(device_port_env) + 512;
    } else {
        str_len = strlen(command_name) + strlen(env) + strlen(wd) +
            strlen(device_port_env) + 530;
    }

    if ((ld_library_path = getenv( "LD_LIBRARY_PATH" ) ) != NULL ) {
        str_len += strlen(ld_library_path);
    }

    remote_command = safe_malloc(str_len);
    xterm_command = safe_malloc(str_len);

    /* 
     * this is the remote command we execute whether we were are using 
     * an xterm or using rsh directly 
     */
    if (ld_library_path != NULL ) {
        sprintf(remote_command, "cd %s; %s LD_LIBRARY_PATH=%s:%s "
                "MPIRUN_MPD=0 MPIRUN_HOST=%s MPIRUN_PORT=%d "
                "MPIRUN_RANK=%d MPIRUN_NPROCS=%d MPIRUN_ID=%d %s %s %s",
                wd, ENV_CMD,LD_LIBRARY_PATH_MPI,ld_library_path, 
                mpirun_host, port, i,
                nprocs, id, display,env,device_port_env);
    } else {
        sprintf(remote_command, "cd %s; %s LD_LIBRARY_PATH=%s "
                "MPIRUN_MPD=0 MPIRUN_HOST=%s MPIRUN_PORT=%d "
                "MPIRUN_RANK=%d MPIRUN_NPROCS=%d MPIRUN_ID=%d %s %s %s",
                wd, ENV_CMD,LD_LIBRARY_PATH_MPI, mpirun_host, port, i,
                nprocs, id, display,env,device_port_env);
    }
    
    len = sprintf(remote_command, "%s %s ", remote_command, command_name);

    if (len > str_len) {
	    fprintf(stderr, "Internal error - overflowed remote_command\n");
	    exit(1);
    }

    if (xterm_on) {
        sprintf(xterm_command, "%s; echo process exited", remote_command);
        sprintf(xterm_title, "\"mpirun process %d of %d\"", i, nprocs);
    }

    plist[i].pid = fork();
    plist[i].state = P_STARTED; 
    /* putting after fork() avoids a race */
   
    if (plist[i].pid == 0) {
        if (i != 0) {
            int fd = open("/dev/null", O_RDWR, 0);
            (void) dup2(fd, STDIN_FILENO);
        }

        if (xterm_on) {
            if (show_on) {
                if (use_sh) {
                    printf("command: %s -T %s -e %s %s \"%s\"\n", XTERM,
                           xterm_title, sh_cmd, SH_ARG,
                           xterm_command);
                } else if (use_rsh) {
                    printf("command: %s -T %s -e %s %s %s\n", XTERM,
                           xterm_title, RSH_CMD, plist[i].hostname,
                           xterm_command);
                } else { /* ssh */
                    printf("command: %s -T %s -e %s %s %s %s\n", XTERM,
                           xterm_title, SSH_CMD, SSH_ARG, plist[i].hostname,
                           xterm_command);
                }
            } else {
                if (use_sh) {
                    execl(XTERM, XTERM, "-T", xterm_title, "-e",
                          sh_cmd, SH_ARG, xterm_command, NULL);
                } else if (use_rsh) {
                    execl(XTERM, XTERM, "-T", xterm_title, "-e",
                          RSH_CMD, plist[i].hostname, xterm_command, NULL);
                } else { /* ssh */
                    execl(XTERM, XTERM, "-T", xterm_title, "-e",
                          SSH_CMD, SSH_ARG, plist[i].hostname,
                          xterm_command, NULL);
                }
            }
        } else {
            if (show_on) {
                if (use_sh) {
                    printf("command: %s %s \"%s\"\n", sh_cmd, SH_ARG,
                           remote_command);
                } else if (use_rsh) {
                    printf("command: %s %s %s\n", RSH_CMD,
                           plist[i].hostname, remote_command);
                } else { /* ssh */
                    printf("command: %s %s %s %s\n", SSH_CMD, SSH_ARG,
                           plist[i].hostname, remote_command);
                }
            } else {
                if (use_sh) {
                    execl(sh_cmd, sh_cmd, SH_ARG,
                          remote_command, NULL);
                } else if (use_rsh) {
                    execl(RSH_CMD, RSH_CMD, plist[i].hostname,
                          remote_command, NULL);
                } else {
                    execl(SSH_CMD, SSH_CMD, SSH_ARG, plist[i].hostname,
                          remote_command, NULL);
                }
            }
        }
        if (!show_on) {
            perror("RSH/SSH command failed!");
        }
        exit(EXIT_FAILURE);
    }

    free(remote_command);
    free(xterm_command);
    free(sh_cmd);
    return (0);
}

void wait_for_errors(int s,struct sockaddr_in *sockaddr,unsigned int sockaddr_len){
 
    int nread,remote_id,local_id,s1,i,flag;
 
ACCEPT_WFE:
    s1 = accept(s,(struct sockaddr *) sockaddr,&sockaddr_len);
    if (s1 < 0) {
        if ((errno == EINTR) || (errno == EAGAIN))
            goto ACCEPT_WFE;
        perror("accept");
        cleanup();
    }

    nread = read(s1, &flag, sizeof(flag));
    if (nread == -1) {
        perror("Termination socket read failed");
    } else if (nread == 0) {
    } else if (nread != sizeof(flag)) {
        printf("Invalid termination socket on read\n");
        cleanup();
    } else {
	nread = read(s1, &local_id, sizeof(local_id));
	if (nread == -1) {
        	perror("Termination socket read failed");
    	} else if (nread == 0) {
    	} else if (nread != sizeof(local_id)) {
        	printf("Invalid termination socket on read\n");
        	cleanup();
    	} else if (flag > -1) {
			remote_id=flag;
        		printf("mpirun_rsh: Abort signaled from [%d : %s] remote host is [%d : %s ]\n",local_id,plist[local_id].hostname,remote_id, plist[remote_id].hostname); 
        		close(s);
        		close(s1);
        		cleanup();
	}
	else
	{
		printf("mpirun_rsh: Abort signaled from [%d]\n",local_id);
                        close(s);
                        close(s1);
                        cleanup();

        }
    }
}

void process_termination()
{
    int i;
    int rval;
    int send_val = 1000;
    int remote_id;

#ifdef MCST_SUPPORT
    int my_rank;
    int mcs_read, mcs_written;
    char grp_info[GRP_INFO_LEN];
    int s;
#endif

    /* we want to wait until all processes have reached a point
     * in their termination where they have cancelled the VIA async
     * error handlers.
     * We implement a simple barrier by waiting in a read on each
     * socket until the process has cancelled its error handler.
     * NOTE: If EOF is returned, this implies the remote process has
     * failed and the connection is broken.
     * Once all processes respond, we inform them to continue by
     * writing a value back down the socket.
     */

    for (i = 0; i < nprocs; i++) {
        int s = plist[i].control_socket;
        int nread;
        remote_id = -1;
        nread = read(s, &remote_id, sizeof(remote_id));
        if (nread == -1) {
            perror("termination socket read failed");
            plist[i].state = P_DISCONNECTED;
        } else if (nread == 0) {
            plist[i].state = P_DISCONNECTED;
        } else if (nread != sizeof(remote_id)) {
            printf("Invalid termination socket read on [%d] "
                   "returned [%d] got [%d]\n", i, nread, remote_id);
            cleanup();
        } else {
            plist[i].state = P_FINISHED;
        }
    }

    /* now, everyone who is still alive has responded */
    for (i = 0; i < nprocs; i++) {
        int s = plist[i].control_socket;
        if (plist[i].state == P_FINISHED) {
            int nwritten = write(s, &send_val, sizeof(send_val));
            if (nwritten != sizeof(send_val)) {
                perror("socket write");
                cleanup();
            }
        }
    }
#ifdef MCST_SUPPORT

    /* First, receive root's group information. Root=0 */

    s = plist[0].control_socket;
    mcs_read = read(s, grp_info, GRP_INFO_LEN);
    if (mcs_read != GRP_INFO_LEN) {
        fprintf(stderr, "mpirun_rsh: read grp info failed "
                "(len %d %s)\n", mcs_read, grp_info);
        cleanup();
    }

    /* Second, send grp info to all others */
    for (i = 1; i < nprocs; i++) {
        s = plist[i].control_socket;
        mcs_written = write(s, grp_info, GRP_INFO_LEN);
        if (mcs_written != GRP_INFO_LEN) {
            fprintf(stderr, "mpirun_rsh: write grp info failed %d\n",
                    mcs_written);
            cleanup();
        }
    }

    /* Third, receive ack from all non-root nodes */
    for (i = 1; i < nprocs; i++) {
        s = plist[i].control_socket;
        mcs_read = read(s, &my_rank, sizeof(my_rank));
        if (mcs_read != sizeof(my_rank)) {
            fprintf(stderr, "mpirun_rsh: write grp info failed %d\n",
                    mcs_written);
            perror("mpirun_rsh: read  grp ack");
            cleanup();
        }
    }

    /* Fourth, write ack to the root */
    my_rank = 0;
    s = plist[0].control_socket;
    mcs_written = write(s, &my_rank, sizeof(my_rank));
    if (mcs_written != sizeof(my_rank)) {
        perror("pmgr_exchange_mcs_group_sync: mpirun_rsh "
               "write ack to root");
        cleanup();
    }
#endif


}

void usage(void)
{
    fprintf(stderr, "usage: mpirun_rsh [-v] [-rsh|-ssh] "
            "[-paramfile=pfile] "
  	    "[-debug] -[tv] [-xterm] [-show] -np N "
            "(-hostfile hfile | h1 h2 ... hN) a.out args\n");
    fprintf(stderr, "Where:\n");
    fprintf(stderr, "\tv          => Show version and exit\n");
    fprintf(stderr, "\trsh        => " "to use rsh for connecting\n");
    fprintf(stderr, "\tssh        => " "to use ssh for connecting\n");
    fprintf(stderr, "\tparamfile  => "
            "file containing run-time MVICH parameters\n");
    fprintf(stderr, "\tdebug      => "
            "run each process under the control of gdb\n");
    fprintf(stderr,"\ttv         => "
	    "run each process under the control of totalview\n");
    fprintf(stderr, "\txterm      => "
            "run remote processes under xterm\n");
    fprintf(stderr, "\tshow       => "
            "show command for remote execution but dont run it\n");
    fprintf(stderr, "\tnp         => "
            "specify the number of processes\n");
    fprintf(stderr, "\th1 h2...   => "
            "names of hosts where processes should run\n");
    fprintf(stderr, "or\thostfile   => "
            "name of file contining hosts, one per line\n");
    fprintf(stderr, "\ta.out      => " "name of MPI binary\n");
    fprintf(stderr, "\targs       => " "arguments for MPI binary\n");
    fprintf(stderr, "\n");
}

/* finds first non-whitespace char in input string */
char *skip_white(char *s)
{
    int len;
    /* return pointer to first non-whitespace char in string */
    /* Assumes string is null terminated */
    /* Clean from start */
    while ((*s == ' ') || (*s == '\t'))
        s++;
    /* Clean from end */
    len = strlen(s) - 1; 

    while (((s[len] == ' ') 
                || (s[len] == '\t')) && (len >=0)){
        s[len]='\0';
        len--;
    }
    return s;
}

/* Read hostfile */
static int read_hostfile(char *hostfile_name)
{
    size_t i, j, hostname_len = 0;
    FILE *hf = fopen(hostfile_name, "r");
    if (hf == NULL) {
        fprintf(stderr, "Can't open hostfile %s\n", hostfile_name);
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    for (i = 0; i < nprocs; i++) {
        char line[100];
        char *trimmed_line;
        int separator_count = 0,j = 0,prev_j = 0;

        if (fgets(line, 100, hf) != NULL) {
            int len = strlen(line);

            if (line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
           
            /* Remove comments and empty lines*/
            if (strchr(line, '#') != NULL) {
                line[strlen(line) - strlen(strchr(line, '#'))] = '\0';  
            }
            
            trimmed_line = skip_white(line);
           
            if (strlen(trimmed_line) == 0) {
                /* The line is empty, drop it */
                i--;
                continue;
            }

            /*Update len and continue patch ?! move it to func ?*/
            len = strlen(trimmed_line);
            
            /* Parsing format:
             * hostname SEPARATOR hca_name SEPARATOR port
             */
           
            for (j =0; j < len; j++){
                if ( trimmed_line[j] == SEPARATOR && separator_count == 0){
                    plist[i].hostname = (char *)strndup(trimmed_line, j + 1);
                    plist[i].hostname[j] = '\0';
                    prev_j = j;
                    separator_count++;
                    hostname_len = hostname_len > len ? hostname_len : len;
                    continue;
                }
                if ( trimmed_line[j] == SEPARATOR && separator_count == 1){
                    plist[i].device = (char *)strndup(&trimmed_line[prev_j + 1],
                            j - prev_j);
                    plist[i].device[j-prev_j-1] = '\0';
                    separator_count++;
                    continue;
                }
                if ( separator_count == 2){
                    plist[i].port = atoi(&trimmed_line[j]);
                    break;
                }
            }
            if (0 == separator_count) {
                plist[i].hostname = safe_strdup(trimmed_line);
                hostname_len = hostname_len > len ? hostname_len : len;
            }
            if (1 == separator_count) {
                plist[i].device = (char*)safe_strdup(&trimmed_line[prev_j+1]);
            }
        } else {
            fprintf(stderr, "End of file reached on "
                    "hostfile at %d of %d hostnames\n", i, nprocs);
            exit(EXIT_FAILURE);
        }
    }
    fclose(hf);
    return hostname_len;
}

/*
 * reads the param file and constructs the environment strings
 * for each of the environment variables.
 * The caller is responsible for de-allocating the returned string.
 *
 * NOTE: we cant just append these to our current environment because
 * RSH and SSH do not export our environment vars to the remote host.
 * Rather, the RSH command that starts the remote process looks
 * something like:
 *    rsh remote_host "cd workdir; env ENVNAME=value ... command"
 */
#define ENV_LEN 1024
#define LINE_LEN 256
char *read_param_file(char *paramfile,char *env)
{
    FILE *pf;
    char errstr[256];
    char name[128], value[128];
    char buf[384];
    char line[LINE_LEN];
    char *p;
    int num, e_len;
    int env_left = 0;

    if ((pf = fopen(paramfile, "r")) == NULL) {
        sprintf(errstr, "Cant open paramfile = %s", paramfile);
        perror(errstr);
        exit(EXIT_FAILURE);
    }

    if ( strlen(env) == 0 ){
	    /* Allocating space for env first time */
	    env = safe_malloc(ENV_LEN);
	    env_left = ENV_LEN - 1;
    }else{
	    /* already allocated */
	    env_left = ENV_LEN - (strlen(env) + 1) - 1;
    }

    while (fgets(line, LINE_LEN, pf) != NULL) {
        p = skip_white(line);
        if (*p == '#' || *p == '\n') {
            /* a comment or a blank line, ignore it */
            continue;
        }
        /* look for NAME = VALUE, where NAME == MVICH_... */
        name[0] = value[0] = '\0';
        if (param_debug) {
            printf("Scanning: %s\n", p);
        }
        if ((num = sscanf(p, "%64[A-Z_] = %192s", name, value)) != 2) {
            /* debug */
            if (param_debug) {
                printf("FAILED: matched = %d, name = %s, "
                       "value = %s in \n\t%s\n", num, name, value, p);
            }
            continue;
        }

        /* construct the environment string */
        buf[0] = '\0';
        sprintf(buf, "%s=%s ", name, value);
        /* concat to actual environment string */
        e_len = strlen(buf);
        if (e_len > env_left) {
            /* oops, need to grow env string */
            int newlen =
                (ENV_LEN > e_len + 1 ? ENV_LEN : e_len + 1) + strlen(env);
            env = safe_realloc(env, newlen);
            if (param_debug) {
                printf("realloc to %d\n", newlen);
            }
            env_left = ENV_LEN - 1;
        }
        strcat(env, buf);
        env_left -= e_len;
        if (param_debug) {
            printf("Added: [%s]\n", buf);
            printf("env len = %d, env left = %d\n", strlen(env), env_left);
        }
    }
    fclose(pf);

    return env;
}

void cleanup_handler(int sig)
{
    static int already_called = 0;
    printf("Signal %d received.\n", sig);
    if (already_called == 0) {
        already_called = 1;
    }
    cleanup();

    exit(EXIT_FAILURE);
}

void pglist_print(void) {
    if(pglist) {
	int i, j;
	size_t npids = 0, npids_allocated = 0;

	fprintf(stderr, "\n--pglist--\ndata:\n");
	for(i = 0; i < pglist->npgs; i++) {
	    fprintf(stderr, "%p - %s:", &pglist->data[i],
		    pglist->data[i].hostname);

	    for(j = 0; j < pglist->data[i].npids; j++) {
		fprintf(stderr, " %d", pglist->data[i].pids[j]);
	    }

	    fprintf(stderr, "\n");
	    npids	    += pglist->data[i].npids;
	    npids_allocated += pglist->data[i].npids_allocated;
	}

	fprintf(stderr, "\nindex:");
	for(i = 0; i < pglist->npgs; i++) {
	    fprintf(stderr, " %p", pglist->index[i]);
	}

	fprintf(stderr, "\nnpgs/allocated: %d/%d (%d%%)\n", pglist->npgs,
		pglist->npgs_allocated, (int)(pglist->npgs_allocated ? 100. *
		    pglist->npgs / pglist->npgs_allocated : 100.));
	fprintf(stderr, "npids/allocated: %d/%d (%d%%)\n", npids,
		npids_allocated, (int)(npids_allocated ? 100. * npids /
		    npids_allocated : 100.));
	fprintf(stderr, "--pglist--\n\n");
    }
}

void pglist_insert(const char * const hostname, const pid_t const pid) {
    const size_t increment = nprocs > 4 ? nprocs / 4 : 1;
    size_t index = 0, bottom = 0, top;
    static size_t alloc_error = 0;
    int i, strcmp_result;
    process_group * pg;
    void * backup_ptr;

    if(alloc_error) return;
    if(pglist == NULL) goto init_pglist;

    top = pglist->npgs - 1;
    index = (top + bottom) / 2;

    while(strcmp_result = strcmp(hostname, pglist->index[index]->hostname)) {
	if(bottom >= top) break;

	if(strcmp_result > 0) {
	    bottom = index + 1;
	}

	else {
	    top = index - 1;
	}

	index = (top + bottom) / 2;
    }

    if(!strcmp_result) goto insert_pid;
    if(strcmp_result > 0) index++;

    goto add_process_group;

init_pglist:
    pglist = malloc(sizeof(process_groups));

    if(pglist) {
	pglist->data		= NULL;
	pglist->index		= NULL;
	pglist->npgs		= 0;
	pglist->npgs_allocated	= 0;
    }

    else {
	goto register_alloc_error;
    }

add_process_group:
    if(pglist->npgs == pglist->npgs_allocated) {
	process_group * pglist_data_backup	= pglist->data;
	process_group ** pglist_index_backup	= pglist->index;
	ptrdiff_t offset;

	pglist->npgs_allocated += increment;

	backup_ptr = pglist->data;
	pglist->data = realloc(pglist->data, sizeof(process_group) *
		pglist->npgs_allocated);

	if(pglist->data == NULL) {
	    pglist->data = backup_ptr;
	    goto register_alloc_error;
	}

	backup_ptr = pglist->index;
	pglist->index = realloc(pglist->index, sizeof(process_group *) *
		pglist->npgs_allocated);

	if(pglist->index == NULL) {
	    pglist->index = backup_ptr;
	    goto register_alloc_error;
	}

	if(offset = (size_t)pglist->data - (size_t)pglist_data_backup) { 
	    for(i = 0; i < pglist->npgs; i++) {
		pglist->index[i] = (process_group *)((size_t)pglist->index[i] +
			offset);
	    }
	}
    }

    for(i = pglist->npgs; i > index; i--) {
	pglist->index[i] = pglist->index[i-1];
    }

    pglist->data[pglist->npgs].hostname		= hostname;
    pglist->data[pglist->npgs].pids		= NULL;
    pglist->data[pglist->npgs].npids		= 0;
    pglist->data[pglist->npgs].npids_allocated	= 0;

    pglist->index[index] = &pglist->data[pglist->npgs++];

insert_pid:
    pg = pglist->index[index];

    if(pg->npids == pg->npids_allocated) {
	if(pg->npids_allocated) {
	    pg->npids_allocated <<= 1;

	    if(pg->npids_allocated < pg->npids) pg->npids_allocated = SIZE_MAX;
	    if(pg->npids_allocated > nprocs) pg->npids_allocated = nprocs;
	}

	else {
	    pg->npids_allocated = 1;
	}

	backup_ptr = pg->pids;
	pg->pids = realloc(pg->pids, pg->npids_allocated * sizeof(pid_t));

	if(pg->pids == NULL) {
	    pg->pids = backup_ptr;
	    goto register_alloc_error;
	}
    }

    pg->pids[pg->npids++] = pid;

    return;

register_alloc_error:
    if(pglist) {
	if(pglist->data) {
	    process_group * pg = pglist->data;

	    while(pglist->npgs--) {
		if(pg->pids) free((pg++)->pids);
	    }

	    free(pglist->data);
	}

	if(pglist->index) free(pglist->index);

	free(pglist);
    }

    alloc_error = 1;
}

void free_memory(void) {
    if(pglist) {
	if(pglist->data) {
	    process_group * pg = pglist->data;

	    while(pglist->npgs--) {
		if(pg->pids) free((pg++)->pids);
	    }

	    free(pglist->data);
	}

	if(pglist->index) free(pglist->index);

	free(pglist);
    }

    if(plist) {
	while(nprocs--) {
	    if(plist[nprocs].device) free(plist[nprocs].device);
	    if(plist[nprocs].hostname) free(plist[nprocs].hostname);
	}

	free(plist);
    }
}

void cleanup(void)
{
    int i;

    if (use_totalview) {
	fprintf(stderr, "Cleaning up all processes ...");
    }
#ifdef MAC_OSX
    for (i = 0; i < NSIG; i++) {
#else
    for (i = 0; i < _NSIG; i++) {
#endif
        signal(i, SIG_DFL);
    }

    for (i = 0; i < nprocs; i++) {
	if (RUNNING(i)) {
	    /* send terminal interrupt, which will hopefully 
	       propagate to the other side. (not sure what xterm will
	       do here.
	     */
	    kill(plist[i].pid, SIGINT);
	}
    }

    sleep(1);

    for (i = 0; i < nprocs; i++) {
	if (plist[i].state != P_NOTSTARTED) {
	    /* send regular interrupt to rsh */
	    kill(plist[i].pid, SIGTERM);
	}
    }

    sleep(1);

    for (i = 0; i < nprocs; i++) {
	if (plist[i].state != P_NOTSTARTED) {
	    /* Kill the processes */
	    kill(plist[i].pid, SIGKILL);
	}
    }

    if(pglist) {
	rkill_fast();
    }

    else {
	rkill_linear();
    }

    exit(EXIT_FAILURE);
}

void rkill_fast(void) {
    int i, j, tryagain, spawned_pid[pglist->npgs];

    fprintf(stderr, "Killing remote processes...");

    for(i = 0; i < pglist->npgs; i++) {
	if(0 == (spawned_pid[i] = fork())) {
	    if(pglist->index[i]->npids) {
		const size_t bufsize = 40 + 10 * pglist->index[i]->npids;
		const process_group * pg = pglist->index[i];
		char kill_cmd[bufsize], tmp[10];

		kill_cmd[0] = '\0';
		strcat(kill_cmd, "kill -s 9");

		for(j = 0; j < pg->npids; j++) {
		    snprintf(tmp, 10, " %d", pg->pids[j]);
		    strcat(kill_cmd, tmp);
		}

		strcat(kill_cmd, " >&/dev/null");

		if(use_rsh) {
		    execl(RSH_CMD, RSH_CMD, pg->hostname, kill_cmd, NULL);
		}

		else {
		    execl(SSH_CMD, SSH_CMD, SSH_ARG, "-x", pg->hostname,
			    kill_cmd, NULL);
		}

		perror(NULL);
		exit(EXIT_FAILURE);
	    }

	    else {
		exit(EXIT_SUCCESS);
	    }
	}
    }

    while(1) {
	static int iteration = 0;
	tryagain = 0;

	sleep(1 << iteration);

	for (i = 0; i < pglist->npgs; i++) {
	    if(spawned_pid[i]) {
		if(!(spawned_pid[i] = waitpid(spawned_pid[i], NULL, WNOHANG))) {
		    tryagain = 1;
		}
	    }
	}

	if(++iteration == 5 || !tryagain) {
	    fprintf(stderr, "DONE\n");
	    break;
	}
    }

    if(tryagain) {
	fprintf(stderr, "The following processes may have not been killed:\n");
	for (i = 0; i < pglist->npgs; i++) {
	    if(spawned_pid[i]) {
		const process_group * pg = pglist->index[i];

		fprintf(stderr, "%s:", pg->hostname);

		for (j = 0; j < pg->npids; j++) {
		    fprintf(stderr, " %d", pg->pids[j]);
		}

		fprintf(stderr, "\n");
	    }
	}
    }
}

void rkill_linear(void) {
    int i, j, tryagain, spawned_pid[nprocs];

    fprintf(stderr, "Killing remote processes...");

    for (i = 0; i < nprocs; i++) {
	if(0 == (spawned_pid[i] = fork())) {
	    char kill_cmd[80];

	    if(!plist[i].remote_pid) exit(EXIT_SUCCESS);

	    snprintf(kill_cmd, 80, "kill -s 9 %d >&/dev/null",
		plist[i].remote_pid);

	    if(use_rsh) {
		execl(RSH_CMD, RSH_CMD, plist[i].hostname, kill_cmd, NULL);
	    }

	    else {
		execl(SSH_CMD, SSH_CMD, SSH_ARG, "-x",
			plist[i].hostname, kill_cmd, NULL);
	    }

	    perror(NULL);
	    exit(EXIT_FAILURE);
	}
    }

    while(1) {
	static int iteration = 0;
	tryagain = 0;

	sleep(1 << iteration);

	for (i = 0; i < nprocs; i++) {
	    if(spawned_pid[i]) {
		if(!(spawned_pid[i] = waitpid(spawned_pid[i], NULL, WNOHANG))) {
		    tryagain = 1;
		}
	    }
	}

	if(++iteration == 5 || !tryagain) {
	    fprintf(stderr, "DONE\n");
	    break;
	}
    }

    if(tryagain) {
	fprintf(stderr, "The following processes may have not been killed:\n");
	for (i = 0; i < nprocs; i++) {
	    if(spawned_pid[i]) {
		fprintf(stderr, "%s [%d]\n", plist[i].hostname,
			plist[i].remote_pid);
	    }
	}
    }
}


void nostop_handler(int signal)
{
    printf("Stopping from the terminal not allowed\n");
}

void alarm_handler(int signal)
{
    extern const char * alarm_msg;

    if (use_totalview) {
	fprintf(stderr, "Timeout alarm signaled\n");
    }

    if(alarm_msg) fprintf(stderr, alarm_msg);
    cleanup();
}


void child_handler(int signal)
{
    int status, i, child, pid;
    int exitstatus = EXIT_SUCCESS;

    if (use_totalview) {
	fprintf(stderr, "mpirun: child died. Waiting for others.\n"); 
    }
    alarm(10);
    alarm_msg = "Child died. Timeout while waiting for others.\n";

    for (i = 0; i < nprocs; i++) {
        pid = wait(&status);
        if (pid == -1) {
            perror("wait");
            exitstatus = EXIT_FAILURE;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            exitstatus = EXIT_FAILURE;
        }
        for (child = 0; child < nprocs; child++) {
            if (plist[child].pid == pid) {
                plist[child].state = P_EXITED;
                break;
            }
        }
        if (child == nprocs) {
            fprintf(stderr, "Unable to find child %d!\n", pid);
            exitstatus = EXIT_FAILURE;
        }
    }
    alarm(0);
    exit(exitstatus);
}

/* vi:set sw=4 sts=4 tw=80: */
