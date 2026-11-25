/*
 * This file is part of The Scalable Checkpoint / Restart (SCR) library.
 * It is made available under the terms of the BSD-3 license which
 * accompanies this distribution in LICENSE.TXT.
*/

#include "scr_globals.h"
#include "scr_lustre_load.h"

#ifdef HAVE_LUSTRE_PERFSTATS

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <lustre/lustreapi.h>

/* sum of the load returned by one type of target */
struct scr_lustre_sum {
  double kbs;      /* read+write KiB/s */
  double kbs_max;  /* peak read+write KiB/s */
  double ops;      /* read+write RPCs/s */
  double ops_max;  /* peak read+write RPCs/s */
  int targets;     /* targets that returned their load */
};

static int scr_lustre_fd = -1;
static char scr_lustre_fsname[LUSTRE_MAXFSNAME + 1];
static char scr_lustre_pool[LOV_MAXPOOLNAME + 1];

/* find the OST pool that new files in prefix are created in, if any */
static void scr_lustre_find_pool(const char* prefix)
{
  struct llapi_layout* layout;

  scr_lustre_pool[0] = '\0';

  layout = llapi_layout_get_by_path(prefix, LLAPI_LAYOUT_GET_EXPECTED);
  if (layout == NULL) {
    return;
  }
  if (llapi_layout_pool_name_get(layout, scr_lustre_pool,
                                 sizeof(scr_lustre_pool)) != 0)
  {
    scr_lustre_pool[0] = '\0';
  }
  llapi_layout_free(layout);
}

/* add the load of all MDTs (type LL_STATFS_LMV) or OSTs (LL_STATFS_LOV) */
static void scr_lustre_sum_targets(__u32 type, struct scr_lustre_sum* sum)
{
  __u32 index;

  memset(sum, 0, sizeof(*sum));

  for (index = 0; ; index++) {
    struct obd_statfs st;
    struct obd_uuid uuid;
    int rc;

    memset(&st, 0, sizeof(st));
    memset(&uuid, 0, sizeof(uuid));
    /* never wait for a target that is down */
    rc = llapi_obd_fstatfs(scr_lustre_fd,
                           type | LL_STATFS_NODELAY | LL_STATFS_PERFSTATS,
                           index, &st, &uuid);
    if (rc == -ENODEV) {
      break;
    }
    if (rc != 0 || !(st.os_state & OS_STATFS_PERFSTATS)) {
      continue;
    }
    /* only count OSTs in the pool that SCR files are written to */
    if (type == LL_STATFS_LOV && scr_lustre_pool[0] != '\0' &&
        llapi_search_ost(scr_lustre_fsname, scr_lustre_pool,
                         uuid.uuid) != 1)
    {
      continue;
    }

    sum->kbs     += (double)st.os_read_kbs + st.os_write_kbs;
    sum->kbs_max += (double)st.os_read_kbs_max + st.os_write_kbs_max;
    sum->ops     += (double)st.os_read_ops + st.os_write_ops;
    sum->ops_max += (double)st.os_read_ops_max + st.os_write_ops_max;
    sum->targets++;
  }
}

int scr_lustre_load_get(scr_lustre_load_info* load)
{
  struct scr_lustre_sum ost;
  struct scr_lustre_sum mdt;

  memset(load, 0, sizeof(*load));
  if (scr_lustre_fd < 0) {
    return SCR_FAILURE;
  }

  scr_lustre_sum_targets(LL_STATFS_LOV, &ost);
  scr_lustre_sum_targets(LL_STATFS_LMV, &mdt);

  load->targets = ost.targets + mdt.targets;
  if (load->targets == 0) {
    return SCR_FAILURE;
  }

  load->cur[SCR_LUSTRE_BW]     = ost.kbs;
  load->cur[SCR_LUSTRE_OPS]    = ost.ops;
  load->cur[SCR_LUSTRE_MD_OPS] = mdt.ops;

  /* each target's peak is the highest load it recently sustained, so the
   * sum of the peaks is what the targets have shown they can do together */
  load->peak[SCR_LUSTRE_BW]     = ost.kbs_max;
  load->peak[SCR_LUSTRE_OPS]    = ost.ops_max;
  load->peak[SCR_LUSTRE_MD_OPS] = mdt.ops_max;

  return SCR_SUCCESS;
}

int scr_lustre_load_init(const char* prefix)
{
  scr_lustre_load_info load;

  if (prefix == NULL) {
    return SCR_FAILURE;
  }

  if (llapi_search_fsname(prefix, scr_lustre_fsname) != 0) {
    scr_dbg(1, "SCR_LUSTRE_LOAD: %s is not on Lustre", prefix);
    return SCR_FAILURE;
  }

  scr_lustre_fd = open(prefix, O_RDONLY | O_DIRECTORY);
  if (scr_lustre_fd < 0) {
    scr_dbg(1, "SCR_LUSTRE_LOAD: cannot open %s: %s",
      prefix, strerror(errno)
    );
    return SCR_FAILURE;
  }

  scr_lustre_find_pool(prefix);

  /* check that at least one server returns its load */
  if (scr_lustre_load_get(&load) != SCR_SUCCESS) {
    scr_dbg(1, "SCR_LUSTRE_LOAD: servers of %s do not return their load",
      scr_lustre_fsname
    );
    scr_lustre_load_finalize();
    return SCR_FAILURE;
  }

  scr_dbg(1, "SCR_LUSTRE_LOAD: filesystem %s pool '%s', %d targets",
    scr_lustre_fsname, scr_lustre_pool, load.targets
  );

  return SCR_SUCCESS;
}

int scr_lustre_load_finalize(void)
{
  if (scr_lustre_fd >= 0) {
    close(scr_lustre_fd);
    scr_lustre_fd = -1;
  }
  return SCR_SUCCESS;
}

#else /* HAVE_LUSTRE_PERFSTATS */

int scr_lustre_load_init(const char* prefix)
{
  scr_dbg(1, "SCR_LUSTRE_LOAD: SCR was built without Lustre load support");
  return SCR_FAILURE;
}

int scr_lustre_load_finalize(void)
{
  return SCR_SUCCESS;
}

int scr_lustre_load_get(scr_lustre_load_info* load)
{
  return SCR_FAILURE;
}

#endif /* HAVE_LUSTRE_PERFSTATS */
