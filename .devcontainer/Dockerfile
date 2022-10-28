FROM fluxrm/testenv:focal

LABEL maintainer="Vanessasaurus <@vsoch>"

# Captain proto (this can eventually go in testenv container)
RUN apt-get update && apt-get install -y capnproto
COPY scripts/requirements-dev.txt /requirements.txt

# For easier Python development. 
RUN python3 -m pip install IPython && \
    python3 -m pip install -r /requirements.txt

# Assuming installing to /usr/local
ENV LD_LIBRARY_PATH=/usr/local/lib
