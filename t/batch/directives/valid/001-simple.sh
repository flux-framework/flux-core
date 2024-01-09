#!/bin/sh
# simple directives
#FLUX: -N4                  # Request four nodes
#FLUX: --queue=batch        # Submit to the batch queue
#FLUX: --job-name=app001    # Set an explicit job name
flux run -N4 app
