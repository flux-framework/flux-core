ARG IMAGESRC

FROM $IMAGESRC

# Allow flux-security version, username, UID, and GID to be overidden on
#  docker build command line:
#
ARG USER=flux
ARG UID=1000
ARG GID=1000
ARG FLUX_SECURITY_VERSION
ARG BASE_IMAGE

# Install flux-security by hand for now:
#
RUN CCACHE_DISABLE=1 \
 && V=$FLUX_SECURITY_VERSION \
 && PKG=flux-security-$V \
 && URL=https://github.com/flux-framework/flux-security/releases/download \
 && wget ${URL}/v${V}/${PKG}.tar.gz \
 && tar xvfz ${PKG}.tar.gz \
 && cd ${PKG} \
 && ./configure --prefix=/usr || cat config.log \
 && make -j 4 \
 && make install \
 && cd .. \
 && rm -rf flux-security-*


# Add configured user to image with sudo access:
#
RUN set -x && groupadd -g $UID $USER \
 && useradd -g $USER -u $UID -d /home/$USER -m $USER \
 && printf "$USER ALL= NOPASSWD: ALL\\n" >> /etc/sudoers

# Also add "flux" user to image with sudo access:
#
RUN set -x && groupadd flux \
 && useradd -g flux -d /home/flux -m flux \
 && printf "flux ALL= NOPASSWD: ALL\\n" >> /etc/sudoers

# Make sure user in appropriate group for sudo on different platorms
RUN case $BASE_IMAGE in \
     bionic*) adduser $USER sudo && adduser flux sudo ;; \
     centos*) usermod -G wheel $USER && usermod -G wheel flux ;; \
     *) (>&2 echo "Unknown BASE_IMAGE") ;; \
    esac

# Add some packages for testing
RUN case $BASE_IMAGE in \
     bionic*) apt update && apt-get -qq install -y --no-install-recommends python-yaml python3-yaml python-jsonschema python3-jsonschema ;; \
     centos*) yum -y update && yum -y install python-yaml python34-yaml python-jsonschema python34-jsonschema;; \
     *) (>&2 echo "Unknown BASE_IMAGE") ;; \
    esac

# Setup MUNGE directories & key
RUN mkdir -p /var/run/munge \
 && dd if=/dev/urandom bs=1 count=1024 > /etc/munge/munge.key \
 && chown -R munge /etc/munge/munge.key /var/run/munge \
 && chmod 600 /etc/munge/munge.key


ENV BASE_IMAGE=$BASE_IMAGE
USER $USER
WORKDIR /home/$USER
