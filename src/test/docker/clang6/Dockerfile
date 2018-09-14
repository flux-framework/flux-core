FROM fluxrm/testenv:bionic-base

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

RUN sudo apt-get -qq update \
 && sudo apt-get -qq install -y --no-install-recommends \
            clang-6.0 \
 && sudo rm -rf /var/lib/apt/lists/*

ENV clangver=6.0
RUN sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${clangver} 100 \
 && sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${clangver} 100
