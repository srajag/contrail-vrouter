/*
 * vr_ip4_mtrie.h --
 *
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __VR_IP4_MTRIE_H__
#define __VR_IP4_MTRIE_H__

#ifdef __SSE4_2__
#include <nmmintrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
struct ip4_bucket;

/*
 * Override the least significant bit of a pointer to indicate whether it
 * points to a bucket or nexthop.
 */
#define ENTRY_IS_BUCKET(EPtr)        (((EPtr)->entry_long_i) & 0x1ul)
#define ENTRY_IS_NEXTHOP(EPtr)       !ENTRY_IS_BUCKET(EPtr)

#define PTR_IS_BUCKET(ptr)           ((ptr) & 0x1ul)
#define PTR_IS_NEXTHOP(ptr)          !PTR_IS_BUCKET(ptr)
#define PTR_TO_BUCKET(ptr)           ((struct ip4_bucket *)((ptr) ^ 0x1ul))
#define PTR_TO_NEXTHOP(ptr)          ((struct vr_nexthop *)(ptr))

struct ip4_bucket_entry {
    union {
        struct vr_nexthop *nexthop_p;
        struct ip4_bucket *bucket_p;
        unsigned long      long_i;
    } entry_data;

    unsigned int entry_prefix_len:8;
    unsigned int entry_label_flags:4;
    unsigned int entry_label:20;
};

#define entry_nh_p      entry_data.nexthop_p
#define entry_bkt_p     entry_data.bucket_p
#define entry_long_i    entry_data.long_i

struct ip4_bucket {
    struct ip4_bucket_entry bkt_data[0];
};

/*
 * Ip4Mtrie
 *
 * Ip4Mtrie ensures that an IP lookup can be performed in 3 data fetches. It
 * organizes the lookup buckets in a (16 + 8 + 8) structure.
 */
/* To disable mtrie cache entirely set cache size to 0 */
#define MTRIE_CACHE_SIZE            (1 << 16)
/*
#define MTRIE_CACHE_SIZE            (1 << 16)
#define MTRIE_CACHE_SIZE            0
*/
#define MTRIE_JHASH(ip)             vr_hash_1word((ip), 1)
/* The perfect hash for a /16 network */
#define MTRIE_SHIFT_HASH(ip)        (((ip) >> 16) ^ (ip))
/* SSE4.2 required */
#define MTRIE_CRC32_HASH(ip)        (_mm_crc32_u32(1, (ip)))

#define MTRIE_HASH(ip)              MTRIE_JHASH(ip)
struct ip4_mtrie {
    struct ip4_bucket_entry root;
#if (MTRIE_CACHE_SIZE > 0)
    struct ip4_mtrie_cache_entry {
        uint32_t prefix;
        struct vr_nexthop *nexthop_p;
        unsigned int prefix_len:8;
        unsigned int label_flags:4;
        unsigned int label:20;
    } cache[MTRIE_CACHE_SIZE];
#endif
};

#define IP4_PREFIX_LEN              32

#define IP4BUCKET_LEVEL0            0
#define IP4BUCKET_LEVEL0_BITS       8
#define IP4BUCKET_LEVEL0_PFX_LEN    IP4BUCKET_LEVEL0_BITS
#define IP4BUCKET_LEVEL0_SHIFT      (IP4_PREFIX_LEN - IP4BUCKET_LEVEL0_BITS)
#define IP4BUCKET_LEVEL0_SIZE       (1 << IP4BUCKET_LEVEL0_BITS)
#define IP4BUCKET_LEVEL0_MASK       (IP4BUCKET_LEVEL0_SIZE - 1)

#define IP4BUCKET_LEVEL1            1
#define IP4BUCKET_LEVEL1_BITS       8
#define IP4BUCKET_LEVEL1_PFX_LEN    (IP4BUCKET_LEVEL0_PFX_LEN + \
                                        IP4BUCKET_LEVEL1_BITS)
#define IP4BUCKET_LEVEL1_SHIFT      (IP4BUCKET_LEVEL0_SHIFT - IP4BUCKET_LEVEL1_BITS)
#define IP4BUCKET_LEVEL1_SIZE       (1 << IP4BUCKET_LEVEL1_BITS)
#define IP4BUCKET_LEVEL1_MASK       (IP4BUCKET_LEVEL1_SIZE - 1)

#define IP4BUCKET_LEVEL2            2
#define IP4BUCKET_LEVEL2_BITS       8
#define IP4BUCKET_LEVEL2_PFX_LEN    (IP4BUCKET_LEVEL1_PFX_LEN + \
                                        IP4BUCKET_LEVEL2_BITS)
#define IP4BUCKET_LEVEL2_SHIFT      (IP4BUCKET_LEVEL1_SHIFT - IP4BUCKET_LEVEL2_BITS)
#define IP4BUCKET_LEVEL2_SIZE       (1 << IP4BUCKET_LEVEL2_BITS)
#define IP4BUCKET_LEVEL2_MASK       (IP4BUCKET_LEVEL2_SIZE - 1)

#define IP4BUCKET_LEVEL3            3
#define IP4BUCKET_LEVEL3_BITS       8
#define IP4BUCKET_LEVEL3_PFX_LEN    (IP4BUCKET_LEVEL2_PFX_LEN + \
                                        IP4BUCKET_LEVEL3_BITS)
#define IP4BUCKET_LEVEL3_SHIFT      (IP4BUCKET_LEVEL2_SHIFT - IP4BUCKET_LEVEL3_BITS)
#define IP4BUCKET_LEVEL3_SIZE       (1 << IP4BUCKET_LEVEL3_BITS)
#define IP4BUCKET_LEVEL3_MASK       (IP4BUCKET_LEVEL3_SIZE - 1)

struct mtrie_bkt_info {
    unsigned char           bi_bits;
    unsigned char           bi_shift;
    unsigned char           bi_pfx_len;
    unsigned int            bi_mask;
    unsigned int            bi_size;
};


#ifdef __cplusplus
}
#endif
#endif /* __VR_IP4_MTRIE_H__ */
