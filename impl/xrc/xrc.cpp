#include <cstdio>
#include <cstdlib>

#include "xrc.h"

namespace rdma {

ExtendedReliableConnection::ExtendedReliableConnection(Peer &peer, int id)
{
    this->ctx = peer.ctx;
    this->ctx->refcnt.fetch_add(1);
    this->cluster = peer.cluster;

    this->peer = &peer;
    this->id = id;

    // Create QP
    this->create_cq(&this->send_cq);
    this->create_cq(&this->recv_cq);
    this->create_cq(&this->placeholder_cq, 4);

    this->create_srq(&this->srq, this->recv_cq);

    this->create_qp(&this->ini_qp, IBV_QPT_XRC, send_cq, placeholder_cq);
    this->create_qp(&this->tgt_qp, IBV_QPT_XRC_RECV, placeholder_cq, recv_cq);
}

ExtendedReliableConnection::~ExtendedReliableConnection()
{
    ibv_destroy_qp(this->ini_qp);
    ibv_destroy_qp(this->tgt_qp);
    ibv_destroy_srq(this->srq);
    ibv_destroy_cq(this->send_cq);
    ibv_destroy_cq(this->recv_cq);
    ibv_destroy_cq(this->placeholder_cq);

    // Dereference the RDMA context
    this->ctx->refcnt.fetch_sub(1);
}

int ExtendedReliableConnection::post_read(void *dst, uintptr_t src, size_t size, bool signaled,
                                          int wr_id)
{
    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(dst);
    sge.length = size;
    sge.lkey = this->ctx->match_mr_lkey(dst, size);

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.exp_opcode = IBV_EXP_WR_RDMA_READ;
    if (signaled)
        wr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = src;
    wr.wr.rdma.rkey = this->peer->match_remote_mr_rkey(src, size);
    wr.xrc_remote_srq_num = this->peer->xrc_srq_nums[this->id];  // Must specify one

    return ibv_exp_post_send(this->ini_qp, &wr, &bad_wr);
}

int ExtendedReliableConnection::post_write(uintptr_t dst, void const *src, size_t size,
                                           bool signaled, int wr_id)
{
    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(src);
    sge.length = size;
    sge.lkey = this->ctx->match_mr_lkey(src, size);

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
    if (signaled)
        wr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = dst;
    wr.wr.rdma.rkey = this->peer->match_remote_mr_rkey(dst, size);
    wr.xrc_remote_srq_num = this->peer->xrc_srq_nums[this->id];  // Must specify one

    return ibv_exp_post_send(this->ini_qp, &wr, &bad_wr);
}

int ExtendedReliableConnection::post_send(void const *src, size_t size, int remote_id,
                                          bool signaled, int wr_id)
{
    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(src);
    sge.length = size;
    sge.lkey = this->ctx->match_mr_lkey(src, size);

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.exp_opcode = IBV_EXP_WR_SEND;
    if (signaled)
        wr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
    wr.xrc_remote_srq_num = this->peer->xrc_srq_nums[remote_id];

    return ibv_exp_post_send(this->ini_qp, &wr, &bad_wr);
}

int ExtendedReliableConnection::post_recv(void *dst, size_t size, int wr_id)
{
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

    return ibv_post_srq_recv(this->srq, &wr, &bad_wr);
}

int ExtendedReliableConnection::post_atomic_cas(uintptr_t dst, void *compare, uint64_t swap,
                                                bool signaled, int wr_id)
{
    if (__glibc_unlikely((dst & 0x7) != 0))
        Emergency::abort("post atomic CAS to non-aligned address");

    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(compare);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(compare, sizeof(uint64_t));

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.exp_opcode = IBV_EXP_WR_ATOMIC_CMP_AND_SWP;
    if (signaled)
        wr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
    wr.wr.atomic.remote_addr = dst;
    wr.wr.atomic.rkey = this->peer->match_remote_mr_rkey(dst, sizeof(uint64_t));
    wr.wr.atomic.compare_add = *(reinterpret_cast<uint64_t *>(compare));
    wr.wr.atomic.swap = swap;
    wr.xrc_remote_srq_num = this->peer->xrc_srq_nums[this->id];  // Must specify one

    return ibv_exp_post_send(this->ini_qp, &wr, &bad_wr);
}

int ExtendedReliableConnection::post_atomic_fa(uintptr_t dst, void *fetch, uint64_t add,
                                               bool signaled, int wr_id)
{
    if (__glibc_unlikely((dst & 0x7) != 0))
        Emergency::abort("post atomic FA to non-aligned address");

    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(fetch);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(fetch, sizeof(uint64_t));

    memset(&wr, 0, sizeof(wr));
    wr.next = nullptr;
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.exp_opcode = IBV_EXP_WR_ATOMIC_FETCH_AND_ADD;
    if (signaled)
        wr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
    wr.wr.atomic.remote_addr = dst;
    wr.wr.atomic.rkey = this->peer->match_remote_mr_rkey(dst, sizeof(uint64_t));
    wr.wr.atomic.compare_add = add;
    wr.xrc_remote_srq_num = this->peer->xrc_srq_nums[this->id];  // Must specify one

    return ibv_exp_post_send(this->ini_qp, &wr, &bad_wr);
}

int ExtendedReliableConnection::post_masked_atomic_cas(uintptr_t dst, void *compare,
                                                       uint64_t compare_mask, uint64_t swap,
                                                       uint64_t swap_mask, bool signaled, int wr_id)
{
    if (__glibc_unlikely((dst & 0x7) != 0))
        Emergency::abort("post masked atomic FA to non-aligned address");

    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(compare);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(compare, sizeof(uint64_t));

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
        *(reinterpret_cast<uint64_t *>(compare));
    wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap.compare_mask = compare_mask;
    wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap.swap_val = swap;
    wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap.swap_mask = swap_mask;
    wr.xrc_remote_srq_num = this->peer->xrc_srq_nums[this->id];  // Must specify one

    return ibv_exp_post_send(this->ini_qp, &wr, &bad_wr);
}

int ExtendedReliableConnection::post_masked_atomic_fa(uintptr_t dst, void *fetch, uint64_t add,
                                                      int highest_bit, int lowest_bit,
                                                      bool signaled, int wr_id)
{
    if (__glibc_unlikely((dst & 0x7) != 0))
        Emergency::abort("post masked atomic FA to non-aligned address");

    ibv_exp_send_wr wr, *bad_wr;
    ibv_sge sge;
    sge.addr = reinterpret_cast<uintptr_t>(fetch);
    sge.length = sizeof(uint64_t);
    sge.lkey = this->ctx->match_mr_lkey(fetch, sizeof(uint64_t));

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
    wr.ext_op.masked_atomics.wr_data.inline_data.op.fetch_add.add_val = add << lowest_bit;
    wr.ext_op.masked_atomics.wr_data.inline_data.op.fetch_add.field_boundary = 1ull << highest_bit;
    wr.xrc_remote_srq_num = this->peer->xrc_srq_nums[this->id];  // Must specify one

    return ibv_exp_post_send(this->ini_qp, &wr, &bad_wr);
}

int ExtendedReliableConnection::poll_send_cq(int n)
{
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
                Emergency::abort("wc failure: " + std::to_string(wc_arr[j].status));
    }
    return n;
}

int ExtendedReliableConnection::poll_send_cq(ibv_wc *wc_arr, int n)
{
    int res = 0;
    while (n > res) {
        res += ibv_poll_cq(this->send_cq, n - res, wc_arr + res);
    }
    for (int j = 0; j < n; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            Emergency::abort("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ExtendedReliableConnection::poll_send_cq_once(ibv_wc *wc_arr, int n)
{
    int res = ibv_poll_cq(this->send_cq, n, wc_arr);
    for (int j = 0; j < res; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            Emergency::abort("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ExtendedReliableConnection::poll_recv_cq(int n)
{
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
                Emergency::abort("wc failure: " + std::to_string(wc_arr[j].status));
    }
    return n;
}

int ExtendedReliableConnection::poll_recv_cq(ibv_wc *wc_arr, int n)
{
    int res = 0;
    while (n > res)
        res += ibv_poll_cq(this->recv_cq, n - res, wc_arr + res);
    for (int j = 0; j < n; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            Emergency::abort("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ExtendedReliableConnection::poll_recv_cq_once(ibv_wc *wc_arr, int n)
{
    int res = ibv_poll_cq(this->recv_cq, n, wc_arr);
    for (int j = 0; j < res; ++j)
        if (__glibc_unlikely(wc_arr[j].status != IBV_WC_SUCCESS))
            Emergency::abort("wc failure: " + std::to_string(wc_arr[j].status));
    return res;
}

int ExtendedReliableConnection::verbose() const
{
    static char const *stat_str[] = {"reset", "init", "rtr",   "rts ok",
                                     "sqd",   "sqe",  "error", "?state"};

    fprintf(stderr, "  [node %d, peer %d] xrc %d: ", this->cluster->whoami(), this->peer->id,
            this->id);

    int rc;
    ibv_qp_state ini_state, tgt_state;
    ibv_qp_init_attr init_attr;
    ibv_qp_attr attr;

    memset(&init_attr, 0, sizeof(ibv_qp_init_attr));
    memset(&attr, 0, sizeof(ibv_qp_attr));
    rc = ibv_query_qp(this->ini_qp, &attr, IBV_QP_STATE, &init_attr);
    if (rc)
        Emergency::abort("failed to perform ibv_query_qp");
    ini_state = attr.qp_state;

    memset(&init_attr, 0, sizeof(ibv_qp_init_attr));
    memset(&attr, 0, sizeof(ibv_qp_attr));
    rc = ibv_query_qp(this->tgt_qp, &attr, IBV_QP_STATE, &init_attr);
    if (rc)
        Emergency::abort("failed to perform ibv_query_qp");
    tgt_state = attr.qp_state;

    fprintf(stderr, "ini %s, tgt %s\n", stat_str[ini_state], stat_str[tgt_state]);
    if (ini_state != IBV_QPS_RTS || tgt_state != IBV_QPS_RTS)
        return -1;
    return 0;
}

int ExtendedReliableConnection::create_cq(ibv_cq **cq, int cq_depth)
{
    *cq = ibv_create_cq(this->ctx->ctx, cq_depth, nullptr, nullptr, 0);
    return errno;
}

int ExtendedReliableConnection::create_srq(ibv_srq **srq, ibv_cq *cq, int srq_depth)
{
    ibv_exp_create_srq_attr srq_init_attr;
    memset(&srq_init_attr, 0, sizeof(ibv_exp_create_srq_attr));
    srq_init_attr.pd = this->ctx->pd;
    srq_init_attr.xrcd = this->ctx->xrcd;
    srq_init_attr.cq = cq;
    srq_init_attr.srq_type = IBV_EXP_SRQT_XRC;
    srq_init_attr.base.attr.max_wr = srq_depth;
    srq_init_attr.base.attr.max_sge = 16;
    /**
     * `ibv_srq_attr::srq_limit`
     *
     * The value that the SRQ will be armed with. When the number of outstanding WRs in the SRQ
     * drops below this limit, the affiliated asynchronous event IBV_EVENT_SRQ_LIMIT_REACHED will be
     * generated. Value can be [0..number of WR that can be posted to the SRQ]. 0 means that the SRQ
     * limit event won’t be generated (since the number of outstanding WRs in the SRQ can’t be
     * negative).
     */
    srq_init_attr.base.attr.srq_limit = 0;
    srq_init_attr.comp_mask = IBV_EXP_CREATE_SRQ_CQ | IBV_EXP_CREATE_SRQ_XRCD;
    *srq = ibv_exp_create_srq(this->ctx->ctx, &srq_init_attr);
    return errno;
}

int ExtendedReliableConnection::create_qp(ibv_qp **qp, ibv_qp_type type, ibv_cq *send_cq,
                                          ibv_cq *recv_cq, int qp_depth)
{
    ibv_exp_qp_init_attr init_attr;

    memset(&init_attr, 0, sizeof(ibv_exp_qp_init_attr));
    init_attr.qp_type = type;
    init_attr.sq_sig_all = 0;
    init_attr.send_cq = send_cq;
    init_attr.recv_cq = recv_cq;
    init_attr.pd = this->ctx->pd;
    init_attr.comp_mask = IBV_EXP_QP_INIT_ATTR_PD | IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG;
    init_attr.max_atomic_arg = sizeof(uint64_t);  // Enable extended atomics

    if (type == IBV_QPT_XRC_RECV) {
        init_attr.xrcd = this->ctx->xrcd;
        init_attr.srq = this->srq;
        init_attr.comp_mask |= IBV_EXP_QP_INIT_ATTR_XRCD;
    }

    init_attr.cap.max_send_wr = qp_depth;
    init_attr.cap.max_recv_wr = qp_depth;
    init_attr.cap.max_send_sge = 16;
    init_attr.cap.max_recv_sge = 16;

    *qp = ibv_exp_create_qp(this->ctx->ctx, &init_attr);
    return errno;
}

void ExtendedReliableConnection::fill_exchange(OOBExchange *xchg)
{
    xchg->xrc_ini_qp_num[this->id] = this->ini_qp->qp_num;
    xchg->xrc_tgt_qp_num[this->id] = this->tgt_qp->qp_num;

    uint32_t srq_num;
    ibv_get_srq_num(this->srq, &srq_num);
    xchg->xrc_srq_num[this->id] = srq_num;
}

void ExtendedReliableConnection::establish(ibv_gid gid, int lid, uint32_t ini_qp_num,
                                           uint32_t tgt_qp_num)
{
    this->modify_to_init();
    this->modify_to_rtr(this->ini_qp, gid, lid, tgt_qp_num);
    this->modify_to_rtr(this->tgt_qp, gid, lid, ini_qp_num);
    this->modify_to_rts();
}

void ExtendedReliableConnection::modify_to_init()
{
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = 1;
    attr.pkey_index = 0;
    attr.qp_access_flags =
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;

    if (ibv_modify_qp(ini_qp, &attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
        Emergency::abort("modify qp failed reset -> init");

    if (ibv_modify_qp(tgt_qp, &attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
        Emergency::abort("modify qp failed reset -> init");
}

void ExtendedReliableConnection::modify_to_rtr(ibv_qp *qp, ibv_gid gid, int lid, uint32_t qp_num)
{
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = qp_num;
    attr.rq_psn = InitPSN;

    attr.ah_attr.dlid = lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = 1;
    attr.ah_attr.is_global = 1;
    memcpy(&attr.ah_attr.grh.dgid, &gid, sizeof(ibv_gid));
    attr.ah_attr.grh.flow_label = 0;
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.sgid_index = 1;
    attr.ah_attr.grh.traffic_class = 0;

    attr.max_dest_rd_atomic = 16;
    attr.min_rnr_timer = 12;

    if (ibv_modify_qp(qp, &attr,
                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                          IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER))
        Emergency::abort("modify qp failed init -> rtr");
}

void ExtendedReliableConnection::modify_to_rts()
{
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = InitPSN;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.max_rd_atomic = 16;

    if (ibv_modify_qp(ini_qp, &attr,
                      IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                          IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC))
        Emergency::abort("modify qp failed rtr -> rts");

    if (ibv_modify_qp(tgt_qp, &attr,
                      IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                          IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC))
        Emergency::abort("modify qp failed rtr -> rts");
}

}  // namespace rdma
