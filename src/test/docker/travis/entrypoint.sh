#!/bin/sh
sudo runuser -u munge /usr/sbin/munged
exec "$@"
