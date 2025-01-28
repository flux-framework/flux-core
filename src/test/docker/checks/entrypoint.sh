#!/bin/sh
sudo -u munge /usr/sbin/munged
exec "$@"
