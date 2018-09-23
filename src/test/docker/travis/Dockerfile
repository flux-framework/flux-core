ARG IMAGESRC

FROM $IMAGESRC

# Allow flux-security version, username, UID, and GID to be overidden on
#  docker build command line:
#
ARG USER=flux
ARG UID=1000
ARG GID=1000
ARG FLUX_SECURITY_VERSION
ARG OS

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
RUN case $OS in \
     ubuntu*) adduser $USER sudo && adduser flux sudo ;; \
     centos*) usermod -G wheel $USER && usermod -G wheel flux ;; \
    esac

# Make sure user in appropriate group for sudo on different platorms
RUN case $OS in \
     ubuntu*) apt-get -qq install -y --no-install-recommends python-six python3-six ;; \
     centos*) yum -y install python-six python34-devel python34-cffi python34-six ;; \
    esac

USER $USER
WORKDIR /home/$USER
