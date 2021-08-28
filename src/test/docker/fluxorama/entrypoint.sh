#!/bin/sh
if systemctl is-enabled ttyd; then
  #  Set default password for fluxuser
  if test -z "$FLUXUSER_PASSWORD"; then
     printf >&2 "ERROR: No password set for fluxuser\n"
     printf >&2 "Please set via the FLUXUSER_PASSWORD environment variable\n"
     printf >&2 \
         "e.g. FLUXUSER_PASSWORD=xxzzyy docker run -e FLUXUSER_PASSWORD ..\n"
   exit 1
  fi
  printf >&2 "Setting requested password for fluxuser..\n"
  echo fluxuser:${FLUXUSER_PASSWORD} | chpasswd
fi
exec "$@"
