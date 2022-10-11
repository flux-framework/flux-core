FROM fluxrm/testenv:focal

LABEL maintainer="Vanessasaurus <@vsoch>"

# For easier Pyton development. 
# TODO add pre-commit installs here when added!
RUN python3 -m pip install IPython && \
    python3 -m pip install 'black==20.08.b1' --force-reinstall && \
    python3 -m pip install 'pylint==2.4.4' --force-reinstall && \
    python3 -m pip install 'mypy==0.770' --force-reinstall

# Assuming installing to /usr/local
ENV LD_LIBRARY_PATH=/usr/local/lib