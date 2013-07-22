/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-411040.
 * All rights reserved.
 * This file is part of the PMGR_COLLECTIVE library.
 * For details, see https://sourceforge.net/projects/pmgrcollective.
 * Please also read this file: LICENSE.TXT.
*/

#ifndef _PMGR_COLLECTIVE_RANGES_H
#define _PMGR_COLLECTIVE_RANGES_H

/* TODO: add functions to parse and free range resources */

int pmgr_range_numbers_size(const char* range, int* size);

int pmgr_range_numbers_nth(const char* range, int n, char* target, size_t target_len);

int pmgr_range_nodelist_size(const char* nodelist, int* size);

int pmgr_range_nodelist_nth(const char* nodelist, int n, char* target, size_t target_len);

#endif /* _PMGR_COLLECTIVE_RANGES_H */
