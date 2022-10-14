FROM fluxrm/testenv:focal

LABEL maintainer="Vanessasaurus <@vsoch>"

# For easier Python development. 
RUN python3 -m pip install IPython && \
    python3 -m pip install --force-reinstall \
        black==20.08.b1 \
        flake8>=5.0.4 \
        isort>=5.9.3 \
        mypy==0.971 \
        pre-commit>=2.9.2 \
        types-PyYAML

# Assuming installing to /usr/local
ENV LD_LIBRARY_PATH=/usr/local/lib
