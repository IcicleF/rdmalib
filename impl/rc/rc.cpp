#include <cstdio>
#include <cstdlib>

#include "rc.h"

namespace rdma {

ReliableConnection::ReliableConnection(Peer &peer, int id) {
    this->ctx = peer.ctx;
    this->ctx->refcnt.fetch_add(1);
    this->cluster = peer.cluster;

    this->peer = &peer;
    this->id = id;

    // Create QP
    this->create_cq(&this->send_cq);
    this->create_cq(&this->recv_cq);
    this->create_qp();
}

ReliableConnection::~ReliableConnection() {
    ibv_destroy_qp(this->qp);
    ibv_destroy_cq(this->send_cq);
    ibv_destroy_cq(this->recv_cq);

    // Dereference the RDMA context
    this->ctx->refcnt.fetch_sub(1);
}

int ReliableConnection::post_read(void *dst, uintptr_t src, size_t size, bool signaled, int wr_id) {
    ibv_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(dst);
    sge.length = size;
    sge.lkey = this->ctx->match_mr_lkey(dst, size);

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    if (signaled)
        wr.send_flags |= IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = src;
    wr.wr.rdma.rkey = this->peer->match_remote_mr_rkey(src, size);
    return ibv_post_send(this->qp, &wr, &bad_wr);
}

int ReliableConnection::post_write(uintptr_t dst, void const *src, size_t size, bool signaled,
                                   int wr_id) {
    ibv_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(src);
    sge.length = size;
    sge.lkey = this->ctx->match_mr_lkey(src, size);

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    if (signaled)
        wr.send_flags |= IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = dst;
    wr.wr.rdma.rkey = this->peer->match_remote_mr_rkey(dst, size);
    return ibv_post_send(this->qp, &wr, &bad_wr);
}

int ReliableConnection::post_send(void const *src, size_t size, bool signaled, int wr_id) {
    ibv_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(src);
    sge.length = size;
    sge.lkey = this->ctx->match_mr_lkey(src, size);

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    if (signaled)
        wr.send_flags = IBV_SEND_SIGNALED;
    return ibv_post_send(this->qp, &wr, &bad_wr);
}

int ReliableConnection::post_recv(void *dst, size_t size, int wr_id) {
    ibv_recv_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(dst);
    sge.length = size;
    sge.lkey = this->ctx->match_mr_lkey(dst, size);

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    return ibv_post_recv(this->qp, &wr, &bad_wr);
}

int ReliableConnection::post_atomic_cas(uintptr_t dst, void *expected, uint64_t desire,
                                        bool signaled, int wr_id) {
    if (__glibc_unlikely((dst & 0x7) != 0))
        throw std::runtime_error("post atomic CAS to non-aligned address");

    ibv_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(expected);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(expected, sizeof(uint64_t));

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    if (signaled)
        wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.atomic.remote_addr = dst;
    wr.wr.atomic.rkey = this->peer->match_remote_mr_rkey(dst, sizeof(uint64_t));
    wr.wr.atomic.compare_add = *(reinterpret_cast<uint64_t *>(expected));
    wr.wr.atomic.swap = desire;
    return ibv_post_send(this->qp, &wr, &bad_wr);
}

int ReliableConnection::post_atomic_fa(uintptr_t dst, void *before, uint64_t delta, bool signaled,
                                       int wr_id) {
    if (__glibc_unlikely((dst & 0x7) != 0))
        throw std::runtime_error("post atomic FA to non-aligned address");

    ibv_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(before);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(before, sizeof(uint64_t));

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
    if (signaled)
        wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.atomic.remote_addr = dst;
    wr.wr.atomic.rkey = this->peer->match_remote_mr_rkey(dst, sizeof(uint64_t));
    wr.wr.atomic.compare_add = delta;
    return ibv_post_send(this->qp, &wr, &bad_wr);
}

int ReliableConnection::post_masked_atomic_cas(uintptr_t dst, void *expected,
                                               uint64_t expected_mask, uint64_t desire,
                                               uint64_t desire_mask, bool signaled, int wr_id) {
    if (__glibc_unlikely((dst & 0x7) != 0))
        throw std::runtime_error("post masked atomic FA to non-aligned address");

    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(expected);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(expected, sizeof(uint64_t));

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.exp_opcode = IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP;
    wr.exp_send_flags = IBV_EXP_SEND_EXT_ATOMIC_INLINE;
    if (signaled)
        wr.exp_send_flags |= IBV_EXP_SEND_SIGNALED;

    wr.ext_op.masked_atomics.log_arg_sz = 3;  // log(sizeof(uint64_t))
    wr.ext_op.masked_atomics.remote_addr = dst;
    wr.ext_op.masked_atomics.rkey = this->peer->match_remote_mr_rkey(dst, sizeof(uint64_t));
    wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap.compare_val =
        *(reinterpret_cast<uint64_t *>(expected));
    wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap.compare_mask = expected_mask;
    wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap.swap_val = desire;
    wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap.swap_mask = desire_mask;

    return ibv_exp_post_send(this->qp, &wr, &bad_wr);
}

int ReliableConnection::post_masked_atomic_fa(uintptr_t dst, void *before, uint64_t delta,
                                              int highest_bit, int lowest_bit, bool signaled,
                                              int wr_id) {
    if (__glibc_unlikely((dst & 0x7) != 0))
        throw std::runtime_error("post masked atomic FA to non-aligned address");

    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(before);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(before, sizeof(uint64_t));

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.exp_opcode = IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD;
    wr.exp_send_flags = IBV_EXP_SEND_EXT_ATOMIC_INLINE;
    if (signaled)
        wr.exp_send_flags |= IBV_EXP_SEND_SIGNALED;

    wr.ext_op.masked_atomics.log_arg_sz = 3;  // log(sizeof(uint64_t))
    wr.ext_op.masked_atomics.remote_addr = dst;
    wr.ext_op.masked_atomics.rkey = this->peer->match_remote_mr_rkey(dst, sizeof(uint64_t));
    wr.ext_op.masked_atomics.wr_data.inline_data.op.fetch_add.add_val = delta << lowest_bit;
    wr.ext_op.masked_atomics.wr_data.inline_data.op.fetch_add.field_boundary = 1ull << highest_bit;

    return ibv_exp_post_send(this->qp, &wr, &bad_wr);
}

int ReliableConnection::poll_send_cq(int n) {
    ibv_wc wc_arr[32];

    for (int i = 0; i < n; i += 32) {
        int m = n - i, res = 0;
        if (m > 32)
            m = 32;
        while (m > res) {
            res += ibv_poll_cq(this->send_cq, m - res, wc_arr + res);
        }
        for (int j = 0; j < m; ++j)
            if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
                throw std::runtime_error("wc failure: " + std::to_string(wc_arr[j].status));
    }
    return n;
}

int ReliableConnection::poll_send_cq(ibv_wc *wc_arr, int n) {
    int res = 0;
    while (n > res) {
        res += ibv_poll_cq(this->send_cq, n - res, wc_arr + res);
    }
    for (int j = 0; j < n; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            throw std::runtime_error("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ReliableConnection::poll_send_cq_once(ibv_wc *wc_arr, int n) {
    int res = ibv_poll_cq(this->send_cq, n, wc_arr);
    for (int j = 0; j < res; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            throw std::runtime_error("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ReliableConnection::poll_recv_cq(int n) {
    ibv_wc wc_arr[32];

    for (int i = 0; i < n; i += 32) {
        int m = n - i, res = 0;
        if (m > 32)
            m = 32;
        while (m > res) {
            res += ibv_poll_cq(this->recv_cq, m - res, wc_arr + res);
        }
        for (int j = 0; j < m; ++j)
            if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
                throw std::runtime_error("wc failure: " + std::to_string(wc_arr[j].status));
    }
    return n;
}

int ReliableConnection::poll_recv_cq(ibv_wc *wc_arr, int n) {
    int res = 0;
    while (n > res) {
        res += ibv_poll_cq(this->recv_cq, n - res, wc_arr + res);
    }
    for (int j = 0; j < n; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            throw std::runtime_error("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ReliableConnection::poll_recv_cq_once(ibv_wc *wc_arr, int n) {
    int res = ibv_poll_cq(this->recv_cq, n, wc_arr);
    for (int j = 0; j < res; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            throw std::runtime_error("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ReliableConnection::verbose() const {
    static char const *qpt_str[] = {"?type", "?type", "RC", "UC", "UD", "XRC"};
    static char const *stat_str[] = {"reset", "init", "rtr",   "rts ok!",
                                     "sqd",   "sqe",  "error", "?state"};

    fprintf(stderr, "  [node %d, peer %d] conn %d: ", this->cluster->whoami(), this->peer->id,
            this->id);

    int rc;
    ibv_qp_init_attr init_attr;
    ibv_qp_attr attr;
    memset(&init_attr, 0, sizeof(ibv_qp_init_attr));
    memset(&attr, 0, sizeof(ibv_qp_attr));

    rc = ibv_query_qp(this->qp, &attr, IBV_QP_STATE, &init_attr);
    if (rc)
        throw std::runtime_error("failed to perform ibv_query_qp");
    fprintf(stderr, "%s %s\n", qpt_str[this->qp->qp_type], stat_str[attr.qp_state]);
    if (attr.qp_state != IBV_QPS_RTS)
        return -1;
    return 0;
}

int ReliableConnection::create_cq(ibv_cq **cq, int cq_depth) {
    *cq = ibv_create_cq(this->ctx->ctx, cq_depth, nullptr, nullptr, 0);
    return errno;
}

int ReliableConnection::create_qp(ibv_qp_type qp_type, int qp_depth) {
    ibv_exp_qp_init_attr init_attr;
    memset(&init_attr, 0, sizeof(ibv_exp_qp_init_attr));

    init_attr.qp_type = qp_type;
    init_attr.sq_sig_all = 0;
    init_attr.send_cq = this->send_cq;
    init_attr.recv_cq = this->recv_cq;
    init_attr.pd = this->ctx->pd;

    if (qp_type == IBV_QPT_RC) {
        init_attr.comp_mask = IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS | IBV_EXP_QP_INIT_ATTR_PD |
                              IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG;
        init_attr.exp_create_flags = IBV_EXP_QP_CREATE_EC_PARITY_EN;  // Enable EC
        init_attr.max_atomic_arg = sizeof(uint64_t);                  // Enable extended atomics
    } else {
        init_attr.comp_mask = IBV_EXP_QP_INIT_ATTR_PD;
    }

    init_attr.cap.max_send_wr = qp_depth;
    init_attr.cap.max_recv_wr = qp_depth;
    init_attr.cap.max_send_sge = 16;
    init_attr.cap.max_recv_sge = 16;

    this->qp = ibv_exp_create_qp(this->ctx->ctx, &init_attr);
    return errno;
}

void ReliableConnection::fill_exchange(OOBExchange *xchg) {
    if (this->qp == nullptr)
        throw std::runtime_error("filling OOBExchange with null QP");
    xchg->qpn[this->id] = this->qp->qp_num;
}

void ReliableConnection::establish(uint8_t *gid, int lid, uint32_t qpn) {
    this->modify_to_init();
    this->modify_to_rtr(gid, lid, qpn);
    this->modify_to_rts();
}

void ReliableConnection::modify_to_init() {
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = 1;
    attr.pkey_index = 0;

    switch (qp->qp_type) {
    case IBV_QPT_RC:
        attr.qp_access_flags =
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
        break;
    case IBV_QPT_UC:
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
        break;
    case IBV_EXP_QPT_DC_INI:
        throw std::runtime_error("QP type DC_INI not implemented");
    default:
        throw std::runtime_error("unrecognized QP type " + std::to_string(qp->qp_type));
    }

    if (ibv_modify_qp(qp, &attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
        throw std::runtime_error("failed to modify QP to init");
}

void ReliableConnection::modify_to_rtr(uint8_t *gid, int lid, uint32_t qpn) {
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = qpn;
    attr.rq_psn = InitPSN;

    attr.ah_attr.dlid = lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = 1;
    attr.ah_attr.is_global = 1;
    memcpy(&attr.ah_attr.grh.dgid, gid, sizeof(ibv_gid));
    attr.ah_attr.grh.flow_label = 0;
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.sgid_index = 1;
    attr.ah_attr.grh.traffic_class = 0;

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN;

    if (qp->qp_type == IBV_QPT_RC) {
        attr.max_dest_rd_atomic = 16;
        attr.min_rnr_timer = 12;
        flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    }

    if (ibv_modify_qp(qp, &attr, flags))
        throw std::runtime_error("failed to modify QP to RTR");
}

void ReliableConnection::modify_to_rts() {
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = InitPSN;
    int flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

    if (qp->qp_type == IBV_QPT_RC) {
        attr.timeout = 14;
        attr.retry_cnt = 7;
        attr.rnr_retry = 7;
        attr.max_rd_atomic = 16;
        flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;
    }

    if (ibv_modify_qp(qp, &attr, flags))
        throw std::runtime_error("failed to modify QP to RTS");
}

}  // namespace rdma
