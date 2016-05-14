/*
 * Copyright (C) 2016 Semihalf.
 *
 * vr_netlink_ring.h -- ring buffer for netlink messages over shared memory
 *
 */

#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>

/**
 * Netlink ring buffer structure in shared memory:
 * VRouter point of view:
 * 				RX RING 		               TX Ring
 * +-------------------------------+-------------------------------+
 * |vr_nl_ring_buf  |  msg buffer  |vr_nl_ring_buf  |  msg buffer  |
 * +-------------------------------+-------------------------------+
 * Agent/utility point of view:
 * 				TX RING                        RX Ring
 *
 * message buffer:
 * +------------------------------------+
 * | vr_nl_ring_msg  |  netlink message |
 * +------------------------------------+
 *
 * ring pointers:
 * TAIL                 HEAD
 *   |                   |
 * +------------------------------------+
 * | |DATA DATA DATA DATA|              |
 * +------------------------------------+
 *
 * After data is read:
 *                   TAIL==HEAD
 *                       |
 * +------------------------------------+
 * | // empty ring //                   |
 * +------------------------------------+
 *
 * During next wirte it will be treated as:
 *
 * TAIL==HEAD
 * |
 * +------------------------------------+
 * | // empty ring //                   |
 * +------------------------------------+
 */

/**
 * header placed before every piece of data enqueued to the ring
 */
struct vr_nl_ring_msg {
    uint32_t len;
};


/**
 * ring structure.
 */
#define CACHE_LINE 64
struct vr_nl_ring_buf {
    unsigned head __attribute__((aligned(CACHE_LINE))); /* bytes written */
    unsigned tail __attribute__((aligned(CACHE_LINE))); /* bytes read */
    char start[0]; /* pointer to the data buffer. start[tail] points to the first new message */
} __attribute__((aligned(CACHE_LINE)));

/* size in bytes of one netlink ring */
#define VR_NL_RING_SZ       102400
/* size of shared memory for mmap: TX ring + RX ring + ring headers */
#define VR_NL_SHM_SZ        2 * (VR_NL_RING_SZ + sizeof(struct vr_nl_ring_buf))

/* next ring in the same shared memory chunk */
#define VR_NL_RING_NEXT(r)  (struct vr_nl_ring_buf *)((char *)r + VR_NL_SHM_SZ/2)

#define VR_NL_RING_DIR      "/var/run/vrouter"
#define VR_NL_SOCKET_FILE   VR_NL_RING_DIR"/dpdk_netlink"

/* memcpy */
int vr_nl_ring_enq(struct vr_nl_ring_buf *ring, void *src, uint32_t len);
int vr_nl_ring_deq(struct vr_nl_ring_buf *ring, void **dst);

/* memcpy iovector */
int vr_nl_ring_msg_enq(struct vr_nl_ring_buf *ring, struct msghdr *msg);
int vr_nl_ring_msg_deq(struct vr_nl_ring_buf *ring, struct msghdr *msg);

/* return pointer to message ready to read */
int vr_nl_ring_deq_ptr(struct vr_nl_ring_buf *ring, void **dst);
void vr_nl_ring_deq_finish(struct vr_nl_ring_buf *ring);

/* return pointer to buffer to write a message */
int vr_nl_ring_enq_ptr(struct vr_nl_ring_buf *ring, void **dst, int len);
void vr_nl_ring_enq_finish(struct vr_nl_ring_buf *ring, int len);


