/*
 * Copyright (C) 2016 Semihalf.
 *
 * vr_dpdk_netlink_ring.h -- header for netlink over shared memory transport
 *
 */

#include "vr_netlink_ring.h"

#define VR_NL_RING_MAX_FDS  5
#define VR_NL_RING_DIR_MODE (S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)

#define VR_NL_RING_NAME_LEN 64
#define VR_NL_RING_FILE     VR_NL_RING_DIR"/nl_ring_"

#define VR_NL_POLL_FREQ_MS  -1

int vr_dpdk_netlink_sock_init(void);
int vr_netlink_ring_loop(int nl_sock_fd);
