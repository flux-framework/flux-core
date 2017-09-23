/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

extern "C" {
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
}

#include <iostream>
#include <fstream>
#include <string>

using namespace std;

void submit_jobspec (flux_t *broker, string& jobspec_data)
{
    flux_msg_t *msg;
    char *response;
    int response_len;
    int rc;

    msg = flux_request_encode_raw ("job-ingest.submit",
                                   jobspec_data.c_str(), jobspec_data.size()+1);
    flux_msg_set_nodeid (msg, FLUX_NODEID_ANY, 0);
    flux_send (broker, msg, 0);
    flux_msg_destroy (msg);

    msg = flux_recv (broker, FLUX_MATCH_RESPONSE, 0);
    rc = flux_response_decode_raw (msg, NULL, (void **)&response, &response_len);
    flux_msg_destroy (msg);

    if (rc != 0) {
        printf("Your jobspec is REJECTED!\n");
    } else {
        printf("%s", response);
    }
}

/*
 * Read from a 'js_stream' which may contain several YAML documents.
 * Split the documents into individual strings.  Submit each document
 * to the flux broker as soon as the individual document is read
 * completely.
 *
 * This does not validate whether the rest of the text is
 * valid YAML syntax; it only looks for YAML document markers.
 */
void parse_yaml_stream_docs (flux_t *broker, istream& js_stream)
{
    const char *byte_order_mark = "\xef\xbb\xbf";
    enum : int { BODY, DIRECTIVES, UNKNOWN };

    int state = UNKNOWN;
    string doc;
    do {
        string line;
        getline (js_stream, line);

        if (line.compare (0, 3, byte_order_mark) == 0) {
            /* The "byte order mark" always marks the beginning of a document */
            if (doc.size() > 0) {
                submit_jobspec (broker, doc);
                doc.clear();
            }
            state = UNKNOWN;
        } else if (line.compare (0, 3, "---") == 0) {
            if (state == BODY ||
                    (state == UNKNOWN && doc.size() > 0)) {
                /* Start of new message marks end of previous */
                submit_jobspec (broker, doc);
                doc.clear();
            }
            /* The --- means the following message starts in the body. */
            state = BODY;
        }

        doc += line;
        if (!js_stream.eof())
            doc += "\n";

        /* Try to intuit the state */
        if (state == UNKNOWN) {
            if (line[0] == '%') {
                state = DIRECTIVES;
            } else if (line [0] != '#') {
                state = BODY;
            }
        }

        if ((line.compare (0, 3, "...") == 0) || js_stream.eof()) {
            submit_jobspec (broker, doc);
            doc.clear();
            state = UNKNOWN;
        }
    } while (!js_stream.eof());
}

int main (int argc, char *argv[])
{
    flux_t *broker;
    broker = flux_open (NULL, 0);

    if (argc == 1) {
        parse_yaml_stream_docs (broker, cin);
    } else {
        for (int i = 1; i < argc; i++) {
            ifstream js_file (argv[i]);
            if (js_file.fail()) {
                cerr << "Unable to open file \"" << argv[i] << "\"" << endl;
                continue;
            }
            parse_yaml_stream_docs (broker, js_file);
        }
    }
    return 0;
}
