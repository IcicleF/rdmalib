#if !defined(__RC_H__)
#define __RC_H__

#include "../cluster.h"
#include "../context.h"
#include "../peer.h"

namespace rdma {

/**
 * @brief Represent an RDMA RC connection.
 */
class ReliableConnection {
    friend class Peer;
    friend class Cluster;

  public:
    ReliableConnection(ReliableConnection const &) = delete;
    ReliableConnection(ReliableConnection &&) = delete;

    int post_read(void *dst, uintptr_t src, size_t size, bool signaled = false, int wr_id = 0);
    int post_write(uintptr_t dst, void const *src, size_t size, bool signaled = false,
                   int wr_id = 0);
    int post_send(void const *src, size_t size, bool signaled = false, int wr_id = 0);
    int post_recv(void *dst, size_t size, int wr_id = 0);

    int post_atomic_cas(uintptr_t dst, void *compare, uint64_t swap, bool signaled = false,
                        int wr_id = 0);
    int post_atomic_fa(uintptr_t dst, void *fetch, uint64_t add, bool signaled = false,
                       int wr_id = 0);
    int post_masked_atomic_cas(uintptr_t dst, void *compare, uint64_t compare_mask, uint64_t swap,
                               uint64_t swap_mask, bool signaled = false, int wr_id = 0);
    int post_masked_atomic_fa(uintptr_t dst, void *fetch, uint64_t add, int highest_bit = 63,
                              int lowest_bit = 0, bool signaled = false, int wr_id = 0);
    int post_masked_atomic_fa(uintptr_t dst, void *fetch, uint64_t add, uint64_t boundary,
                              bool signaled = false, int wr_id = 0);

    int poll_send_cq(int n = 1);
    int poll_send_cq(ibv_wc *wc_arr, int n = 1);
    int poll_send_cq_once(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq(int n = 1);
    int poll_recv_cq(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq_once(ibv_wc *wc_arr, int n = 1);

    int verbose() const;

  private:
    explicit ReliableConnection(Peer &peer, int id);
    ~ReliableConnection();

    int create_cq(ibv_cq **cq, int cq_depth = Consts::MaxQueueDepth);
    int create_qp(int qp_depth = Consts::MaxQueueDepth);

    void fill_exchange(OOBExchange *xchg);
    void establish(ibv_gid gid, int lid, uint32_t qpn);
    void modify_to_init();
    void modify_to_rtr(ibv_gid gid, int lid, uint32_t qpn);
    void modify_to_rts();

    static const int InitPSN = 3185;

    Context *ctx;
    Cluster *cluster;
    Peer *peer;
    int id;
    ibv_qp *qp;
    ibv_cq *send_cq;
    ibv_cq *recv_cq;
};

}  // namespace rdma

#endif  // __RC_H__
