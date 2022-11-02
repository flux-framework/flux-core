FROM fluxrm/testenv:focal

# docker build -f .devcontainers/Dockerfile -t flux-core-env .

LABEL maintainer="Vanessasaurus <@vsoch>"

RUN apt-get update && apt-get install -y cmake
COPY scripts/requirements-dev.txt /requirements.txt

# For easier Python development. 
RUN python3 -m pip install IPython && \
    python3 -m pip install -r /requirements.txt

# Install flatbuffer
RUN git clone https://github.com/google/flatbuffers.git && \
    cd flatbuffers && \
    cmake -G "Unix Makefiles" && \
    make && make install

# Assuming installing to /usr/local
ENV LD_LIBRARY_PATH=/usr/local/lib
