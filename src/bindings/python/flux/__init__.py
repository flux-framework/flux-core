"""
python bindings to flux-core, the main core of the flux resource manager
"""
# Import core symbols directly, allows flux.FLUX_MSGTYPE_ANY for example
# pylint: disable=wildcard-import
from flux.constants import *
from flux.core import Flux

__all__ = ['core',
           'kvs',
           'jsc',
           'rpc',
           'sec',
           'constants',
           'Flux', ]


