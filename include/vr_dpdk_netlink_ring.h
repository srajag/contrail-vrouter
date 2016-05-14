/*
 * Copyright (C) 2016 Semihalf.
 *
 * vr_dpdk_netlink_ring.h -- header for vRouter/DPDK module to use Netlink
 *                           over shared memory ring buffer.
 *
 */
#include "vr_message.h"
#include "vr_netlink_ring.h"

/* Max simultanously connected clients + 1 for vRouter listening socket */
#define VR_NL_RING_MAX_FDS  5

/* File backing shared memory properties */
#define VR_NL_RING_NAME_LEN 64
#define VR_NL_RING_DIR_MODE (S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#define VR_NL_RING_FILE     "/vrouter_nl_ring_"

/* Socket events pollig frequency. -1 == infinite timeout */
#define VR_NL_POLL_FREQ_MS  -1

int vr_dpdk_netlink_sock_init(void);
void vr_netlink_ring_loop(int nl_sock_fd);
int vr_nl_ring_message_write(struct vr_nl_ring_buf *tx_ring,
    struct vr_message *message);
