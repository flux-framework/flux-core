#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <pmgr_collective_client.h>

#include "list.h"
#include "log_msg.h"
#include "pepe-lua.h"

/****************************************************************************
 *  Options
 ****************************************************************************/
#include "optparse.h"
struct optparse_option opt_table [] = {
    { "verbose",    'v', 0, 0, NULL,   "Increase verbosity.", NULL, NULL },
    { "config",     'c', 1, 0, "FILE", "Set config to FILE.", NULL, NULL },
    OPTPARSE_TABLE_END,
};

/****************************************************************************
 *  Data Types
 ****************************************************************************/

struct prog_options {
    int                     argc;
    char **                 argv;
    char *                  config_file;
    int                     verbose;

};

struct prog_ctx {
    char *                  prog;
    struct prog_options     opts;
    pepe_lua_t              lua;
    int                     nprocs;     /* Total number of processes in job */
    int                     rank;       /* This process' rank               */
};

/****************************************************************************
 *  Prototypes
 ****************************************************************************/

static int  prog_ctx_init (struct prog_ctx *ctx, int ac, char *av []);
static void prog_ctx_fini (struct prog_ctx *ctx);
static int  prog_ctx_pmgr_init (struct prog_ctx *ctx);
static int  parse_cmdline (struct prog_ctx *ctx, int ac, char *av []);
static void exec_user_args (struct prog_ctx *ctx);
static int  setup_shell_environment (struct prog_ctx *ctx);

/****************************************************************************
 *  Functions
 ****************************************************************************/

int main (int ac, char **av)
{
    struct prog_ctx ctx;

    if (prog_ctx_init (&ctx, ac, av) < 0) {
        fprintf (stderr, "%s: completely failed to initialize\n", av[0]);
        exit (1);
    }

    if (parse_cmdline (&ctx, ac, av) < 0)
        log_fatal (1, "Error parsing command line\n");

    if (prog_ctx_pmgr_init (&ctx) < 0)
        log_fatal (1, "Failed to initialize PMGR_COLLECTIVE\n");

    if (!(ctx.lua = pepe_lua_state_create (ctx.nprocs, ctx.rank)))
        log_fatal (1, "Failed to initialize lua state\n");

    if (pepe_lua_script_execute (ctx.lua, ctx.opts.config_file) < 0)
        log_fatal (1, "%s: Failed to read config file\n", ctx.opts.config_file);

    if ((pmgr_barrier ()) != PMGR_SUCCESS)
        log_fatal (1, "pmgr_barrier: Failed\n");

    /*
     *  Rank 0 executes user program:
     */
    if (ctx.rank == 0) {
        setup_shell_environment (&ctx);
        exec_user_args (&ctx);
    }

    /*
     *  Everyone waits at a final "we're exiting" barrier
     */
    log_debug ("rank%d: barrier\n", ctx.rank);
    if ((pmgr_barrier ()) != PMGR_SUCCESS)
        log_fatal (1, "pmgr_barrier(final): Failed\n");

    prog_ctx_fini (&ctx);
    exit (0);
}

static void prog_options_init (struct prog_options *opts)
{
    memset (opts, 0, sizeof (*opts));
    opts->config_file = strdup ("default");
}

static int prog_ctx_init (struct prog_ctx *ctx, int ac, char *av[])
{
    char *prog = basename (av [0]);

    memset (ctx, 0, sizeof (*ctx));
    ctx->rank = -1;
    ctx->nprocs = -1;

    ctx->prog = strdup (prog);
    if (log_msg_init (prog) < 0)
        return (-1);

    prog_options_init (&ctx->opts);

    return (0);
}

static void prog_options_fini (struct prog_options *opts)
{
    if (opts->config_file)
        free (opts->config_file);
}

void prog_ctx_fini (struct prog_ctx *ctx)
{
    if (ctx->prog)
        free (ctx->prog);
    ctx->rank = -1;
    ctx->nprocs = -1;
    prog_options_fini (&ctx->opts);
    log_msg_fini ();
}

static int parse_cmdline (struct prog_ctx *ctx, int ac, char *av[])
{
    const char *optarg;
    int n, optind;

    optparse_t p = optparse_create (ctx->prog);
    if (p == NULL)
        log_fatal (1, "Failed to create opt parser!\n");

    optparse_set (p, OPTPARSE_USAGE, "[OPTIONS]... COMMAND...");
    optparse_add_doc (p,
        "Load and run scripts from config and launch COMMAND", 0);

    if (optparse_add_option_table (p, opt_table) != OPTPARSE_SUCCESS)
        log_fatal (1, "Failed to add option table!\n");

    if ((optind = optparse_parse_args (p, ac, av)) < 0)
        log_fatal (1, "Option parsing failed!\n");

    if ((n = optparse_getopt (p, "verbose", NULL)) > 0)
        log_msg_set_verbose (n);

    if (optparse_getopt (p, "config", &optarg) > 0)
        ctx->opts.config_file = strdup (optarg);

    /*
     *  Get remaining args -- the program to run
     */
    if ((ctx->opts.argc = ac - optind) == 0)
        log_fatal (1, "Must supply executable to run.\n");
    ctx->opts.argv = &av [optind];

    return (0);
}

static void add_rank_to_log_prefix (struct prog_ctx *ctx)
{
    char buf[16];
    sprintf (buf, "%d", ctx->rank);
    log_msg_set_secondary_prefix (buf);
}

static int prog_ctx_pmgr_init (struct prog_ctx *ctx)
{
    int np, rank, id;

    if ((pmgr_init (NULL, NULL, &np, &rank, &id)) != PMGR_SUCCESS)
        return log_err ("pmgr_init failure");

    if ((pmgr_open ()) != PMGR_SUCCESS)
        return log_err ("pmgr_open failure");

    ctx->rank = rank;
    ctx->nprocs = np;

    add_rank_to_log_prefix (ctx);

    return (0);
}

static void exec_user_args (struct prog_ctx *ctx)
{
    pid_t pid;
    extern char **environ;

    if ((pid = fork ()) < 0)
        log_fatal (1, "fork: %s\n", strerror (errno));
    else if (pid == 0) {
        log_debug ("executing process `%s'\n", ctx->opts.argv [0]);
        if (execve (ctx->opts.argv[0], ctx->opts.argv, environ) < 0) {
            log_err ("exec: %s: %s\n", ctx->opts.argv [0], strerror (errno));
            exit (127);
        }
        /* NORETURN */
    }

    /* Wait for child process to exit */
    waitpid (pid, NULL, 0);

    return;
}

/*
 *  Allocate copy of env var entry in [entry] into buffer [bufp],
 *   NUL terminating at '='. Furthermore, if [valp] is non-NULL,
 *   set [valp] to point to first character after nullified '='.
 *
 */
static int get_env_var (const char *entry, char **bufp, char **valp)
{
    char *var;
    const char *p = entry;
    int len = strlen (entry) + 1;

    *bufp = malloc (len * sizeof (char));
    if (*bufp == NULL)
        return (-1);

    memset (*bufp, 0, len);

    var = *bufp;

    while (*p != '\0') {
        *var = *p;

        if (*var == '=') {
            *var = '\0';
            if (valp)
                *valp = var + 1;
        }
        p++;
        var++;
    }

    return 0;
}


static int setup_shell_environment (struct prog_ctx *ctx)
{
    extern char **environ;
    char **env;
    char *var;
    char *val;
    char *entry;
    List l;

    /*  First save some important SLURM env vars:
     */
    char *jobid =    getenv ("SLURM_JOB_ID");
    char *nodelist = getenv ("SLURM_JOB_NODELIST");
    char *nnodes =   getenv ("SLURM_JOB_NUM_NODES");
    char *cpus =     getenv ("SLURM_JOB_CPUS_PER_NODE");
    char *conf =     getenv ("SLURM_CONF");


    l = list_create (NULL);

    /*  Collect all SLURM_* and MPIRUN_* env vars to unset:
     */
    env = environ;
    while (*env != NULL) {
        if ((strncmp (*env, "SLURM", 5) == 0) ||
            (strncmp (*env, "MPIRUN", 4) == 0)) {
            get_env_var (*env, &var, &val);
            list_push (l, var);
        }
        ++env;
    }

    while ((var = list_pop (l))) {
        log_debug ("unsetenv (%s)\n", var);
        unsetenv (var);
        free (var);
    }

    /*  Reset important vars from above:
     */
    setenv ("SLURM_JOB_ID", jobid, 1);
    setenv ("SLURM_JOB_NODELIST", nodelist, 1);
    setenv ("SLURM_JOB_NUM_NODES", nnodes, 1);
    setenv ("SLURM_JOB_CPUS_PER_NODE", cpus, 1);
    if (conf)
        setenv ("SLURM_CONF", conf, 1);

    /*  Restore any env vars with save_pepe_ prefix:
     */
    env = environ;
    while (*env != NULL) {
        if (strncmp (*env, "save_pepe_", 10) == 0)
            list_push (l, strdup (*env));
        ++env;
    }

    while ((entry = list_pop (l))) {
            get_env_var (entry, &var, &val);

            log_debug ("setenv (%s)\n", entry+10);
            if (setenv (var+10, val, 1) < 0)
                log_err ("setenv (%s): %s\n", entry+10, strerror (errno));

            log_debug ("unsetenv (%s)\n", var);
            unsetenv (var);
            free (var);
            free (entry);
    }
    list_destroy (l);
    return (0);
}


#if 0
static void sigset_init (sigset_t *setp)
{
    sigemptyset (setp);
    sigaddset (setp, SIGTERM);
    sigaddset (setp, SIGINT);
    sigaddset (setp, SIGQUIT);
}

static void sig_block (void)
{
    sigset_t set;
    sigset_init (&set);
    if (sigprocmask (SIG_BLOCK, &set, NULL) < 0)
        log_err ("sigprocmask: %s\n", strerror (errno));
}

static void wait_for_signal_and_exit (void)
{
    sigset_t set;
    int sig;

    sigset_init (&set);
    if (sigwait (&set, &sig) < 0)
        log_fatal (1, "sigwait: %s\n", strerror (errno));

    switch (sig) {
        case SIGINT:
        case SIGTERM:
        case SIGQUIT:
            log_msg ("Got signal %d. Exiting...\n", sig);
            exit (0);
        default:
            log_err ("Got unexpected signal %d\n", sig); 
            break;
    }

    /* NOTREACHED */
    exit (1);
}
#endif


/*
 * vi: ts=4 sw=4 expandtab
 */
