import cffi

"""Global constants for the flux interface"""

cast_only_ffi = cffi.FFI()
#constants from #define values
FLUX_NODEID_ANY =  int(cast_only_ffi.cast('uint32_t', ~0))
FLUX_NODEID_UPSTREAM = int(cast_only_ffi.cast('uint32_t', ~1))

FLUX_MSGTYPE_REQUEST    = 0x01
FLUX_MSGTYPE_RESPONSE   = 0x02
FLUX_MSGTYPE_EVENT      = 0x04
FLUX_MSGTYPE_KEEPALIVE  = 0x08
FLUX_MSGTYPE_ANY        = 0x0f
FLUX_MSGTYPE_MASK       = 0x0f

FLUX_MSGFLAG_TOPIC      = 0x01
FLUX_MSGFLAG_PAYLOAD    = 0x02
FLUX_MSGFLAG_JSON       = 0x04
FLUX_MSGFLAG_ROUTE      = 0x08
FLUX_MSGFLAG_UPSTREAM   = 0x10

FLUX_O_TRACE = 1
FLUX_O_COPROC = 2
FLUX_O_NONBLOCK = 4

FLUX_SEC_TYPE_PLAIN = 1
FLUX_SEC_TYPE_CURVE = 2
FLUX_SEC_TYPE_MUNGE = 4
FLUX_SEC_TYPE_ALL = 7

FLUX_MATCHTAG_NONE = 0

