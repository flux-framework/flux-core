# Import core symbols directly, allows flux.FLUX_MSGTYPE_ANY for example
from flux.constants import *
from flux.core import Flux
import flux.core as core

__all__ = ['core',
           'kvs',
           'mrpc',
           'barrier',
           'constants',
           'Flux'
           ]
