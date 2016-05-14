/*
 * Copyright (C) 2016 Semihalf.
 *
 * vr_netlink_ring.h -- header for ring buffer for netlink messages over
 *                      shared memory.
 *
 */

#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>

#include <stdbool.h>
#include <vr_queue.h>


/**
 * Ring structure
 */
#define CACHE_LINE 64
struct vr_nl_ring_buf {
    /* position where to write to */
    unsigned head __attribute__((aligned(CACHE_LINE)));
    /* position where to read from */
    unsigned tail __attribute__((aligned(CACHE_LINE)));
    /* netlink responses queue */
    struct vr_qhead nl_responses;
    /* pointer to the data buffer after this header */
    char start[0];
} __attribute__((aligned(CACHE_LINE)));

/**
 * Header placed before every piece of data enqueued to the ring
 */
struct vr_nl_ring_msg {
    /**
     * length of data after this header or (unsigned)-1 if message is placed
     * not right after the header but on the beginning of the ring
     */
    uint32_t len;
};


/**
 * Netlink shared memory ring buffer internals explained.
 *
 * The same shared memory chunk is mmaped by both VRouter and Agent/utility.
 * Separate chunks are mmaped by VRouter and each client connected.
 * Each memory chunk is divided into two rings: from VRouter's point of view
 * first comes RX ring, then TX ring. Client sees them the other way, so when
 * client writes to its TX ring, VRouter sees the message in its RX ring.
 * This way each ring is single producer-single consumer.
 *
 * Each ring has vr_nl_ring_buf structure set at its beginning. Then there comes
 * a message buffer. Size of vr_nl_ring_buf + size of message buffer of a ring
 * is a half of the mmaped memory. Then comes another vr_nl_ring_buf and another
 * message buffer space of the second ring in the same memory chunk.
 *
 * Every piece of data in the message buffer is preceded with vr_nl_ring_msg
 * structure which contains length of the following message.
 * Length may be set to (unsigned)-1 when message doesn't follow the header
 * immediately because there's not enough space after the header to write
 * the message. In such case, the message is written not after the header,
 * but starting from the beginning of the message buffer space. This works
 * provided there's enough free space there.
 *
 * New message may be saved to the ring if there's enough space from the head
 * pointer to the end of the ring or from the start of the buffer to the
 * (tail-1)th byte. Otherwise, -ENOMEM is returned by the writing function.
 *
 * If there's no new message to read, -ENOENT is returned by the reading
 * function.
 *
 * From VRouter point of view:
 *              RX Ring                          TX Ring
 * +-------------------------------+-------------------------------+
 * |vr_nl_ring_buf  |  msg buffer  |vr_nl_ring_buf  |  msg buffer  |
 * +-------------------------------+-------------------------------+
 * From Agent/utility point of view:
 *              TX RING                          RX Ring
 *
 * Data starts just after vr_nl_ring_buf and is pointed to by start[0]:
 * +------------------------------------------------------------------+
 * |vr_nl_ring_msg|MSG|vr_nl_ring_msg|MSG|vr_nl_ring_msg|MSG|  (...)  |
 * +------------------------------------------------------------------+
 *
 * vr_nl_ring_msg->start[offset] points to the offset-th byte in the
 * data buffer of the ring:
 *
 *                      vr_nl_ring_msg->start[128]
 *                                   |
 * |----------128 bytes--------------|
 * +------------------------------------------------------------------+
 * |vr_nl_ring_msg|MSG|vr_nl_ring_msg|MSG|vr_nl_ring_msg|MSG|  (...)  |
 * +------------------------------------------------------------------+
 *
 * Ring pointers.
 * Head points to the first byte of free space. Tail points
 * to the first byte of data that has not been read yet:
 * TAIL                 HEAD
 *   |                   |
 * +------------------------------------+
 * | |DATA DATA DATA DATA|              |
 * +------------------------------------+
 *
 * Reader moves tail atomically by the length of the data read:
 *                   TAIL==HEAD
 *                       |
 * +------------------------------------+
 * | / considered free  /               |
 * +------------------------------------+
 *
 * During next write data will be written eiter from head...
 *
 *                      TAIL      HEAD
 *                       |          |
 * +------------------------------------+
 * | / considered free  /| NEW DATA |   |
 * +------------------------------------+
 *
 * ...or if message does not fit from head to end of the ring,
 * dummy header with length set to -1 is put after head,
 * then head is set to 0 and message is written to the
 * beginning of the ring.
 * Writer moves head atomically after data is written.
 *
 *                          HEAD
 *                  TAIL      |
 *                   |        |new msg should go here but does not fit|
 * +------------------------------------+
 * |                 |old msg |         |
 * +------------------------------------+
 *
 *         HEAD     TAIL
 *          |        |
 * +------------------------------------+
 * ||new msg|        |old msg ||len = -1|
 * +------------------------------------+
 *
 * When reader staring from tail encounters
 * the dummy header, it will know that reading
 * should be done from the beginning:
 *         HEAD               TAIL
 *          |                  |
 * +------------------------------------+
 * ||new msg|        |old msg ||len = -1|
 * +------------------------------------+
 *
 * TAIL    HEAD
 *  |       |
 * +------------------------------------+
 * ||new msg|        |this has been read|
 * +------------------------------------+
 */


/* Size in bytes of one netlink ring, including header with tail and head */
#define VR_NL_RING_SZ       (1<<29) /* 512MB */
/* Size of shared memory for mmap: TX ring + RX ring + ring headers */
#define VR_NL_SHM_SZ        2 * (VR_NL_RING_SZ + sizeof(struct vr_nl_ring_buf))

/* Find next ring in the same shared memory chunk */
#define VR_NL_RING_NEXT(r)  (struct vr_nl_ring_buf *)((char *)r + VR_NL_SHM_SZ/2)

/* Listening socket file */
#define VR_NL_RING_DIR      "/var/run/vrouter"
#define VR_NL_SOCKET_FILE   VR_NL_RING_DIR"/dpdk_netlink"

/* Ring API */
/* Copy len bytes from src to ring */
int vr_nl_ring_enq(struct vr_nl_ring_buf *ring, void *src, uint32_t len);
/* Copy first message from ring to buffer of size buf_sz pointed to by dst */
int vr_nl_ring_deq(struct vr_nl_ring_buf *ring, void *dst, uint32_t buf_sz);

/* Copy messages from iovectors to ring */
int vr_nl_ring_msg_enq(struct vr_nl_ring_buf *ring, struct msghdr *msg);

/* Return in dst a pointer to the first message in the ring */
int vr_nl_ring_deq_ptr(struct vr_nl_ring_buf *ring, void **dst);
/* Move tail pointer behind the first message in the ring to mark it as read */
void vr_nl_ring_deq_finish(struct vr_nl_ring_buf *ring);

/* Return in dst a pointer to continuous free space of len bytes in ring */
int vr_nl_ring_enq_ptr(struct vr_nl_ring_buf *ring, void **dst, int len);
/* Move head pointer of len bytes to mark the space as used by message */
void vr_nl_ring_enq_finish(struct vr_nl_ring_buf *ring, int len);


