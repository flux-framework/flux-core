FROM fluxrm/testenv:focal

LABEL maintainer="Vanessasaurus <@vsoch>"

# Pip not provided in this version
RUN apt-get update
COPY scripts/requirements-dev.txt /requirements.txt

# For easier Python development.
RUN python3 -m pip install IPython && \
    python3 -m pip install -r /requirements.txt

# Assuming installing to /usr/local
ENV LD_LIBRARY_PATH=/usr/local/lib

# Assuming installing to /usr/local
ENV LD_LIBRARY_PATH=/usr/local/lib

# extra interactive utilities and compilation_database generation
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
    bear \
    fd-find \
    gdb \
    less \
    ripgrep