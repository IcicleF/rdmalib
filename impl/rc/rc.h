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

    int post_atomic_cas(uintptr_t dst, void *expected, uint64_t desire, bool signaled = false,
                        int wr_id = 0);
    int post_atomic_fa(uintptr_t dst, void *before, uint64_t delta, bool signaled = false,
                       int wr_id = 0);
    int post_masked_atomic_cas(uintptr_t dst, void *expected, uint64_t expected_mask,
                               uint64_t desire, uint64_t desire_mask, bool signaled = false,
                               int wr_id = 0);
    int post_masked_atomic_fa(uintptr_t dst, void *before, uint64_t delta, int highest_bit = 63,
                              int lowest_bit = 0, bool signaled = false, int wr_id = 0);

    int poll_send_cq(int n = 1);
    int poll_send_cq(ibv_wc *wc_arr, int n = 1);
    int poll_send_cq_once(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq(int n = 1);
    int poll_recv_cq(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq_once(ibv_wc *wc_arr, int n = 1);

    int verbose() const;

    static const int MaxQueueDepth = 256;

  private:
    explicit ReliableConnection(Peer &peer, int id);
    ~ReliableConnection();

    int create_cq(ibv_cq **cq, int cq_depth = MaxQueueDepth);
    int create_qp(ibv_qp_type qp_type = IBV_QPT_RC, int qp_depth = MaxQueueDepth);

    void fill_exchange(OOBExchange *xchg);
    void establish(uint8_t *gid, int lid, uint32_t qpn);
    void modify_to_init();
    void modify_to_rtr(uint8_t *gid, int lid, uint32_t qpn);
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
