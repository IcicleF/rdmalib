#if !defined(__XRC_H__)
#define __XRC_H__

#include "../cluster.h"
#include "../context.h"
#include "../peer.h"

namespace rdma {

/**
 * @brief Represent an end of RDMA XRC connection.
 * RDMA XRC is unidirectional. Therefore, an instance of `ExtendedReliableConnection` simultaneously
 * serves as (1) a per-thread initiator end, (2) a counterpart end of a remote initiator, and (3) a
 * per-thread receiver end. It is NOT a thread-to-thread connection.
 *
 * For example, suppose there are two nodes, both running 8 threads and want to communicate with
 * each other. With rdmalib, each node would have to create 8 * 8 = 64 `ReliableConnection`
 * instances with RDMA RC, but only need to create 8 `ExtendedReliableConnection` instances with
 * RDMA XRC.
 */
class ExtendedReliableConnection {
    friend class Peer;
    friend class Cluster;

  public:
    ExtendedReliableConnection(ExtendedReliableConnection const &) = delete;
    ExtendedReliableConnection(ExtendedReliableConnection &&) = delete;

    int post_read(void *dst, uintptr_t src, size_t size, bool signaled = false, int wr_id = 0);
    int post_write(uintptr_t dst, void const *src, size_t size, bool signaled = false,
                   int wr_id = 0);
    int post_send(void const *src, size_t size, int remote_id = 0, bool signaled = false,
                  int wr_id = 0);
    int post_recv(void *dst, size_t size, int wr_id = 0);

    int post_atomic_cas(uintptr_t dst, void *compare, uint64_t swap, bool signaled = false,
                        int wr_id = 0);
    int post_atomic_faa(uintptr_t dst, void *fetch, uint64_t add, bool signaled = false,
                        int wr_id = 0);
    int post_masked_atomic_cas(uintptr_t dst, void *compare, uint64_t compare_mask, uint64_t swap,
                               uint64_t swap_mask, bool signaled = false, int wr_id = 0);
    int post_field_atomic_faa(uintptr_t dst, void *fetch, uint64_t add, int highest_bit = 63,
                              int lowest_bit = 0, bool signaled = false, int wr_id = 0);

    int poll_send_cq(int n = 1);
    int poll_send_cq(ibv_wc *wc_arr, int n = 1);
    int poll_send_cq_once(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq(int n = 1);
    int poll_recv_cq(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq_once(ibv_wc *wc_arr, int n = 1);

    int verbose() const;

  private:
    explicit ExtendedReliableConnection(Peer &peer, int id);
    ~ExtendedReliableConnection();

    int create_cq(ibv_cq **cq, int cq_depth = Consts::MaxQueueDepth);
    int create_srq(ibv_srq **srq, ibv_cq *cq, int srq_depth = Consts::MaxQueueDepth);
    int create_qp(ibv_qp **qp, ibv_qp_type type, ibv_cq *send_cq, ibv_cq *recv_cq,
                  int qp_depth = Consts::MaxQueueDepth);

    void fill_exchange(OOBExchange *xchg);
    void establish(ibv_gid gid, int lid, uint32_t ini_qp_num, uint32_t tgt_qp_num);

    void modify_to_init();
    void modify_to_rtr(ibv_qp *qp, ibv_gid gid, int lid, uint32_t qp_num);
    void modify_to_rts();

    static const int InitPSN = 3185;

    Context *ctx;
    Cluster *cluster;
    Peer *peer;
    int id;

    ibv_qp *ini_qp;  // Initiator (belongs to this thread)
    ibv_qp *tgt_qp;  // Counterpart of remote initiator (DOES NOT belong to this thread)
    ibv_srq *srq;    // SRQ (belongs to this thread)

    ibv_cq *send_cq;         // Initiator side CQ
    ibv_cq *recv_cq;         // Receiver (SRQ) side CQ
    ibv_cq *placeholder_cq;  // Initiator's recv & Receiver's send
};

}  // namespace rdma

#endif  // __XRC_H__
