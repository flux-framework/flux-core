"""
python bindings to flux-core, the main core of the flux resource manager
"""
# Manually lazy
# pylint: disable=invalid-name
def Flux(*args, **kwargs):
    import flux.core
    return flux.core.Flux(*args, **kwargs)

__all__ = ['core',
           'kvs',
           'jsc',
           'rpc',
           'sec',
           'constants',
           'Flux', ]
