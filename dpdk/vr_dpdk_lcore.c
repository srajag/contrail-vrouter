/*
 * Copyright (C) 2014 Semihalf.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * vr_dpdk_lcore.c -- lcore support functions
 *
 */

#include <sched.h>
#include <rte_malloc.h>
#include <urcu-qsbr.h>
#include <linux/vhost.h>

#include "vr_dpdk.h"
#include "vr_dpdk_usocket.h"
#include "vr_dpdk_virtio.h"

/* 
 * vr_dpdk_phys_lcore_least_used_get - returns the least used lcore among the 
 * ones that handle TX for physical interfaces.
 */
unsigned int
vr_dpdk_phys_lcore_least_used_get(void)
{
    unsigned lcore_id;
    struct vr_dpdk_lcore *lcore;
    unsigned least_used_id = RTE_MAX_LCORE;
    uint16_t least_used_nb_queues = 2 * VR_MAX_INTERFACES;
    unsigned int num_queues;

    RTE_LCORE_FOREACH(lcore_id) {
        lcore = vr_dpdk.lcores[lcore_id];
        num_queues = lcore->lcore_nb_rx_queues + lcore->lcore_nb_rings_to_push;
        if (num_queues < least_used_nb_queues) {
            least_used_nb_queues = num_queues;
            least_used_id = lcore_id;
        }
    }

    return least_used_id;
}

/* Returns the least used lcore or RTE_MAX_LCORE */
unsigned
vr_dpdk_lcore_least_used_get(void)
{
    unsigned lcore_id;
    struct vr_dpdk_lcore *lcore;
    unsigned least_used_id = RTE_MAX_LCORE;
    uint16_t least_used_nb_queues = 2 * VR_MAX_INTERFACES;
    unsigned int num_queues;

    RTE_LCORE_FOREACH(lcore_id) {
        if (lcore_id == vr_dpdk.packet_lcore_id)
            continue;
        lcore = vr_dpdk.lcores[lcore_id];

        num_queues = lcore->lcore_nb_rx_queues +
                      lcore->lcore_nb_rings_to_push;
        if (num_queues < least_used_nb_queues) {
            least_used_nb_queues = num_queues;
            least_used_id = lcore_id;
        }
    }

    return least_used_id;
}

/* Add a RX queue to a lcore */
void
dpdk_lcore_rx_queue_add(unsigned lcore_id, struct vr_dpdk_rx_queue *rx_queue)
{
    struct vr_dpdk_lcore *lcore = vr_dpdk.lcores[lcore_id];
    uint8_t queue_id = rx_queue - &lcore->lcore_rx_queues[0];

    rte_wmb();

    /* set mask to enable the queue */
    lcore->lcore_rx_queues_mask |= 1ULL << queue_id;

    /* increase the number of RX queues */
    lcore->lcore_nb_rx_queues++;
}

/* Add a TX queue to a lcore */
void
dpdk_lcore_tx_queue_add(unsigned lcore_id, struct vr_dpdk_tx_queue *tx_queue)
{
    struct vr_dpdk_lcore *lcore = vr_dpdk.lcores[lcore_id];
    unsigned vif_idx = tx_queue->txq_vif->vif_idx;
    struct vr_dpdk_tx_queue *prev_tx_queue;
    struct vr_dpdk_tx_queue *cur_tx_queue;

    /* write barrier */
    rte_wmb();

    /* add queue to the list */
    if (SLIST_EMPTY(&lcore->lcore_tx_head)) {
        /* insert first queue */
        SLIST_INSERT_HEAD(&lcore->lcore_tx_head, tx_queue, txq_next);
    } else {
        /* sort TX queues by vif_idx to optimize CPU cache usage */
        prev_tx_queue = NULL;
        SLIST_FOREACH(cur_tx_queue, &lcore->lcore_tx_head, txq_next) {
            if (cur_tx_queue->txq_vif->vif_idx < vif_idx)
                prev_tx_queue = cur_tx_queue;
            else
                break;
        }
        /* insert new queue */
        if (prev_tx_queue == NULL)
            SLIST_INSERT_HEAD(&lcore->lcore_tx_head, tx_queue, txq_next);
        else
            SLIST_INSERT_AFTER(prev_tx_queue, tx_queue, txq_next);
    }
}

/* Schedule an MPLS label queue */
int
vr_dpdk_lcore_mpls_schedule(struct vr_interface *vif, unsigned dst_ip,
    unsigned mpls_label)
{
    int queue_id, ret;
    struct vr_dpdk_rx_queue *rx_queue;
    unsigned least_used_id = vr_dpdk_lcore_least_used_get();

    if (least_used_id == RTE_MAX_LCORE) {
        RTE_LOG(ERR, VROUTER, "\terror getting the least used lcore ID\n");
        return -EFAULT;
    }

    queue_id = vr_dpdk_ethdev_ready_queue_id_get(vif);
    if (queue_id < 0)
        return -ENOMEM;

    /* add hardware filter */
    ret = vr_dpdk_ethdev_filter_add(vif, queue_id, dst_ip, mpls_label);
    if (ret < 0)
        return ret;

    /* init RX queue */
    RTE_LOG(INFO, VROUTER, "\tlcore %u RX from filtering queue %d MPLS %u\n",
        least_used_id, queue_id, mpls_label);
    rx_queue = vr_dpdk_ethdev_rx_queue_init(least_used_id, vif, queue_id);
    if (rx_queue == NULL)
        return -EFAULT;

    /* add the queue to the lcore */
    dpdk_lcore_rx_queue_add(least_used_id, rx_queue);

    return 0;
}

/* Schedule an interface */
int
vr_dpdk_lcore_if_schedule(struct vr_interface *vif, unsigned least_used_id,
    uint16_t nb_rx_queues, vr_dpdk_rx_queue_init_op rx_queue_init_op,
    uint16_t nb_tx_queues, vr_dpdk_tx_queue_init_op tx_queue_init_op)
{
    unsigned lcore_id;
    uint16_t queue_id;
    struct vr_dpdk_rx_queue *rx_queue;
    struct vr_dpdk_tx_queue *tx_queue;
    struct vr_dpdk_lcore *lcore;

    if (least_used_id == RTE_MAX_LCORE) {
        RTE_LOG(ERR, VROUTER, "\terror getting the least used lcore ID\n");
        return -EFAULT;
    }

    /* init TX queues starting with the least used lcore */
    lcore_id = least_used_id;
    queue_id = 0;
    /* for all lcores */
    do {
        /* never use netlink lcore */
        lcore = vr_dpdk.lcores[lcore_id];
        if (lcore->lcore_nb_rx_queues >= VR_MAX_INTERFACES) {
            /* do not skip master lcore but wrap */
            lcore_id = rte_get_next_lcore(lcore_id, 0, 1);
            continue;
        }

        /* init hardware or ring queue */
        if (((lcore_id != vr_dpdk.packet_lcore_id) ||
                    (nb_tx_queues > vr_dpdk.nb_fwd_lcores)) &&
                (queue_id < nb_tx_queues)) {
            /* there is a hardware queue available */
            RTE_LOG(INFO, VROUTER, "\tlcore %u TX to HW queue %" PRIu16 "\n",
                lcore_id, queue_id);
            tx_queue = (*tx_queue_init_op)(lcore_id, vif, queue_id);
            if (tx_queue == NULL)
                return -EFAULT;
            /* next queue */
            queue_id++;
        } else {
            /* no more hardware queues left, so we use rings instead */
            RTE_LOG(INFO, VROUTER, "\tlcore %u TX to SW ring\n", lcore_id);
            tx_queue = vr_dpdk_ring_tx_queue_init(lcore_id, vif, least_used_id);
            if (tx_queue == NULL)
                return -EFAULT;
        }

        /* add the queue to the lcore */
        dpdk_lcore_tx_queue_add(lcore_id, tx_queue);

        /* do not skip master lcore but wrap */
        lcore_id = rte_get_next_lcore(lcore_id, 0, 1);
    } while (lcore_id != least_used_id);

    /* init RX queues starting with the least used lcore */
    lcore_id = least_used_id;
    queue_id = 0;
    /* for all lcores */
    do {
        /* never use service lcores */
        lcore = vr_dpdk.lcores[lcore_id];
        if (lcore->lcore_nb_rx_queues >= VR_MAX_INTERFACES) {
            /* do not skip master lcore but wrap */
            lcore_id = rte_get_next_lcore(lcore_id, 0, 1);
            continue;
        }

        /* init hardware or ring queue */
        if (lcore_id != vr_dpdk.packet_lcore_id) {
            if (queue_id < nb_rx_queues) {
                /* there is a hardware queue available */
                RTE_LOG(INFO, VROUTER, "\tlcore %u RX from HW queue %" PRIu16
                        "\n", lcore_id, queue_id);
                rx_queue = (*rx_queue_init_op)(lcore_id, vif, queue_id);
                if (rx_queue == NULL)
                    return -EFAULT;

                /* add the queue to the lcore */
                dpdk_lcore_rx_queue_add(lcore_id, rx_queue);

                /* next queue */
                queue_id++;
            } else {
                /* break if no more hardware queues left */
                break;
            }
        }

        /* do not skip master lcore but wrap */
        lcore_id = rte_get_next_lcore(lcore_id, 0, 1);
    } while (lcore_id != least_used_id);

    return 0;
}

/* Flush TX queues */
static inline void
dpdk_lcore_flush(struct vr_dpdk_lcore *lcore)
{
    struct vr_dpdk_tx_queue *tx_queue;

    SLIST_FOREACH(tx_queue, &lcore->lcore_tx_head, txq_next) {
        tx_queue->txq_ops.f_flush(tx_queue->txq_queue_h);
    }
    /* TODO: find a better place to call the function */
    vr_dpdk_packet_tx();
}

/* Send a burst of packets to vRouter */
static inline void
dpdk_vroute(struct vr_interface *vif, struct rte_mbuf *pkts[VR_DPDK_MAX_BURST_SZ],
    uint32_t nb_pkts)
{
    unsigned i;
    struct rte_mbuf *mbuf;
    struct vr_packet *pkt;

    RTE_LOG(DEBUG, VROUTER, "%s: RX %" PRIu32 " packet(s) from interface %s\n",
         __func__, nb_pkts, vif->vif_name);
    for (i = 0; i < nb_pkts; i++) {
        mbuf = pkts[i];
#ifdef VR_DPDK_RX_PKT_DUMP
        rte_pktmbuf_dump(stdout, mbuf, 0x60);
#endif
        rte_prefetch0(vr_dpdk_mbuf_to_pkt(mbuf));
        rte_prefetch0(rte_pktmbuf_mtod(mbuf, void *));

        /* convert mbuf to vr_packet */
        pkt = vr_dpdk_packet_get(mbuf, vif);
        /* send the packet to vRouter */
        vif->vif_rx(vif, pkt, VLAN_ID_INVALID);
    }
}

/* Forwarding lcore RX */
static inline uint32_t
dpdk_lcore_fwd_rx(struct vr_dpdk_lcore *lcore, struct rte_mbuf **pkts,
    uint64_t *rx_queues_mask)
{
    struct vr_dpdk_rx_queue *rx_queue = &lcore->lcore_rx_queues[0];
    uint64_t cur_queues_mask = *rx_queues_mask;
    uint64_t cur_queue = 1;
    uint32_t nb_pkts = 0;
    struct vr_packet *pkt_arr[VR_DPDK_MAX_BURST_SZ];
    int pkti;

    /* for all RX queues */
    while (cur_queues_mask) {
        rte_prefetch0(rx_queue);
        if (likely(cur_queues_mask & cur_queue)) {
            /* burst RX */
            nb_pkts = rx_queue->rxq_ops.f_rx(rx_queue->rxq_queue_h, pkts,
                rx_queue->rxq_burst_size);
            cur_queues_mask &= ~cur_queue;
            if (likely(nb_pkts > 0)) {
                /* transmit packets to vrouter */
                if (vif_is_virtual(rx_queue->rxq_vif)) {
                    for (pkti = 0; pkti < nb_pkts; pkti++) {
                        pkt_arr[pkti] = vr_dpdk_packet_get(pkts[pkti],
                                                           rx_queue->rxq_vif);
                    }
                    vr_dpdk_virtio_enq_pkts_to_phys_lcore(rx_queue,
                                                          pkt_arr, nb_pkts);
                } else {
                    dpdk_vroute(rx_queue->rxq_vif, pkts, nb_pkts);
                }
            } else {
                /* mark the queue as empty */
                *rx_queues_mask &= ~cur_queue;
            }
        }
        cur_queue <<= 1;
        rx_queue++;
    }
    return nb_pkts;
}

/* Forwarding lcore IO */
static inline void
dpdk_lcore_fwd_io(struct vr_dpdk_lcore *lcore)
{
    uint64_t rx_queues_mask = lcore->lcore_rx_queues_mask;
    uint64_t total_pkts = 0;
    struct rte_mbuf *pkts[VR_DPDK_MAX_BURST_SZ];
    uint32_t nb_pkts;
    int i;
    struct vr_dpdk_ring_to_push *rtp;
    struct vr_packet *pkt;
    int pkti;
    uint16_t num_tx_rings, max_tx_rings;

    total_pkts += dpdk_lcore_fwd_rx(lcore, &pkts[0], &rx_queues_mask);
    total_pkts += dpdk_lcore_fwd_rx(lcore, &pkts[0], &rx_queues_mask);
    total_pkts += dpdk_lcore_fwd_rx(lcore, &pkts[0], &rx_queues_mask);
    total_pkts += dpdk_lcore_fwd_rx(lcore, &pkts[0], &rx_queues_mask);
    total_pkts += dpdk_lcore_fwd_rx(lcore, &pkts[0], &rx_queues_mask);

    /* for all TX rings to push */
    rtp = &lcore->lcore_rings_to_push[0];
    max_tx_rings = lcore->lcore_nb_rings_to_push;
    num_tx_rings = 0;
    while ((num_tx_rings < max_tx_rings) &&
              ((rtp - &lcore->lcore_rings_to_push[0]) < VR_DPDK_MAX_RINGS)) {
        if (rtp->rtp_tx_ring == NULL) {
            /*
             * TODO - need ot handle decrementing lcore_nb_rings_to_push
             * when vif is deleted.
             */
            rtp++;
            continue;
        }

        nb_pkts = rte_ring_sc_dequeue_burst(rtp->rtp_tx_ring, (void **)pkts,
            VR_DPDK_MAX_BURST_SZ-1);
        if (likely(nb_pkts != 0)) {
            total_pkts += nb_pkts;

            if (rtp->rtp_tx_queue) {
                /* push packets to the TX queue */
                for (i = 0; i < nb_pkts; i++) {
                    rtp->rtp_tx_queue->txq_ops.f_tx(
                        rtp->rtp_tx_queue->txq_queue_h, pkts[i]);
                }
            } else {
                for (pkti = 0; pkti < nb_pkts; pkti++) {
                    pkt = (struct vr_packet *) pkts[pkti];
                    pkt->vp_if->vif_rx(pkt->vp_if, pkt, VLAN_ID_INVALID);
                }
            }
        }
        rtp++;
        num_tx_rings++;
    }

    rcu_quiescent_state();

#if VR_DPDK_SLEEP_NO_PACKETS_US > 0
    /* sleep if no single packet received */
    if (unlikely(total_pkts == 0)) {
        usleep(VR_DPDK_SLEEP_NO_PACKETS_US);
    }
#endif
#if VR_DPDK_YIELD_NO_PACKETS > 0
    /* yield if no single packet received */
    if (unlikely(total_pkts == 0)) {
        sched_yield();
    }
#endif
}

/* Init forwarding lcore */
static int
dpdk_lcore_init(void)
{
    const unsigned lcore_id = rte_lcore_id();
    struct vr_dpdk_lcore *lcore;

    /* allocate lcore context */
    lcore = rte_zmalloc_socket("vr_dpdk_lcore", sizeof(struct vr_dpdk_lcore),
        CACHE_LINE_SIZE, rte_socket_id());
    if (lcore == NULL) {
        RTE_LOG(CRIT, VROUTER, "Error allocating lcore %u context\n", lcore_id);
        return -ENOMEM;
    }

    /* init lcore lists */
    SLIST_INIT(&lcore->lcore_tx_head);

    vr_dpdk.lcores[lcore_id] = lcore;

    rcu_register_thread();
    rcu_thread_offline();

    return 0;
}

/* Exit forwarding lcore */
static void
dpdk_lcore_exit()
{
    const unsigned lcore_id = rte_lcore_id();
    struct vr_dpdk_lcore *lcore = vr_dpdk.lcores[lcore_id];

    rcu_unregister_thread();

    /* free lcore context */
    vr_dpdk.lcores[lcore_id] = NULL;
    rte_free(lcore);
}

/* Forwarding lcore main loop */
int
dpdk_lcore_fwd_loop(struct vr_dpdk_lcore *lcore)
{
    /* cycles counters */
    uint64_t cur_cycles = 0;
    uint64_t diff_cycles;
    uint64_t last_tx_cycles = 0;
#if VR_DPDK_USE_TIMER
    /* calculate timeouts in CPU cycles */
    const uint64_t tx_flush_cycles = (rte_get_timer_hz() + US_PER_S - 1)
        * VR_DPDK_TX_FLUSH_US / US_PER_S;
#else
    const uint64_t tx_flush_cycles = VR_DPDK_TX_FLUSH_LOOPS;
#endif

    RTE_LOG(DEBUG, VROUTER, "Hello from forwarding lcore %u\n", rte_lcore_id());

    while (1) {
        rte_prefetch0(lcore);

        /* update cycles counter */
#if VR_DPDK_USE_TIMER
        cur_cycles = rte_get_timer_cycles();
#else
        cur_cycles++;
#endif

        /* run forwarding lcore IO */
        dpdk_lcore_fwd_io(lcore);

        /* check if we need to flush TX queues */
        diff_cycles = cur_cycles - last_tx_cycles;
        if (unlikely(tx_flush_cycles < diff_cycles)) {
            /* update TX flush cycles */
            last_tx_cycles = cur_cycles;

            /* flush all TX queues */
            dpdk_lcore_flush(lcore);

            if (unlikely(lcore->lcore_nb_rx_queues == 0)) {
                /* no queues to poll -> sleep a bit */
                usleep(VR_DPDK_SLEEP_NO_QUEUES_US);
            }

            /* check for the stop flag */
            if (unlikely(rte_atomic16_read(&lcore->lcore_stop_flag) != 0))
                break;
        } /* flush TX queues */
    } /* lcore loop */

    RTE_LOG(DEBUG, VROUTER, "Bye-bye from forwarding lcore %u\n", rte_lcore_id());

    return 0;
}

/* Service lcore main loop */
int
dpdk_lcore_service_loop(struct vr_dpdk_lcore *lcore, unsigned netlink_lcore_id,
    unsigned packet_lcore_id)
{
    unsigned lcore_id = rte_lcore_id();

    RTE_LOG(DEBUG, VROUTER, "Hello from service lcore %u\n", rte_lcore_id());

    /* never schedule interfaces on the service lcore */
    if (lcore_id != packet_lcore_id) {
        lcore->lcore_nb_rx_queues = VR_MAX_INTERFACES;
    } else {
        vr_dpdk.packet_lcore_id = packet_lcore_id;
    }

    while (1) {
        rte_prefetch0(lcore);

        if (lcore_id == netlink_lcore_id) {
            RTE_LOG(DEBUG, VROUTER, "%s: NetLink IO on lcore %u\n",
                __func__, rte_lcore_id());
            dpdk_netlink_io();
        }

        if (lcore_id == packet_lcore_id) {
            RTE_LOG(DEBUG, VROUTER, "%s: packet IO on lcore %u\n",
                __func__, rte_lcore_id());
            dpdk_packet_io();
        }

        if (netlink_lcore_id != packet_lcore_id)
            break;

        usleep(VR_DPDK_SLEEP_SERVICE_US);
        /* check for the stop flag */
        if (unlikely(rte_atomic16_read(&lcore->lcore_stop_flag) != 0))
            break;
    } /* lcore loop */

    RTE_LOG(DEBUG, VROUTER, "Bye-bye from service lcore %u\n", rte_lcore_id());

    return 0;
}

/* Launch lcore main loop */
int
vr_dpdk_lcore_launch(__attribute__((unused)) void *dummy)
{
    const unsigned lcore_id = rte_lcore_id();
    struct vr_dpdk_lcore *lcore;
    const unsigned netlink_lcore_id = rte_get_master_lcore();
    /* skip master lcore and wrap */
    unsigned packet_lcore_id = rte_get_next_lcore(netlink_lcore_id, 1, 1);

    /* init forwarding lcore */
    if (dpdk_lcore_init() != 0)
        return -ENOMEM;

    /* set current lcore context */
    lcore = vr_dpdk.lcores[lcore_id];

    if (rte_lcore_count() == VR_DPDK_MIN_LCORES) {
        /* use master lcore for packet and NetLink handling */
        packet_lcore_id = netlink_lcore_id;
    }

    if (lcore_id == netlink_lcore_id || lcore_id == packet_lcore_id)
        dpdk_lcore_service_loop(lcore, netlink_lcore_id, packet_lcore_id);
    else
        dpdk_lcore_fwd_loop(lcore);

    /* exit forwarding lcore */
    dpdk_lcore_exit();

    return 0;
}
