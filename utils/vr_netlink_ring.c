/*
 * Copyright (C) 2016 Semihalf.
 *
 * vr_netlink_ring.c -- ring buffer for netlink messages over shared memory
 *
 */

#include "vr_netlink_ring.h"

#include <stdlib.h>


/**
 * @brief Get a pointer to the first new message in the ring. After message
 * is processed, vr_nl_ring_deq_finish() must be called.
 *
 * @param ring Pointer to RX ring
 * @param dst Pointer that will point to the message
 *
 * @return Length of message that dst points to
 * @retval -ENOENT if no data in ring
 */
int
vr_nl_ring_deq_ptr(struct vr_nl_ring_buf *ring, void **dst)
{
    unsigned head, tail;
    uint32_t len;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    tail = ring->tail;

    /* if ring is empty - nothing to return */
    if (head == tail) {
        *dst = NULL;
        return -ENOENT;
    }

    /* get message length */
    len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;

    /* check if producer has written to the start of the ring */
    if (len == (uint32_t)-1) {
        tail = 0;
        len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;
    }

    /* return pointer to the data */
    *dst = &ring->start[tail + hdr_len];

    /* return length of data */
    return len;
}

/**
 * @brief Move tail pointer in the ring after the first encountered
 * message. This basically marks the space occupied by the message as free.
 * Function assumes that there's at least one valid message in a ring, as it
 * should be called after vr_nl_ring_deq_ptr() only.
 *
 * @param ring Pointer to RX ring
 *
 * @return void
 */
void
vr_nl_ring_deq_finish(struct vr_nl_ring_buf *ring)
{
    unsigned head, tail;
    uint32_t len;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    tail = ring->tail;

    /* get message length */
    len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;

    /* check if producer has written to the start of the ring */
    if (len == (uint32_t)-1) {
        tail = 0;
        len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;
    }

    /* move tail by the len of the data */
    __atomic_store_n(&ring->tail, tail + hdr_len + len, __ATOMIC_RELEASE);
}

/**
 * @brief Get a pointer to the continuous space of requested length.
 * After message is written, vr_nl_ring_enq_finish() must be called.
 *
 * @param ring Pointer to TX ring
 * @param dst Pointer that will point to the free space
 * @param len Requested length
 *
 * @return Length of requested space
 * @retval -ENOMEM if there's no free space in the ring
 */
int
vr_nl_ring_enq_ptr(struct vr_nl_ring_buf *ring, void **dst, int len)
{
    unsigned head, tail;
    char *write_ptr;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    /* work on local copies of ring pointers */
    head = ring->head;
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);

    write_ptr = &ring->start[head];

    /* find free space in buffer */
    if ((head >= tail && head + hdr_len + len <= VR_NL_RING_SZ) ||
        (head < tail && tail - head < len)) {
       /**
        * data fits from current head to eiter end of buffer
        * or to (tail-1) if head wrapped around during previous write.
        */
        *dst = write_ptr + hdr_len;
    } else if (head >= tail && head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail) {
        /**
         * data fits from start to current tail - need to do a wrap around.
         * write dummy header with len = -1 to current head,
         * then write proper header and data to the start of the buffer.
         */
        ((struct vr_nl_ring_msg *)write_ptr)->len = (uint32_t)-1;
        *dst = &ring->start[hdr_len];
    } else {
        return -ENOMEM;
    }

    /* return length of requested space */
    return len;
}

/**
 * @brief Move head pointer in the ring to point after message written
 * with vr_nl_ring_enq_ptr(). Function checks if data was written with wrap
 * around.
 *
 * @param ring Pointer to TX ring
 * @param len Length of message written with vr_nl_ring_enq_ptr()
 *
 * @return void
 */
void
vr_nl_ring_enq_finish(struct vr_nl_ring_buf *ring, int len)
{
    unsigned head, tail;
    char *write_ptr;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    head = ring->head;
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);

    /* check if we're writing from current head or from the start of the ring */
    if (head > tail && head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail)
        head = 0;

    /* set length of the new message */
    write_ptr = &ring->start[head];
    ((struct vr_nl_ring_msg *)write_ptr)->len = len;

    /* move head to the end of enqueued data */
    __atomic_store_n(&ring->head, head + hdr_len + len, __ATOMIC_RELEASE);
}

/**
 * @brief Copy data from iovectors to the ring.
 *
 * @param ring Pointer to TX ring
 * @param msg Message header with pointer to iovector
 *
 * @return Length of data written
 * @retval -ENOMEM if there's no free space in the ring
 */
int
vr_nl_ring_msg_enq(struct vr_nl_ring_buf *ring, struct msghdr *msg)
{
    char *write_ptr, *temp_write_ptr;
    struct iovec *iov = msg->msg_iov;
    unsigned int count = msg->msg_iovlen, i;
    unsigned head, tail;
    uint32_t len = 0;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    /* calculate length of data to enqueue */
    for (i = 0; i < count; i++)
        len += iov[i].iov_len;

    /**
     * XXX I assume the whole bunch of data will fit continuously somewhere.
     * There's no support of writing iovs with wrap around at the moment.
     * However, writing thousands of requests at a time seems to be an extremely
     * rare case, so there's probably no need to worry about that.
     */

    /* work on local copies of ring pointers */
    head = ring->head;
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);

    /* find free space in buffer */
    if ((head >= tail && head + hdr_len + len <= VR_NL_RING_SZ) ||
        (head < tail && tail - head < len)) {
       /**
        * data fits from current head to eiter end of buffer
        * or to (tail-1) if head wrapped around during previous write.
        */
        write_ptr = &ring->start[head];
    } else if (head >= tail && head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail) {
        /**
         * data fits from start to current tail - need to do a wrap around.
         * write dummy header with len = -1 to current head,
         * then write proper header and data to the start of the buffer.
         */
        write_ptr = &ring->start[head];
        ((struct vr_nl_ring_msg *)write_ptr)->len = (uint32_t)-1;
        write_ptr = &ring->start[0];
        head = 0;
    } else {
        return -ENOMEM;
    }

    /* set length of the new message and copy it to the ring */
    ((struct vr_nl_ring_msg *)write_ptr)->len = len;
    temp_write_ptr = write_ptr + hdr_len;
    for (i = 0; i < count; i++) {
        memcpy(temp_write_ptr, iov[i].iov_base, iov[i].iov_len);
        temp_write_ptr += iov[i].iov_len;
    }

    /* move head to the end of enqueued data */
    __atomic_store_n(&ring->head, head + hdr_len + len, __ATOMIC_RELEASE);

    return len;
}

/**
 * @brief Copy data to the ring.
 *
 * @param ring Pointer to TX ring
 * @param src Pointer to data that needs to be written
 * @param len Length of data pointed by src
 *
 * @return Length of data written
 * @retval -ENOMEM if there's no free space in the ring
 */
int
vr_nl_ring_enq(struct vr_nl_ring_buf *ring, void *src, uint32_t len)
{
    char *write_ptr;
    unsigned head, tail;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    /* work on local copies of ring pointers */
    head = ring->head;
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);

    /* find free space in buffer */
    if ((head >= tail && head + hdr_len + len <= VR_NL_RING_SZ) ||
        (head < tail && tail - head < len)) {
       /**
        * data fits from current head to eiter end of buffer
        * or to (tail-1) if head wrapped around during previous write.
        */
        write_ptr = &ring->start[head];
    } else if (head >= tail && head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail) {
        /**
         * data fits from start to current tail - need to do a wrap around.
         * write dummy header with len = -1 to current head,
         * then write proper header and data to the start of the buffer.
         */
        write_ptr = &ring->start[head];
        ((struct vr_nl_ring_msg *)write_ptr)->len = (uint32_t)-1;
        write_ptr = &ring->start[0];
        head = 0;
    } else {
        return -ENOMEM;
    }

    /* set length of the new message and copy it to the ring */
    ((struct vr_nl_ring_msg *)write_ptr)->len = len;
    memcpy(write_ptr + hdr_len, src, len);

    /* move head to the end of enqueued data */
    __atomic_store_n(&ring->head, head + hdr_len + len, __ATOMIC_RELEASE);

    return len;
}

/**
 * @brief Copy data from the ring.
 *
 * @param ring Pointer to RX ring
 * @param dst Pointer to preallocated buffer
 * @param buf_sz Length of data buffer pointed by dst
 *
 * @return Length of data written
 * @retval -ENOMEM if there's no free space in the dst buffer
 * @retval -ENOENT if there's no new messages in the ring
 */
int
vr_nl_ring_deq(struct vr_nl_ring_buf *ring, void *dst, uint32_t buf_sz)
{
    unsigned head, tail;
    uint32_t len;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    /* work on local copies of ring pointers */
    head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    tail = ring->tail;

    /* if ring is empty - nothing to return */
    if (head == tail)
        return -ENOENT;

    /* get message length */
    len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;

    /* check if producer has written to the start of the ring */
    if (len == (uint32_t)-1) {
        tail = 0;
        len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;
    }

    /* check if message will fit into destination buffer */
    if (buf_sz < len)
        return -ENOMEM;

    /* copy data */
    memcpy(dst, &ring->start[tail + hdr_len], len);

    /* move tail by the length of dequeued data */
    __atomic_store_n(&ring->tail, tail + hdr_len + len, __ATOMIC_RELEASE);

    /* return length of dequeued data */
    return len;
}
