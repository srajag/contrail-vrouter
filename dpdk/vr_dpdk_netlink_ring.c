/*
 * Copyright (C) 2016 Semihalf.
 *
 * vr_dpdk_netlink_ring.c -- netlink over shared memory transport
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/genetlink.h>

#include "vr_dpdk.h"
#include "vr_dpdk_netlink_ring.h"
#include "vr_message.h"
#include "vr_genetlink.h"

#define HDR_LEN (NLMSG_HDRLEN + GENL_HDRLEN + sizeof(struct nlattr))

extern struct nlmsghdr *dpdk_nl_message_hdr(struct vr_message *);
extern unsigned int dpdk_nl_message_len(struct vr_message *);

static struct pollfd pollfds[VR_NL_RING_MAX_FDS];
static struct vr_nl_ring_buf* pollrings[VR_NL_RING_MAX_FDS];
static struct vr_nl_ring_buf* munmaprings[VR_NL_RING_MAX_FDS];


/**
 * opens a file, then mmaps file-backed shared memory.
 *
 * returns a file descriptor used with mmap and stores a pointer to
 * the mmaped memory in **shm.
 */
static int
vr_nl_shm_open(void **shm)
{
    static uint64_t fdcnt; /* counter for file names */
    int ret, file_fd;
    char name[VR_NL_RING_NAME_LEN];

    RTE_LOG(DEBUG, VROUTER,
            "%s: Creating new shared memory mapping for netlink transport\n",
            __func__);

    ret = snprintf(name, VR_NL_RING_NAME_LEN - 1, "%s%"PRIu64,
                    VR_NL_RING_FILE, fdcnt);
    if (ret >= sizeof(name)) {
        RTE_LOG(ERR, VROUTER,
                "\terror creating name for file %s%"PRIu64" (got %s)\n",
                VR_NL_RING_FILE, fdcnt, name);
        return -1;
    }

    mkdir(VR_NL_RING_DIR, VR_NL_RING_DIR_MODE);
    unlink(name);
    file_fd = open(name, O_RDWR | O_CREAT, VR_NL_RING_DIR_MODE);
    if (file_fd == -1) {
        RTE_LOG(ERR, VROUTER, "\terror opening file %s: %s (%d)\n",
                name, strerror(errno), errno);
        return -1;
    }

    if (ftruncate(file_fd, VR_NL_SHM_SZ) == -1) {
        RTE_LOG(ERR, VROUTER, "\terror truncating file %s to size %lu: %s (%d)\n",
                name, VR_NL_SHM_SZ, strerror(errno), errno);
        close(file_fd);
        return -1;
    }

    *shm = mmap(NULL, VR_NL_SHM_SZ, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_LOCKED, file_fd, 0);

    unlink(name);

    if (*shm == MAP_FAILED) {
        RTE_LOG(ERR, USOCK, "%s: mmaping shared memory failed: %s (%d)\n",
                __func__, strerror(errno), errno);
        close(file_fd);
        return -1;
    }

    fdcnt++;
    return file_fd;
}

/**
 * sends file descriptor fd to socket sock
 */
static int
vr_nl_fd_send(int sock, int fd)
{
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(fd))];
    struct iovec iov;

    RTE_LOG(DEBUG, VROUTER, "%s: Sending fd %d to socket %d\n",
            __func__, sock, fd);

    /* dummy data needed for sendmsg() to actually send anything */
    iov.iov_base = "";
    iov.iov_len = 1;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    msg.msg_controllen = cmsg->cmsg_len;

    if (sendmsg(sock, &msg, 0) < 0) {
        RTE_LOG(ERR, VROUTER, "Error sending fd %d to socket %d: %s (%d)\n",
                sock, fd, strerror(errno), errno);
        return -1;
    }

    return 0;
}

/* add file descriptor fd to poll list. return used index in array. */
static int
vr_nl_poll_fd_add(int fd)
{
    unsigned int i;

    for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
        if (pollfds[i].fd == -1) {
            pollfds[i].fd = fd;
            return i;
        }
    }

    RTE_LOG(ERR, VROUTER,
            "Error adding fd %d to polling list - no free space\n", fd);
    return -1;
}

/* close given fd and remove it from poll list. return its index in array. */
static int
vr_nl_poll_fd_del(int fd)
{
    unsigned int i;

    for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
        if (pollfds[i].fd == fd) {
            close(fd);
            pollfds[i].fd = -1;
            return i;
        }
    }

    RTE_LOG(ERR, VROUTER,
            "Error removing fd %d from polling list - descriptor not found\n",
            fd);
    return -1;
}

/* add ring pointer to the poll list with the given array index. */
static void
vr_nl_poll_ring_add(struct vr_nl_ring_buf *ring, int idx)
{
    if (!pollrings[idx]) {
        pollrings[idx] = ring;
    } else {
        /* TODO remove after tests - this shouldnt happen */
        RTE_LOG(ERR, VROUTER,
                "Error adding ring pointer to polling list -"
                "pointer at this index already exists. We did something wrong.\n");
        munmap(pollrings[idx], VR_NL_SHM_SZ);
        pollrings[idx] = ring;
    }
}

/* remove ring pointer from the poll lists with the given array index. */
static void
vr_nl_poll_ring_del(int idx)
{
    unsigned i;

    if (pollrings[idx]) {
        /**
         * schedule memory unmpap.
         * it is possible that a process is using a ring
         * at the moment, so just remove pointer from pollrings,
         * to prevent another read.
         * memory will be unmapped in a separate routine
         */
        for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
            if (!munmaprings[i])
                munmaprings[i] = pollrings[idx];
        }
        pollrings[idx] = NULL;
    } else {
        /* TODO remove after tests - this shouldnt happen */

        RTE_LOG(ERR, VROUTER,
                "Error removing shared mem pointer from polling list"
                " - pointer not found\n");
    }
}

static void
vr_nl_ring_init(void *ring)
{
    struct vr_nl_ring_buf *r = ring;

    r->head = r->tail = 0;
}

/* add accepted socket to the poll list and opens shared memory for the client */
static int
vr_nl_client_add(int cl_fd)
{
    int rx_ring_fd;
    void *rx_ring;
    unsigned int fd_idx;

    RTE_LOG(DEBUG, VROUTER, "%s: Accepted new netlink client fd %d\n",
            __func__, cl_fd);

    /* add fd to poll list to check if connection ends */
    fd_idx = vr_nl_poll_fd_add(cl_fd);
    if (fd_idx < 0) {
        close(cl_fd);
        return -1;
    }

    /* open shared memory */
    rx_ring_fd = vr_nl_shm_open(&rx_ring);
    if (rx_ring_fd < 0) {
        vr_nl_poll_fd_del(cl_fd);
        return -1;
    }

    /* add ring to poll list to check for incoming messages */
    vr_nl_poll_ring_add(rx_ring, fd_idx);

    /* initialize internal ring strucutres for both TX and RX rings */
    vr_nl_ring_init(rx_ring);
    vr_nl_ring_init(VR_NL_RING_NEXT(rx_ring));

    /* send shared memory file descriptor to the client */
    if (vr_nl_fd_send(cl_fd, rx_ring_fd) < 0) {
        /* remove fd and shared mem from poll lists and clean up */
        fd_idx = vr_nl_poll_fd_del(cl_fd);
        vr_nl_poll_ring_del(fd_idx);
        return -1;
    }
    close(rx_ring_fd);

    /**
     * at this point client can mmap the same memory and put messages there.
     * when message is put into the memory, it will be received by vrouter.
     *
     * when client ends the connection, vrouter will close the socket and
     * munmap memory.
     */

    return 0;
}

static int
vr_nl_client_del(int fd)
{
    int fd_idx;

    fd_idx = vr_nl_poll_fd_del(fd);
    if (fd_idx < 0)
        return -1;

    vr_nl_poll_ring_del(fd_idx);

    return 0;
}

static void
vr_nl_poll_init(void)
{
    unsigned int i;

    for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
        pollrings[i] = NULL;
        munmaprings[i] = NULL;
        pollfds[i].fd = -1;
        pollfds[i].events = POLLIN; /* .events has to be non-zero */
    }
}

static void
vr_nl_poll_deinit(void)
{
    unsigned int i;

    for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
        if (pollrings[i] != NULL) {
            munmap(pollrings[i], VR_NL_SHM_SZ);
            munmap(munmaprings[i], VR_NL_SHM_SZ);
            munmaprings[i] = pollrings[i] = NULL;
        }
        if (pollfds[i].fd != -1) {
            close(pollfds[i].fd);
            pollfds[i].fd = -1;
        }
    }
}

int
vr_dpdk_netlink_sock_init(void)
{
    int fd, ret;
    struct sockaddr_un sun;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        goto error;

    ret = fcntl(fd, F_GETFL);
    if (ret < 0)
        goto error;

    ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
    if (ret < 0)
        goto error;

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, VR_NL_SOCKET_FILE, sizeof(sun.sun_path) - 1);

    mkdir(VR_NL_RING_DIR, VR_NL_RING_DIR_MODE);
    unlink(sun.sun_path);
    ret = bind(fd, (struct sockaddr *) &sun, sizeof(sun));
    if (ret == -1)
        goto error;

    if (listen(fd, 1) == -1)
        goto error;

    return fd;

error:
    RTE_LOG(ERR, VROUTER, "Error creating netlink server socket: %s (%d)\n",
            strerror(errno), errno);
    if (fd > 0)
        close(fd);
    unlink(sun.sun_path);
    return -1;
}

static void
vr_nl_ring_process_response(struct vr_nl_ring_buf *tx_ring, struct nlmsghdr *nlh)
{
    __u32 seq;
    unsigned int multi_flag = 0;
    bool write = true;

    struct vr_message *resp;

    struct nlmsghdr *resp_nlh;
    struct genlmsghdr *genlh, *resp_genlh;
    struct nlattr *resp_nla;

    seq = nlh->nlmsg_seq;
    genlh = (struct genlmsghdr *)((unsigned char *)nlh + NLMSG_HDRLEN);

    /* Process responses */
    while ((resp = (struct vr_message *)vr_message_dequeue_response())) {
        if (!write) {
            vr_message_free(resp);
            continue;
        }

        if (!vr_response_queue_empty()) {
            multi_flag = NLM_F_MULTI;
        } else {
            multi_flag = 0;
        }

        /* Update Netlink headers */
        resp_nlh = dpdk_nl_message_hdr(resp);
        resp_nlh->nlmsg_len = dpdk_nl_message_len(resp);
        resp_nlh->nlmsg_type = nlh->nlmsg_type;
        resp_nlh->nlmsg_flags = multi_flag;
        resp_nlh->nlmsg_seq = seq;
        resp_nlh->nlmsg_pid = 0;

        resp_genlh = (struct genlmsghdr *)((unsigned char *)resp_nlh +
                NLMSG_HDRLEN);
        memcpy(resp_genlh, genlh, sizeof(*genlh));

        resp_nla = (struct nlattr *)((unsigned char *)resp_genlh + GENL_HDRLEN);
        resp_nla->nla_len = resp->vr_message_len;
        resp_nla->nla_type = NL_ATTR_VR_MESSAGE_PROTOCOL;

        if (vr_nl_ring_enq(tx_ring, resp_nlh, resp_nlh->nlmsg_len) < 0) {
            RTE_LOG(ERR, VROUTER, "ENQ ENOMEM len %"PRIu32" tail %u\n",
                resp_nlh->nlmsg_len, tx_ring->tail);
            write = false;
        }
    }

    return;
}

static int
vr_nl_ring_receive(struct vr_nl_ring_buf *tx_ring,
                    char *nl_buf, unsigned int nl_len)
{
    int ret;
    struct vr_message request;

    memset(&request, 0, sizeof(request));
    request.vr_message_buf = nl_buf + HDR_LEN;
    request.vr_message_len = nl_len - HDR_LEN;

    ret = vr_message_request(&request);
    if (ret < 0)
        vr_send_response(ret);

    vr_nl_ring_process_response(tx_ring, (struct nlmsghdr *)nl_buf);

    return 0;
}

static void *
vr_netlink_poll_loop(void *args)
{
    int cl_fd, nl_sock_fd = *(int *)args;
    unsigned int i;

    while (1) {
        /* get new connection */
        cl_fd = accept(nl_sock_fd, NULL, NULL);
        if (cl_fd > 0) {
            /* incoming client connection */
            if (vr_nl_client_add(cl_fd) < 0) {
                RTE_LOG(ERR, VROUTER, "Error, adding netlink ring client\n");
                /**
                 * no harm, vr_nl_client_add() cleaned up.
                 * proceed to polling exising rings and sockets.
                 */
            }
        }

        /* poll on sockets waiting for connections to close */
        rcu_thread_offline();
        if (poll(pollfds, VR_NL_RING_MAX_FDS, VR_NL_POLL_FREQ_MS) < 0) {
            RTE_LOG(ERR, VROUTER, "Error polling fds: %s (%d)\n",
                    strerror(errno), errno);
        }
        rcu_thread_online();

        /* handle sockets events */
        for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
            if (pollfds[i].revents & POLLHUP)
                vr_nl_client_del(pollfds[i].fd);
        }
    }

    return NULL;
}

int
vr_netlink_ring_loop(int nl_sock_fd)
{
    int ret = 0;
    unsigned int i = 0;
    struct vr_nl_ring_buf *tx_ring, *rx_ring;
    int msg_len;
    void *msg;
    pthread_t th;

    vr_nl_poll_init();

    /* add netlink sock to poll list to make poll() return on new connection */
    vr_nl_poll_fd_add(nl_sock_fd);

    /* separate thread for handling connections */
    pthread_create(&th, NULL, &vr_netlink_poll_loop, &nl_sock_fd);

    while (1) {
        if (vr_dpdk_is_stop_flag_set())
            break;

        for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
            rx_ring = pollrings[i];
            while (rx_ring) {
                /* get pointer to the message */
                msg_len = vr_nl_ring_deq_ptr(rx_ring, &msg);
                if (msg_len < 0)
                    break;

                tx_ring = VR_NL_RING_NEXT(rx_ring);
                vr_nl_ring_receive(tx_ring, msg, msg_len);

                /* message was processed, so move offsets in ring */
                vr_nl_ring_deq_finish(rx_ring);
            }
        }

        /* munmap unused rings */
        for (i = 0; i < VR_NL_RING_MAX_FDS; i++) {
            if (munmaprings[i]) {
                munmap(munmaprings[i], VR_NL_SHM_SZ);
                munmaprings[i] = NULL;
            }
        }
    }

    RTE_LOG(DEBUG, VROUTER, "Exit netlink ring loop\n");
    pthread_cancel(th);
    close(nl_sock_fd);
    vr_nl_poll_deinit();

    return ret;
}
