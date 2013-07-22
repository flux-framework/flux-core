/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-411040.
 * All rights reserved.
 * This file is part of the PMGR_COLLECTIVE library.
 * For details, see https://sourceforge.net/projects/pmgrcollective.
 * Please also read this file: LICENSE.TXT.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmgr_collective_client.h"

int ranks, my_rank, my_id;
char** procs;

size_t size = 1024;
size_t buffer_size;
char* sbuffer;
char* rbuffer;

//#define BIGBUF_SIZE (1024*1024*1024)
#define BIGBUF_SIZE (1024*1024*10)
static char bigbuf[BIGBUF_SIZE] = {'a'};

/* fill the buffer with a pattern */
void init_sbuffer(int rank)
{
  size_t i;
  char value;
  for(i = 0; i < buffer_size; i++)
  {
    /*value = ((char) ((i+1)*(rank+1) + i) % 26) + 'a';*/
    value = (char) ((i+1)*(rank+1) + i);
    sbuffer[i] = value;
  }
}

/* blank out the receive buffer */
void init_rbuffer(int rank)
{
  memset(rbuffer, 0, buffer_size);
}

/* check the send buffer for any deviation from expected pattern */
void check_sbuffer(char* op)
{
  size_t i;
  char value;
  for(i = 0; i < buffer_size; i++)
  {
    /*value = ((char) ((i+1)*(my_rank+1) + i) % 26) + 'a';*/
    value = (char) ((i+1)*(my_rank+1) + i);
    if (sbuffer[i] != value)
    {
      printf("%d: %s: Send buffer corruption detected at sbuffer[%d]\n",
             my_rank, op, i
      );
      break;
    }
  }
}

/* check the receive buffer for any deviation from expected pattern */
void check_rbuffer(char* buffer, size_t byte_offset, int rank, size_t src_byte_offset, size_t element_count, char* op)
{
  size_t i, j;
  char value;
  buffer += byte_offset;
  for(i = 0, j = src_byte_offset; i < element_count; i++, j++)
  {
    /*value = ((char) ((j+1)*(rank+1) + j) % 26) + 'a';*/
    value = (char) ((j+1)*(rank+1) + j);
    if (buffer[i] != value)
    {
      printf("%d: %s: Receive buffer corruption detected at rbuffer[%d] from rank %d\n",
             my_rank, op, byte_offset+i, rank
      );
      break;
    }
  }
}

int main(int argc, char* argv[])
{
  int root = 0;
  int i;

  /* initialize the client (read environment variables) */
  if (pmgr_init(&argc, &argv, &ranks, &my_rank, &my_id) != PMGR_SUCCESS) {
    printf("Failed to init\n");
    exit(1);
  }
//  printf("Ranks: %d, Rank: %d, ID: %d\n", ranks, my_rank, my_id);

  for (i = 0; i < BIGBUF_SIZE; i++) {
    bigbuf[i] = (char)(i % 26) + 'a';
  }

  buffer_size = ranks * size;
  sbuffer = malloc(buffer_size);
  rbuffer = malloc(buffer_size);

  /* open connections (connect to launcher and build the TCP tree) */
  if (pmgr_open() != PMGR_SUCCESS) {
    printf("Failed to open\n");
    exit(1);
  }

  /* test pmgr_barrier */
  if (pmgr_barrier() != PMGR_SUCCESS) {
    printf("Barrier failed\n");
    exit(1);
  }

  /* test pmgr_bcast */
  init_sbuffer(my_rank);
  init_rbuffer(my_rank);
  void* buf = (void*) rbuffer;
  if (my_rank == root) { buf = sbuffer; }
  if (pmgr_bcast(buf, (int) size, root) != PMGR_SUCCESS) {
    printf("Bcast failed\n");
    exit(1);
  }
/*  check_sbuffer(); */
  check_rbuffer(buf, 0, root, 0, size, "pmgr_bcast");

  /* test pmgr_scatter */
  init_sbuffer(my_rank);
  init_rbuffer(my_rank);
  if (pmgr_scatter(sbuffer, (int) size, rbuffer, root) != PMGR_SUCCESS) {
    printf("Scatter failed\n");
    exit(1);
  }
  check_sbuffer("pmgr_scatter");
  check_rbuffer(rbuffer, 0, root, my_rank*size, size, "pmgr_scatter");

  /* test pmgr_gather */
  init_sbuffer(my_rank);
  init_rbuffer(my_rank);
  if (pmgr_gather(sbuffer, (int) size, rbuffer, root) != PMGR_SUCCESS) {
    printf("Gather failed\n");
    exit(1);
  }
  check_sbuffer("pmgr_gather");
  if (my_rank == root) {
    for (i = 0; i < ranks; i++) {
      check_rbuffer(rbuffer, i*size, i, 0, size, "pmgr_gather");
    }
  }

  /* test pmgr_allgather */
  init_sbuffer(my_rank);
  init_rbuffer(my_rank);
  if (pmgr_allgather(sbuffer, (int) size, rbuffer) != PMGR_SUCCESS) {
    printf("Allgather failed\n");
    exit(1);
  }
  check_sbuffer("pmgr_allgather");
  for (i = 0; i < ranks; i++) {
    check_rbuffer(rbuffer, i*size, i, 0, size, "pmgr_allgather");
  }

#if 0
  /* test pmgr_alltoall */
  init_sbuffer(my_rank);
  init_rbuffer(my_rank);
  if (pmgr_alltoall(sbuffer, (int) size, rbuffer) != PMGR_SUCCESS) {
    printf("Alltoall failed\n");
    exit(1);
  }
  check_sbuffer("pmgr_alltoall");
  for (i = 0; i < ranks; i++) {
    check_rbuffer(rbuffer, i*size, i, my_rank*size, size, "pmgr_alltoall");
  }
#endif

  /* hacky way to invoke allreduce_int64t for timing */
  int64_t my64  = (int64_t) my_rank;
  int64_t max64;
  if (pmgr_allreduce_int64t(&my64, &max64, PMGR_MAX) != PMGR_SUCCESS) {
    printf("Allreducemaxint failed\n");
    exit(1);
  }
//  printf("%d: Max int %d\n", my_rank, max);

/*
  char** hosts;
  char*  hostsbuf;
  char   host[255];
  gethostname(host, 255);
  if (pmgr_allgatherstr(host, &hosts, &hostsbuf) != PMGR_SUCCESS) {
    printf("Allgatherstr failed\n");
    exit(1);
  }
  int i;
  if (my_rank == 0 || my_rank == ranks-1) {
    for (i=0; i<ranks; i++) {
      printf("%d: hosts[%d] = %s\n", my_rank, i, hosts[i]);
    }
  }
  free(hosts);
  free(hostsbuf);
*/

done:
  /* close connections (close connection with launcher and tear down the TCP tree) */
  if (pmgr_close() != PMGR_SUCCESS) {
    printf("Failed to close\n");
    exit(1);
  }

  /* shutdown */
  if (pmgr_finalize() != PMGR_SUCCESS) {
    printf("Failed to finalize\n");
    exit(1);
  }

  /* needed this sleep so that mpirun prints out all debug info (don't know why yet) */
  sleep(1);

  return 0;
}
