/*
 * This file is part of The Scalable Checkpoint / Restart (SCR) library.
 * It is made available under the terms of the BSD-3 license which
 * accompanies this distribution in LICENSE.TXT.
*/

#ifndef SCR_LUSTRE_LOAD_H
#define SCR_LUSTRE_LOAD_H

/*
=========================================
Load of the Lustre filesystem holding the SCR prefix directory.

Lustre servers that support it return their current and peak
read/write bandwidth and RPC rate in statfs.  These functions turn
that into the total current and peak load of the targets that SCR
writes to, so checkpoints and flushes can be deferred while the
filesystem is heavily loaded.  They are called only on rank 0.
=========================================
*/

/* kinds of load that are tracked */
enum {
  SCR_LUSTRE_BW = 0,  /* OST read+write KiB/s */
  SCR_LUSTRE_OPS,     /* OST read+write RPCs/s */
  SCR_LUSTRE_MD_OPS,  /* MDT read+write RPCs/s */
  SCR_LUSTRE_NR
};

/* current load of the filesystem */
typedef struct {
  double cur[SCR_LUSTRE_NR];   /* sum of the current rates of the targets */
  double peak[SCR_LUSTRE_NR];  /* sum of the recent peak rates */
  int targets;                 /* number of targets that returned their load */
} scr_lustre_load_info;

/* find the targets used by files in prefix, returns SCR_FAILURE if prefix
 * is not on Lustre or the servers do not return their load */
int scr_lustre_load_init(const char* prefix);

/* release resources from scr_lustre_load_init() */
int scr_lustre_load_finalize(void);

/* query the current load of the targets found by scr_lustre_load_init() */
int scr_lustre_load_get(scr_lustre_load_info* load);

#endif
