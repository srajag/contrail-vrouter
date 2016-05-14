/*
 * Copyright (C) 2016 Semihalf.
 *
 * vr_netlink_ring.c -- ring buffer for netlink messages over shared memory
 *
 */

#include "vr_netlink_ring.h"

#include <stdlib.h>

/**
 * returns in **dst a pointer to data after the first ring message header.
 * returns length of the data or -ENOENT if ring is empty.
 * tail pointer must be moved manually with vr_nl_ring_deq_finish() after
 * dequeued data is not needed anymore.
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
 * moves tail pointer of the ring by the length of the first portion of data.
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
 * checks if len bytes will fit in the ring.
 * returns pointer to continuous free space of len bytes in **dest
 * or -ENOMEM if data will not fit into the ring.
 * head pointer must be moved manually with vr_nl_ring_enq_finish() as soon as
 * data is ready to be processed by consumer.
 */
int
vr_nl_ring_enq_ptr(struct vr_nl_ring_buf *ring, void **dst, int len)
{
    unsigned head, tail;
    char *write_ptr;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    head = ring->head;
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);

    write_ptr = &ring->start[head];

    if (head + hdr_len + len <= VR_NL_RING_SZ) {
       /* data fits from current head to end of buffer */
        *dst = write_ptr + hdr_len;
    } else if (head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail) {
        /**
         * data fits from start to current tail.
         * write dummy header with len = -1 to current head,
         * then return pointer to the start of the buffer.
         */
        ((struct vr_nl_ring_msg *)write_ptr)->len = (uint32_t)-1;
        *dst = &ring->start[hdr_len];
    } else {
        return -ENOMEM;
    }

    /* return length of dequeued data */
    return len;
}

/**
 * moves head pointer of the ring by the len bytes.
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
    if (head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail)
        head = 0;

    /* set length of the new message */
    write_ptr = &ring->start[head];
    ((struct vr_nl_ring_msg *)write_ptr)->len = len;

    /* move head to the end of enqueued data */
    __atomic_store_n(&ring->head, head + hdr_len + len, __ATOMIC_RELEASE);
}

/**
 * memcpy iovector of data to ring and move head pointer.
 * returns number of bytes enqueued (excluding ring msg header)
 * or -ENOMEM if there's not enough space in ring.
 */
int
vr_nl_ring_msg_enq(struct vr_nl_ring_buf *ring, struct msghdr *msg)
{
    /* TODO add flags to make the function blocking/nonblocking? */

    char *write_ptr, *temp_write_ptr;
    struct iovec *iov = msg->msg_iov;
    unsigned int count = msg->msg_iovlen, i;
    unsigned head, tail;
    uint32_t len = 0;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    /* calculate length of data to enqueue */
    for (i = 0; i < count; i++)
        len += iov[i].iov_len;

    head = ring->head;
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);

    if (head + hdr_len + len <= VR_NL_RING_SZ) {
       /* data fits from current head to end of buffer */
        write_ptr = &ring->start[head];
    } else if (head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail) {
        /**
         * data fits from start to current tail.
         * write dummy header with len = -1 to current head,
         * then return pointer to the start of the buffer.
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
 * memcpy iovector of data from ring and move tail pointer.
 * returns length of the data or -ENOENT if ring is empty.
 */
int
vr_nl_ring_msg_deq(struct vr_nl_ring_buf *ring, struct msghdr *msg)
{
    /* TODO add flags to make the function blocking/nonblocking? */

    unsigned head, tail;
    int len, ret = 0;
    struct iovec *iov = msg->msg_iov;
    unsigned int count = 0;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    tail = ring->tail;

    while (head != tail && count < msg->msg_iovlen) {
        /* get message length */
        len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;

        /* check if producer has written to the start of the ring */
        if (len == (uint32_t)-1) {
            tail = 0;
            len = ((struct vr_nl_ring_msg *)&ring->start[tail])->len;
        }
        ret += len;

        /* copy message */
        memcpy(iov[count].iov_base, &ring->start[tail + hdr_len], len);

        /* move tail pointer by length of dequeued data */
        __atomic_store_n(&ring->tail, tail + hdr_len + len, __ATOMIC_RELEASE);

        head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
        tail += hdr_len + len;
        count++;
    }

    /* return length of dequeued data */
    return ret ? ret : -ENOENT;
}

/**
 * memcpy len bytes from src to ring.
 * returns number of bytes enqueued (excluding ring msg header)
 * or -ENOMEM if there's not enough space in ring.
 */
int
vr_nl_ring_enq(struct vr_nl_ring_buf *ring, void *src, uint32_t len)
{
    char *write_ptr;
    unsigned head, tail;
    uint8_t hdr_len = sizeof(struct vr_nl_ring_msg);

    head = ring->head;
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);

    if (head + hdr_len + len <= VR_NL_RING_SZ) {
       /* data fits from current head to end of buffer */
        write_ptr = &ring->start[head];
    } else if (head + hdr_len <= VR_NL_RING_SZ &&
                hdr_len + len < tail) {
        /**
         * data fits from start to current tail.
         * write dummy header with len = -1 to current head,
         * then return pointer to the start of the buffer.
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
 * memcpy first message from ring to dst and move the tail pointer.
 * returns length of dequeued data.
 */
int
vr_nl_ring_deq(struct vr_nl_ring_buf *ring, void **dst)
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

    /* copy data */
    *dst = malloc(len);
    memcpy(*dst, &ring->start[tail + hdr_len], len);

    /* move tail by the length of dequeued data */
    __atomic_store_n(&ring->tail, tail + hdr_len + len, __ATOMIC_RELEASE);

    /* return length of dequeued data */
    return len;
}
