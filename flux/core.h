/* Allow in-tree programs to #include <flux/core.h> like out-of-tree would.
 */

#ifndef FLUX_CORE_H
#define FLUX_CORE_H

#include "src/common/libflux/flux.h"
#include "src/common/libzio/zio.h"
#include "src/common/libzio/kz.h"
#include "src/common/libzio/forkzio.h"
#include "src/common/libmrpc/mrpc.h"

#include "src/modules/api/api.h"
#include "src/modules/kvs/kvs.h"
#include "src/modules/live/live.h"
#include "src/modules/modctl/modctl.h"

#endif /* FLUX_CORE_H */
