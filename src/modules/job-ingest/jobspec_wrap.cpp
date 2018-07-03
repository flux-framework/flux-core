/* C wrappers for jobspec class */

#include "jobspec.h"

#include <string>
#include "src/common/libjobspec/jobspec.hpp"

using namespace Flux::Jobspec;

extern "C"
int jobspec_validate (const char *buf, int len,
                      char *errbuf, int errbufsz)
{
    std::string str (buf, len);
    Jobspec js;

    try {
        js = Jobspec (str);
    } catch (parse_error& e) {
        if (errbuf && errbufsz > 0) {
            if (e.position != -1 || e.line != -1 || e.column != -1)
                snprintf (errbuf, errbufsz,
                          "jobspec (pos %d line %d col %d): %s",
                          e.position, e.line, e.column, e.what());
            else
                snprintf (errbuf, errbufsz, "jobspec: %s", e.what());
        }
        return -1;
    }

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
