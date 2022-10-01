FROM fluxrm/testenv:bionic

LABEL maintainer="Vanessasaurus <@vsoch>"

# Pip not provided in this version
RUN apt-get update && apt-get install -y python3-pip

# For easier Python development. 
# TODO add pre-commit installs here when added!
RUN python3 -m pip install IPython && \
    python3 -m pip install --force-reinstall black==20.08.b1 && \
    python3 -m pip install --force-reinstall flake8>=4.0.1 && \
    python3 -m pip install --force-reinstall isort>=5.9.3 && \
    python3 -m pip install --force-reinstall mypy==0.770 && \
    python3 -m pip install --force-reinstall pre-commit>=2.9.2 && \
    python3 -m pip install --force-reinstall types-PyYAML

# Assuming installing to /usr/local
ENV LD_LIBRARY_PATH=/usr/local/lib
