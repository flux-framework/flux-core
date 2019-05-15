/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libtap/tap.h"
#include "src/common/libutil/kary.h"

int main (int argc, char** argv)
{
    plan (NO_PLAN);

    /* Completely test a size=6 (rank 0-5) k=2 tree */

    ok (kary_parentof (2, 0) == KARY_NONE, "k=2: rank 0 has no parent");
    ok (kary_parentof (2, 1) == 0, "k=2: rank 1 parent is 0");
    ok (kary_parentof (2, 2) == 0, "k=2: rank 2 parent is 0");
    ok (kary_parentof (2, 3) == 1, "k=2: rank 3 parent is 1");
    ok (kary_parentof (2, 4) == 1, "k=2: rank 4 parent is 1");
    ok (kary_parentof (2, 5) == 2, "k=2: rank 5 parent is 2");

    ok (kary_childof (2, 6, 0, -1) == KARY_NONE, "k=2,size=6: rank 0 has no child -1");
    ok (kary_childof (2, 6, 0, 0) == 1, "k=2,size=6: rank 0 child 0 is 1");
    ok (kary_childof (2, 6, 0, 1) == 2, "k=2,size=6: rank 0 child 1 is 2");
    ok (kary_childof (2, 6, 0, 2) == KARY_NONE, "k=2,size=6: rank 0 has no child 2");
    ok (kary_childof (2, 6, 1, 0) == 3, "k=2,size=6: rank 1 child 0 is 3");
    ok (kary_childof (2, 6, 1, 1) == 4, "k=2,size=6: rank 1 child 1 is 4");
    ok (kary_childof (2, 6, 2, 0) == 5, "k=2,size=6: rank 2 child 0 is 5");
    ok (kary_childof (2, 6, 2, 1) == KARY_NONE, "k=2,size=6: rank 2 has no child 1");
    ok (kary_childof (2, 6, 3, 0) == KARY_NONE, "k=2,size=6: rank 3 has no child 0");
    ok (kary_childof (2, 6, 3, 1) == KARY_NONE, "k=2,size=6: rank 3 has no child 1");
    ok (kary_childof (2, 6, 4, 0) == KARY_NONE, "k=2,size=6: rank 4 has no child 0");
    ok (kary_childof (2, 6, 4, 1) == KARY_NONE, "k=2,size=6: rank 4 has no child 1");
    ok (kary_childof (2, 6, 5, 0) == KARY_NONE, "k=2,size=6: rank 5 has no child 0");
    ok (kary_childof (2, 6, 5, 1) == KARY_NONE, "k=2,size=6: rank 5 has no child 1");
    ok (kary_childof (2, 6, 6, 0) == KARY_NONE, "k=2,size=6: rank 6 has no child 0");

    ok (kary_sum_descendants (2, 6, 0) == 5, "k=2,size=6: rank 0 has 5 descendants");
    ok (kary_sum_descendants (2, 6, 1) == 2, "k=2,size=6: rank 1 has 2 descendants");
    ok (kary_sum_descendants (2, 6, 2) == 1, "k=2,size=6: rank 2 has 1 descendant");
    ok (kary_sum_descendants (2, 6, 3) == 0, "k=2,size=6: rank 2 has 0 descendant");
    ok (kary_sum_descendants (2, 6, 4) == 0, "k=2,size=6: rank 4 has 0 descendant");
    ok (kary_sum_descendants (2, 6, 5) == 0, "k=2,size=6: rank 5 has 0 descendant");
    ok (kary_sum_descendants (2, 6, 6) == 0, "k=2,size=6: rank 6 has 0 descendant");

    ok (kary_parent_route (2, 6, 0, 0) == KARY_NONE, "k=2,size=6: route up 0>0: none");
    ok (kary_parent_route (2, 6, 0, 1) == KARY_NONE, "k=2,size=6: route up 0>1: none");
    ok (kary_parent_route (2, 6, 1, 0) == 0, "k=2,size=6: route up 1>0: via 0");
    ok (kary_parent_route (2, 6, 2, 0) == 0, "k=2,size=6: route up 2>0: via 0");
    ok (kary_parent_route (2, 6, 3, 0) == 1, "k=2,size=6: route up 3>0: via 1");
    ok (kary_parent_route (2, 6, 4, 0) == 1, "k=2,size=6: route up 4>0: via 1");
    ok (kary_parent_route (2, 6, 5, 0) == 2, "k=2,size=6: route up 5>0: via 2");
    ok (kary_parent_route (2, 6, 6, 0) == KARY_NONE, "k=2,size=6: route up 6>0: none");
    ok (kary_parent_route (2, 6, 1, 1) == KARY_NONE, "k=2,size=6: route up 1>1: none");
    ok (kary_parent_route (2, 6, 2, 1) == KARY_NONE, "k=2,size=6: route up 2>1: none");
    ok (kary_parent_route (2, 6, 3, 1) == 1, "k=2,size=6: route up 3>1: via 1");
    ok (kary_parent_route (2, 6, 4, 1) == 1, "k=2,size=6: route up 4>1: via 1");
    ok (kary_parent_route (2, 6, 5, 1) == KARY_NONE, "k=2,size=6: route up 5>1: none");
    ok (kary_parent_route (2, 6, 5, 2) == 2, "k=2,size=6: route up 5>2: via 2");

    ok (kary_child_route (2, 6, 0, 0) == KARY_NONE, "k=2,size=6: route down 0>0: none");
    ok (kary_child_route (2, 6, 1, 0) == KARY_NONE, "k=2,size=6: route down 1>0: none");
    ok (kary_child_route (2, 6, 0, 1) == 1, "k=2,size=6: route down 0>1: via 1");
    ok (kary_child_route (2, 6, 0, 2) == 2, "k=2,size=6: route down 0>2: via 2");
    ok (kary_child_route (2, 6, 0, 3) == 1, "k=2,size=6: route down 0>3: via 1");
    ok (kary_child_route (2, 6, 0, 4) == 1, "k=2,size=6: route down 0>4: via 1");
    ok (kary_child_route (2, 6, 0, 5) == 2, "k=2,size=6: route down 0>5: via 2");
    ok (kary_child_route (2, 6, 0, 6) == KARY_NONE, "k=2,size=6: route down 0>6: none");
    ok (kary_child_route (2, 6, 1, 3) == 3, "k=2,size=6: route down 1>3: via 3");
    ok (kary_child_route (2, 6, 1, 4) == 4, "k=2,size=6: route down 1>4: via 4");
    ok (kary_child_route (2, 6, 2, 3) == KARY_NONE, "k=2,size=6: route down 2>3: none");
    ok (kary_child_route (2, 6, 2, 4) == KARY_NONE, "k=2,size=6: route down 2>4: none");
    ok (kary_child_route (2, 6, 2, 5) == 5, "k=2,size=6: route down 2>5: via 5");
    ok (kary_child_route (2, 6, 2, 6) == KARY_NONE, "k=2,size=6: route down 2>6: none");
    ok (kary_child_route (2, 6, 3, 4) == KARY_NONE, "k=2,size=6: route down 3>4: none");

    ok (kary_levelof (2, 0) == 0, "k=2: rank 0 is level 0");
    ok (kary_levelof (2, 1) == 1, "k=2: rank 1 is level 1");
    ok (kary_levelof (2, 2) == 1, "k=2: rank 2 is level 1");
    ok (kary_levelof (2, 3) == 2, "k=2: rank 3 is level 2");
    ok (kary_levelof (2, 4) == 2, "k=2: rank 4 is level 2");
    ok (kary_levelof (2, 5) == 2, "k=2: rank 5 is level 2");
    ok (kary_levelof (2, 6) == 2, "k=2: rank 6 is level 2");
    ok (kary_levelof (2, 7) == 3, "k=2: rank 7 is level 3");

    /* Check k=1 as a boundary case */

    ok (kary_parentof (1, 0) == KARY_NONE, "k=1: rank 0 has no parent");
    ok (kary_parentof (1, 1) == 0, "k=1: rank 1 parent is 0");
    ok (kary_parentof (1, 2) == 1, "k=1: rank 2 parent is 1");
    ok (kary_parentof (1, 3) == 2, "k=1: rank 3 parent is 2");

    ok (kary_childof (1, 6, 0, 0) == 1, "k=1,size=6: rank 0 child 0 is 1");
    ok (kary_childof (1, 6, 0, 1) == KARY_NONE, "k=1,size=6: rank 0 has no child 1");
    ok (kary_childof (1, 6, 1, 0) == 2, "k=1,size=6: rank 1 child 0 is 2");
    ok (kary_childof (1, 6, 2, 0) == 3, "k=1,size=6: rank 2 child 0 is 3");
    ok (kary_childof (1, 6, 5, 0) == KARY_NONE, "k=1,size=6: rank 2 has no child 0");

    done_testing ();
}
