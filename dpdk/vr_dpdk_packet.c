/*
 * vr_dpdk_packet.c -- the packet interface
 *
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved
 */
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#include "vr_queue.h"
#include "vr_dpdk.h"
#include "vr_dpdk_usocket.h"

int dpdk_packet_core_id = -1;

void
vr_dpdk_packet_tx(struct vr_dpdk_lcore *lcorep)
{
    int ret;
    uint64_t event = 1;

    if (lcorep->lcore_event_sock) {
        ret = vr_usocket_write(lcorep->lcore_event_sock, (unsigned char *)&event,
                sizeof(event));
        if (ret < 0) {
            vr_usocket_close(lcorep->lcore_event_sock);
            lcorep->lcore_event_sock = NULL;
        }
    }
}

int
dpdk_packet_io(void)
{
    int ret;
    struct vr_dpdk_lcore *lcore = vr_dpdk.lcores[rte_lcore_id()];

wait_for_connection:
    while (!vr_dpdk.packet_transport) {
        /* handle an IPC command */
        if (unlikely(vr_dpdk_lcore_cmd_handle(lcore)))
            return -1;
        usleep(VR_DPDK_SLEEP_SERVICE_US);
    }

    ret = vr_usocket_io(vr_dpdk.packet_transport);
    if (ret < 0) {
        vr_dpdk.packet_transport = NULL;
        /* handle an IPC command */
        if (unlikely(vr_dpdk_lcore_cmd_handle(lcore)))
            return -1;
        usleep(VR_DPDK_SLEEP_SERVICE_US);
        goto wait_for_connection;
    }

    return ret;
}

void
dpdk_packet_socket_close(void)
{
    void *usockp;

    if (!vr_dpdk.packet_transport)
        return;
    usockp = vr_dpdk.packet_transport;

    vr_dpdk.packet_transport = NULL;

    vr_usocket_close(usockp);

    return;
}

int
dpdk_packet_socket_init(void)
{
    unsigned lcore_id;
    struct vr_dpdk_lcore *lcorep;

    vr_dpdk.packet_transport = (void *)vr_usocket(PACKET, RAW);
    if (!vr_dpdk.packet_transport)
        return -1;

    if (rte_lcore_count() == VR_DPDK_MIN_LCORES) {
        RTE_LOG(INFO, VROUTER, "Setting packet socket to non-blocking\n");
        vr_usocket_non_blocking(vr_dpdk.packet_transport);
    }

    if (!vr_dpdk.packet_ring) {
        vr_dpdk.packet_ring = rte_ring_lookup("pkt0_tx");
        if (!vr_dpdk.packet_ring) {
            vr_dpdk.packet_ring = rte_ring_create("pkt0_tx", VR_DPDK_TX_RING_SZ,
                    SOCKET_ID_ANY, 0);
            if (!vr_dpdk.packet_ring) {
                goto error;
            }
        }
    }

    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        lcorep = vr_dpdk.lcores[lcore_id];
        lcorep->lcore_event_sock = (void *)vr_usocket(EVENT, RAW);
        if (!lcorep->lcore_event_sock) {
            goto error;
        }

        if (vr_usocket_bind_usockets(vr_dpdk.packet_transport,
                    lcorep->lcore_event_sock))
            goto error;
    }

    return 0;

error:
    vr_usocket_close(vr_dpdk.packet_transport);
    vr_dpdk.packet_transport = NULL;

    return -ENOMEM;
}
