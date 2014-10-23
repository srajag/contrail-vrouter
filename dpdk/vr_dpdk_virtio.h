/*
 * vr_dpdk_virtio.h - header for DPDK virtio forwarding infrastructure.
 *
 *
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VR_DPDK_VIRTIO_H__
#define __VR_DPDK_VIRTIO_H__

/*
 * Burst size for packets from a VM
 */
#define VR_DPDK_VIRTIO_RX_BURST_SZ VR_DPDK_MAX_BURST_SZ 
/*
 * Burst size for packets to a VM
 */
#define VR_DPDK_VIRTIO_TX_BURST_SZ VR_DPDK_MAX_BURST_SZ 

typedef enum vq_ready_state {
    VQ_NOT_READY = 1,
    VQ_READY,
} vq_ready_state_t;

typedef struct vr_dpdk_virtioq {
    vq_ready_state_t vdv_ready_state;
    int vdv_enabled_state;
    unsigned int vdv_vif_idx;
    int vdv_zero_copy;
    struct vring_desc *vdv_desc;
    struct vring_avail *vdv_avail;
    struct vring_used *vdv_used;
    unsigned int vdv_base_idx;
    struct vhost_vring_state vdv_vvs;
    uint16_t vdv_soft_avail_idx;
    uint16_t vdv_soft_used_idx;
    struct rte_mbuf *vdv_tx_mbuf[2 * VR_DPDK_VIRTIO_TX_BURST_SZ];
    uint32_t vdv_tx_mbuf_count;
} vr_dpdk_virtioq_t;

uint16_t vr_dpdk_virtio_nrxqs(struct vr_interface *vif);
uint16_t vr_dpdk_virtio_ntxqs(struct vr_interface *vif);
struct vr_dpdk_rx_queue *
vr_dpdk_virtio_rx_queue_init(unsigned int lcore_id, struct vr_interface *vif,
                             unsigned int q_id);
struct vr_dpdk_tx_queue *
vr_dpdk_virtio_tx_queue_init(unsigned int lcore_id, struct vr_interface *vif,
                             unsigned int q_id);
int vr_dpdk_virtio_set_vring_base(unsigned int vif_idx, unsigned int vring_idx,
                                   unsigned int vring_base);
int vr_dpdk_virtio_get_vring_base(unsigned int vif_idx, unsigned int vring_idx,
                                  unsigned int *vring_basep);
int vr_dpdk_set_vring_addr(unsigned int vif_idx, unsigned int vring_idx,
                           struct vring_desc *vrucv_desc,
                           struct vring_avail *vrucv_avail,
                           struct vring_used *vrucv_used);
int vr_dpdk_set_ring_num_desc(unsigned int vif_idx, unsigned int vring_idx,
                              unsigned int num_desc);
int vr_dpdk_set_virtq_ready(unsigned int vif_idx, unsigned int vring_idx,
                            vq_ready_state_t ready);
void vr_dpdk_virtio_set_vif_client(unsigned int idx, void *client);
void *vr_dpdk_virtio_get_vif_client(unsigned int idx);
#endif /* __VR_DPDK_VIRTIO_H__ */
