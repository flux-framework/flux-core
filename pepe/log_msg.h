/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235277
 * 
 *  This file is part of io-watchdog.
 * 
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#ifndef _LOG_MSG_H
#define _LOG_MSG_H

typedef int (*out_f) (const char *buf);

int log_msg_init (const char *prefix);
void log_msg_fini ();

int log_msg_verbose ();
int log_msg_set_verbose (int level);
void log_msg_set_secondary_prefix (const char *prefix);
void log_msg_set_output_fn (out_f out);
int log_msg_quiet ();
void log_fatal (int err, const char *format, ...);
int log_err (const char *format, ...);
void log_msg (const char *format, ...);
void log_verbose (const char *format, ...);
void log_debug (const char *format, ...);
void log_debug2 (const char *format, ...);
void log_debug3 (const char *format, ...);

#endif /* !_LOG_MSG_H */
