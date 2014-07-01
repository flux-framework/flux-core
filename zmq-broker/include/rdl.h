#ifndef FLUX_RDL_H
#define FLUX_RDL_H

/*
 *  C API interface to Flux Resource Description Language.
 */

#include <stdlib.h>
#include <json/json.h>

/*
 *  Forward declarations:
 */

/*
 *  struct rdl represents a handle to an in-memory copy of an RDL database:
 */
struct rdl;

/*
 *  struct resource represents a handle to a hierarchical resource
 *   within a given rdl db.
 */
struct resource;

/*
 *  struct rdl_accumulator is a container used to accumulate resources
 *   and generate a new RDL representation.
 */
struct rdl_accumulator;


/*
 *  Prototype for error processing function
 */
typedef void (*rdl_err_f) (void *ctx, const char *fmt, ...);


/*
 *  RDL Database functions:
 */

/*
 *  Set default rdllib error handling function to [fn] and context [ctx].
 */
void rdllib_set_default_errf (void *ctx, rdl_err_f fn);

/*
 *  Create a new rdl library handle
 */
struct rdllib * rdllib_open (void);

/*
 *  Close rdllib [l] and free all associated rdl handles
 */
void rdllib_close (struct rdllib *l);

/*
 *  Set rdllib error handling function to [fn] with context [ctx]
 *   to be passed to error function at each call.
 */
int rdllib_set_errf (struct rdllib *l, void *ctx, rdl_err_f fn);

/*
 *  Load an RDL db into library [l] from string [s] and return
 *   a new rdl handle.
 */
struct rdl * rdl_load (struct rdllib *l, const char *s);

/*
 *  Load an RDL db into library [l] from file [filename] and return
 *   a new rdl handle.
 */
struct rdl * rdl_loadfile (struct rdllib *l, const char *filename);

/*
 *  Copy an RDL handle
 */
struct rdl * rdl_copy (struct rdl *rdl);

/*
 *  Return a new RDL handle containing all resources that
 *   match expression in json_object [args].
 *
 *  JSON object supports the following keys, each of which are
 *   ANDed together
 *
 *  {
 *    'basename' : STRING,   - base name of object
 *    'name'     : NAMELIST, - match full name in NAMELIST (hostlist format)
 *    'ids'      : IDLIST,   - match resource "id" in idlist
 *    'type'     : STRING,   - match resource type name
 *    'tags'     : [ TAGS ]  - list of tags to match
 *  }
 */
struct rdl * rdl_find (struct rdl *rdl, json_object *args);

/*
 *  Destroy and deallocate an rdl handle
 */
void rdl_destroy (struct rdl *rdl);


/*
 *  Serialize an entire rdl db [rdl] and return a string. Caller
 *   is responsible for freeing memory from the string.
 */
char *rdl_serialize (struct rdl *rdl);


/*
 *   RDL Resource methods:
 */

/*
 *  Fetch a resource from the RDL db [rdl] at URI [uri], where
 *   [uri] is of the form "name[:path]", to fetch resource from
 *   optional path element [path] in hierarchy [name].
 *   (e.g. "default" or "default:/clusterA")
 */
struct resource * rdl_resource_get (struct rdl *rdl, const char *uri);

/*
 *  Free memory associated with resource object [r].
 */
void rdl_resource_destroy (struct resource *r);

/*
 *  Return the path to resource [r]
 */
const char *rdl_resource_path (struct resource *r);

/*
 *  Get the string representation of the name for resource [r].
 */
const char *rdl_resource_name (struct resource *r);

/*
 *  Tag a resource with [tag] (tag only)
 */
void rdl_resource_tag (struct resource *r, const char *tag);

/*
 *  Set or get an arbitrary [tag] to an integer value [val]
 */
int rdl_resource_set_int (struct resource *r, const char *tag, int64_t val);
int rdl_resource_get_int (struct resource *r, const char *tag, int64_t *valp);

/*
 *  Remove a tag [tag] from resource object [r]
 */
void rdl_resource_delete_tag (struct resource *r, const char *tag);

/*
 *  Get representation of resource object [r] in json form
 *
 *  Format is a dictionary of name and values something like:
 *    { type: "string",
 *      name: "string",
 *      id:   number,
 *      properties: { list of key/value pairs },
 *      tags: {list of values},
 *    }
 */
json_object *rdl_resource_json (struct resource *r);

/*
 *  Aggregate all properties, tags, values and types from
 *   the resource hierarchy starting at [r]. Returns a json object
 *   reprenting the aggregation.
 */
json_object * rdl_resource_aggregate_json (struct resource *r);

/*
 *  Iterate over child resources in resource [r].
 */
struct resource * rdl_resource_next_child (struct resource *r);

/*
 *  Reset internal child iterator
 */
void rdl_resource_iterator_reset (struct resource *r);

/*
 *  Unlink a child with name [name] from this hierarchy at parent [r].
 */
int rdl_resource_unlink_child (struct resource *r, const char *name);


/*
 *   RDL "Accumulator" methods:
 */

/*
 *  Create a "resource accumulator" object from a source RDL [rdl]
 */
struct rdl_accumulator * rdl_accumulator_create (struct rdl *rdl);

void rdl_accumulator_destroy (struct rdl_accumulator *a);

/*
 *  Add hierarchy rooted at resource [r] to accumulator.
 */
int rdl_accumulator_add (struct rdl_accumulator *a, struct resource *r);

/*
 *  Serialize the RDL represented by accumulator [a]
 */
char * rdl_accumulator_serialize (struct rdl_accumulator *a);

/*
 *  Copy the accumulated resource data in [a] to a new RDL object.
 */
struct rdl * rdl_accumulator_copy (struct rdl_accumulator *a);

#endif /* !FLUX_RDL_H */
