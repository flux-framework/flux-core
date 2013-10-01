/* mp_cobo_fen.c - exercise public interfaces */

#include "mp_util.h"
#include "cobo_fen.h"


static int 
open_bind_listen (int *sfd, struct sockaddr_in *sin)
{
    int rc;
    struct sockaddr_in servaddr;
    struct hostent *hp;
    struct in_addr **addr;
    char hn[COBOFEN_MAX_STR_LEN];
    socklen_t len;

    if ( (rc = gethostname(hn, COBOFEN_MAX_STR_LEN)) < 0 ) {
	mp_cobo_say_msg ( COBOOUT_PREFIX, 1,
	    "gethostname call failed");
	goto ret_loc;
    } 

    if ( (hp = gethostbyname(hn)) == NULL ) {
	mp_cobo_say_msg ( COBOOUT_PREFIX, 1,
	    "gethostbyname call failed");
	goto ret_loc;
    }

    addr = (struct in_addr **) hp->h_addr_list;
    if ( ((*sfd) = socket ( AF_INET, SOCK_STREAM, 0 )) < 0 ) {
	mp_cobo_say_msg ( COBOOUT_PREFIX, 1,
	    "socket call failed");
	goto ret_loc;
    } 

    bzero (&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    memcpy (&servaddr.sin_addr, *addr, sizeof(struct in_addr));
    servaddr.sin_port = 0; 

    if ( (rc = bind( (*sfd),
		     (struct sockaddr*) &servaddr,
		     sizeof(servaddr)) < 0)) {
	mp_cobo_say_msg ( COBOOUT_PREFIX, 1,
	    "bind call failed");
	goto ret_loc;
    } 

    if ( ( rc = listen( (*sfd), SOMAXCONN) ) < 0 ) {
	mp_cobo_say_msg ( COBOOUT_PREFIX, 1,
	    "listen call failed");
	goto ret_loc;
	    
    }

    len = sizeof(*sin);
    if (getsockname((*sfd), (struct sockaddr *) sin, &len) == -1) {
	mp_cobo_say_msg  ( COBOOUT_PREFIX, 1,
	    "getsocknamefailed");
	goto ret_loc;		
    }

    rc = 0;

 ret_loc:
    return rc;
}


static int
handle_cobo (int listeningSocketFd, int rootFd, unsigned int *sessid)
{
    int doneint = -1;
    int rc;

    rc = cobo_server_open (listeningSocketFd, sessid);
    if (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "cobo_server_open returned failure");
        return -1;
    }

    mp_cobo_say_msg (COBOOUT_PREFIX, 0,
        "After cobo_server_open");

    rc = cobo_server_get_root_socket (&rootFd);
    if (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "cobo_server_get_root_socket failed");
        return -1;
    }

    if ( write (rootFd, (const void *) sessid, 
                sizeof(*sessid)) != sizeof(*sessid)) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "write failed.");
        return -1;
    }

    if ( read (rootFd, &doneint, 
               sizeof(doneint)) != sizeof(doneint)) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "read failed.");
	return -1;
    }

   if ( doneint != (int) *sessid ) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "Info equality does not hold: failure!");
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "Proceed, nevertheless.");
   }

    rc = cobo_server_close();
    if (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "cobo_server_close failed.");
	return -1;
    }


    return rc;
}


int 
main (int argc, char *argv[])
{
    unsigned int sessid;
    int rc;
    int listenSocketFd = -1;
    int rootFd = -1;
    int pCount = 0;
    struct sockaddr_in listenSocketInfo;
    char ip[INET_ADDRSTRLEN], ipPortPair[COBOFEN_MAX_STR_LEN]; 
    const char *rip;
    int port;

    if ( argc != 2 ) {
        mp_cobo_say_msg (COBOOUT_PREFIX, 1, 
           "Usage: prog process_count");
        exit(1);
    }

    pCount = atoi (argv[1]);

    rc = open_bind_listen (&listenSocketFd, &listenSocketInfo);
    if ( rc == -1) {	
    	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "open_bind_listen failed.");
	exit(1);
    }

    port = ntohs (listenSocketInfo.sin_port);
    rip = inet_ntop (AF_INET, 
		     (const void *) &listenSocketInfo.sin_addr,
		     ip, 
		     INET_ADDRSTRLEN);
    if (rip == NULL) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
	    "inet_ntop failed.");
	exit(1);
    }


    /*****************************************************************
     *  ip contains the IP address of the node this fen is running
     *  port contains the opened port
     *
     *****************************************************************/

    mp_cobo_say_msg (COBOOUT_PREFIX, 0,
       "IP: %s", ip);
    mp_cobo_say_msg (COBOOUT_PREFIX, 0,
       "port: %d", port);

    snprintf (ipPortPair,COBOFEN_MAX_STR_LEN,
        "%s:%d", ip, port);


    /*****************************************************************
     * CHILD STARTS: Fork/Exec back-ends with ipPortPair
     *
     *****************************************************************/
     if ( fork () == 0 ) {
         /* CHILD */
         sleep (5); 
         char **newargv = NULL;
         char argbuf[128];
         if (getenv("COBO_DEBUG") != NULL) {
             newargv = (char **) malloc (8*sizeof(char *)); 
             newargv[0] = strdup("/usr/local/bin/totalview");
             newargv[1] = strdup ("/usr/bin/srun");
             newargv[2] = strdup ("-a");
             newargv[3] = strdup ("--overcommit");
             snprintf (argbuf, 128, "-n%d", pCount);
             newargv[4] = strdup (argbuf);
             newargv[5] = strdup ("./mp_cobo_be");
             newargv[6] = strdup (ipPortPair);
             newargv[7] = NULL;
             execv ((const char *) newargv[0], (char * const *) newargv);
         }
         else {
             newargv = (char **) malloc (6*sizeof(char *)); 
             newargv[0] = strdup ("/usr/bin/srun");
             newargv[1] = strdup ("--overcommit");
             snprintf (argbuf, 128, "-n%d", pCount);
             newargv[2] = strdup (argbuf);
             newargv[3] = strdup ("./mp_cobo_be");
             newargv[4] = strdup (ipPortPair);
             newargv[5] = NULL;
             execv ((const char *) newargv[0], (char * const *) newargv);
         }
         /* SINK */
     }
    /*****************************************************************
     * CHILD ENDS: Fork/Exec back-ends with ipPortPair
     *
     *****************************************************************/


    rc = handle_cobo (listenSocketFd, rootFd, &sessid);
    if (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
	    "handle_cobo failed.");
	exit(1);
    }

    mp_cobo_say_msg (COBOOUT_PREFIX, 0, "TEST SUCCESS");

    return EXIT_SUCCESS;
}

