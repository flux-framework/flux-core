/* Implement subset of PMI functionality on top of pmgr_collective calls */

#include "pmi.h"
#include "pmgr_collective_client.h"

#include <map>
#include <string>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

static int initialized = 0;
static int pmgr_ranks;
static int pmgr_rank;
static int pmgr_id;

#define MAX_KVS_LEN (256)
#define MAX_KEY_LEN (256)
#define MAX_VAL_LEN (256)

static char kvs_name[MAX_KVS_LEN];

/*
put:    avl str-->str
commit: avl str-->str
global: avl str-->str
mem:    avl void*-->null
*/

typedef map<string,string> str2str;
static str2str put;
static str2str commit;
static str2str global;

int PMI_Init( int *spawned )
{
  /* check that we got a variable to write our flag value to */
  if (spawned == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  /* we don't support spawned procs */
  *spawned = PMI_FALSE;

  /* initialize the pmgr_collective library */
  if (pmgr_init(NULL, NULL, &pmgr_ranks, &pmgr_rank, &pmgr_id) == PMGR_SUCCESS) {
    if (pmgr_open() == PMGR_SUCCESS) {
      if (snprintf(kvs_name, MAX_KVS_LEN, "%d", pmgr_id) < MAX_KVS_LEN) {
        initialized = 1;
        return PMI_SUCCESS;
      } else {
        return PMI_ERR_NOMEM;
      }
    }
  }
  return PMI_FAIL;
}

int PMI_Initialized( PMI_BOOL *out_initialized )
{
  /* check that we got a variable to write our flag value to */
  if (out_initialized == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  /* set whether we've initialized or not */
  *out_initialized = PMI_FALSE;
  if (initialized) {
    *out_initialized = PMI_TRUE;
  }
  return PMI_SUCCESS;
}

int PMI_Finalize( void )
{
  int rc = PMI_SUCCESS;

  /* close down PMGR_COLLECTIVE */
  if (pmgr_close() != PMGR_SUCCESS) {
    rc = PMI_FAIL;
  }

  if (pmgr_finalize() != PMGR_SUCCESS) {
    rc = PMI_FAIL;
  }

  /* clear put, commit, global, and mem */

  return rc;
}

int PMI_Get_size( int *size )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (size == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  *size = pmgr_ranks;
  return PMI_SUCCESS;
}

int PMI_Get_rank( int *out_rank )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (out_rank == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  *out_rank = pmgr_rank;
  return PMI_SUCCESS;
}

int PMI_Get_universe_size( int *size )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (size == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  *size = pmgr_ranks;
  return PMI_SUCCESS;
}

int PMI_Get_appnum( int *appnum )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (appnum == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  *appnum = pmgr_id;
  return PMI_SUCCESS;
}

int PMI_Abort(int exit_code, const char error_msg[])
{
  /* call pmgr_abort */
  pmgr_abort(exit_code, error_msg);

  /* exit in case above function returns */
  exit(exit_code);

  /* function prototype requires us to return something */
  return PMI_SUCCESS;
}

int PMI_KVS_Get_my_name( char kvsname[], int length )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (kvsname == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  /* check that length is large enough */
  if (length < MAX_KVS_LEN) {
    return PMI_ERR_INVALID_LENGTH;
  }

  /* just use the pmgr_id as the kvs space */
  strcpy(kvsname, kvs_name);
  return PMI_SUCCESS;
}

int PMI_KVS_Get_name_length_max( int *length )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (length == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  *length = MAX_KVS_LEN;
  return PMI_SUCCESS;
}

int PMI_KVS_Get_key_length_max( int *length )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (length == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  *length = MAX_KEY_LEN;
  return PMI_SUCCESS;
}

int PMI_KVS_Get_value_length_max( int *length )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check that we got a variable to write our flag value to */
  if (length == NULL) {
    return PMI_ERR_INVALID_ARG;
  }

  *length = MAX_VAL_LEN;
  return PMI_SUCCESS;
}

int PMI_KVS_Create( char kvsname[], int length )
{
  /* since we don't support spawning, we just have a static key value space */
  int rc = PMI_KVS_Get_my_name(kvsname, length);
  return rc;
}

int PMI_KVS_Put( const char kvsname[], const char key[], const char value[])
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check length of name */
  if (kvsname == NULL || strlen(kvsname) > MAX_KVS_LEN) {
    return PMI_ERR_INVALID_KVS;
  }

  /* check length of key */
  if (key == NULL || strlen(key) > MAX_KEY_LEN) {
    return PMI_ERR_INVALID_KEY;
  }

  /* check length of value */
  if (value == NULL || strlen(value) > MAX_VAL_LEN) {
    return PMI_ERR_INVALID_VAL;
  }

  /* check that kvsname is the correct one */
  if (strcmp(kvsname, kvs_name) != 0) {
    return PMI_ERR_INVALID_KVS;
  }
      
  /* add string to put */
  pair<string,string> kv(key, value);
  put.insert(kv);

  return PMI_SUCCESS;
}

int PMI_KVS_Commit( const char kvsname[] )
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check length of name */
  if (kvsname == NULL || strlen(kvsname) > MAX_KVS_LEN) {
    return PMI_ERR_INVALID_KVS;
  }

  /* check that kvsname is the correct one */
  if (strcmp(kvsname, kvs_name) != 0) {
    return PMI_ERR_INVALID_KVS;
  }
      
  /* copy all entries in put to commit, in order to overwrite existing
   * entires in commit, we may have to erase before the insert */
  str2str::iterator i;
  str2str::iterator target;
  for (i = put.begin(); i != put.end(); i++) {
    /* search for this string in commit */
    string key = i->first;
    target = commit.find(key);

    /* if we found an existing entry in commit, we need to erase it */
    if (target != commit.end()) {
      commit.erase(target);
    }

    /* now insert value from put */
    commit.insert(*i);
  }

  /* clear put */
  put.clear();

  return PMI_SUCCESS;
}

int PMI_Barrier( void )
{
  /* check that we're initialized */
  if (!initialized) {
    /* would like to return PMI_ERR_INIT here, but definition says
     * it must return either SUCCESS or FAIL, and since user knows
     * that PMI_FAIL == -1, he could be testing for this */
    return PMI_FAIL;
  }

  /* count number of bytes to serialize our key/value pairs in commit, if any */
  int64_t size = 0;
  str2str::iterator i;
  for (i = commit.begin(); i != commit.end(); i++) {
    size_t key_len = (i->first).size()  + 1;
    size_t val_len = (i->second).size() + 1;
    size += (int64_t) (key_len + val_len);
  }

  /* determine whether any procs have entires in commit */
  int64_t total_size = 0;
  pmgr_allreduce_int64t(&size, &total_size, PMGR_SUM);

  /* if no one has committed any new values, we're done */
  if (total_size == 0) {
    return PMI_SUCCESS;
  }

  /* otherwise, allocate mem */
  char* data       = new char[(size_t) size];
  char* total_data = new char[(size_t) total_size];

  /* TODO: problem if diff procs specify diff values for same key, since
   * pmgr_aggregate is not guaranteed to return data in same order on same procs */

  /* copy our entires to input buffer */
  char* p = data;
  for (i = commit.begin(); i != commit.end(); i++) {
    /* copy in key string */
    strcpy(p, (i->first).c_str());
    p += (i->first).size() + 1;

    /* copy in value string */
    strcpy(p, (i->second).c_str());
    p += (i->second).size() + 1;
  }

  /* gather all entries */
  int64_t actual_size = 0;
  pmgr_aggregate(data, size, total_data, total_size, &actual_size);

  /* insert entries in global, be careful to overwrite any matching keys */
  char* last = total_data + actual_size;
  p = total_data;
  while (p < last) {
    /* read key from output buffer */
    string key = p;
    p += strlen(p) + 1;

    /* read value from output buffer */
    string value = p;
    p += strlen(p) + 1;

    /* search for this string in global */
    str2str::iterator target = global.find(key);

    /* if we found an existing entry, we need to erase it */
    if (target != global.end()) {
      global.erase(target);
    }

    /* build a kv pair */
    pair<string,string> kv(key, value);

    /* insert the kv pair */
    global.insert(kv);
  }
  
  /* free our temporary memory */
  delete[] data;
  delete[] total_data;

  /* clear commit */
  commit.clear();

  return PMI_SUCCESS; 
}

int PMI_KVS_Get( const char kvsname[], const char key[], char value[], int length)
{
  /* check that we're initialized */
  if (!initialized) {
    return PMI_ERR_INIT;
  }

  /* check length of name */
  if (kvsname == NULL || strlen(kvsname) > MAX_KVS_LEN) {
    return PMI_ERR_INVALID_KVS;
  }

  /* check that kvsname is the correct one */
  if (strcmp(kvsname, kvs_name) != 0) {
    return PMI_ERR_INVALID_KVS;
  }
      
  /* check length of key */
  if (key == NULL || strlen(key) > MAX_KEY_LEN) {
    return PMI_ERR_INVALID_KEY;
  }

  /* check that we have a buffer to write something to */
  if (value == NULL) {
    return PMI_ERR_INVALID_VAL;
  }

  /* lookup entry from global */
  string keystr = key;
  str2str::iterator target = global.find(keystr);
  if (target == global.end()) {
    /* failed to find the key */
    return PMI_FAIL;
  }

  /* check that the user's buffer is large enough */
  int len = (target->second).size() + 1;
  if (length < len) {
    return PMI_ERR_INVALID_LENGTH;
  }

  /* copy the value into user's buffer */
  strcpy(value, (target->second).c_str());

  return PMI_SUCCESS;
}

int PMI_Spawn_multiple(
  int count, const char * cmds[], const char ** argvs[], const int maxprocs[],
  const int info_keyval_sizesp[], const PMI_keyval_t * info_keyval_vectors[],
  int preput_keyval_size, const PMI_keyval_t preput_keyval_vector[], int errors[])
{
  /* we don't implement this yet, but mvapich2 needs a reference */
  return PMI_FAIL;
}
