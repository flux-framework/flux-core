FROM fluxrm/testenv:bionic-base

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

RUN sudo apt-get -qq update \
 && sudo apt-get -qq install -y --no-install-recommends \
            clang-6.0 \
 && sudo rm -rf /var/lib/apt/lists/*
