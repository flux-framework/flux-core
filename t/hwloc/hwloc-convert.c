#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <hwloc.h>

int main(int argc, char *argv[])
{
#if HWLOC_API_VERSION < 0x20000
    fprintf(stderr, "hwloc-convert requires hwloc v2.0+\n");
    return 1;
#else
    const char* usage = "USAGE: hwloc-convert input_xml output_xml";
    if (argc != 3) {
        printf("Incorrect arguments supplied.\n");
        printf("%s\n", usage);
    }

    const char *inpath = argv[1];
    int rc = 0, exit_code = 0;

    hwloc_topology_t topology;
    if (hwloc_topology_init (&topology) < 0) {
        fprintf(stderr, "Error initializing hwloc topology\n");
        exit_code = 1;
        goto ret;
    }
    if (hwloc_topology_set_io_types_filter(topology,
                                           HWLOC_TYPE_FILTER_KEEP_IMPORTANT)
        < 0) {
        fprintf(stderr, "hwloc_topology_set_io_types_filter\n");
        exit_code = errno;
        goto ret;
    }
    if (hwloc_topology_set_cache_types_filter(topology,
                                              HWLOC_TYPE_FILTER_KEEP_STRUCTURE)
        < 0) {
        fprintf(stderr, "hwloc_topology_set_cache_types_filter\n");
        exit_code = errno;
        goto ret;
    }
    if (hwloc_topology_set_icache_types_filter(topology,
                                               HWLOC_TYPE_FILTER_KEEP_STRUCTURE)
        < 0) {
        fprintf(stderr, "hwloc_topology_set_icache_types_filter\n");
        exit_code = errno;
        goto ret;
    }
    if ((rc = hwloc_topology_set_xml(topology, inpath)) < 0) {
        fprintf(stderr, "Error reading XML: %s\n", strerror(errno));
        exit_code = errno;
        goto ret;
    }
    if ((rc = hwloc_topology_load(topology)) < 0) {
        fprintf(stderr, "Error loading topology\n");
        exit_code = 1;
        goto ret;
    }

    const char *outpath = argv[2];
    if ((rc = hwloc_topology_export_xml(topology, outpath,
                                        HWLOC_TOPOLOGY_EXPORT_XML_FLAG_V1))
        < 0) {
        fprintf(stderr, "Error exporting XML\n");
        exit_code = 1;
        goto ret;

    }

ret:
    hwloc_topology_destroy (topology);
    return exit_code;
#endif
}
