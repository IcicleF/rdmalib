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

    /**
     * @brief Post RDMA one-sided READ verb to this QP.
     * Equivalence: `async memcpy(local.dst, remote.src, size);`
     *
     * @param dst Local virtual address, the destination to place the read content. Must have been
     * registered locally.
     * @param src Remote virtual address, the source to read. Must have been registered by the peer.
     * @param size Bytes to read.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_post_send` function.
     */
    int post_read(void *dst, uintptr_t src, size_t size, bool signaled = false, uint32_t wr_id = 0);

    /**
     * @brief Post RDMA one-sided WRITE verb to this QP.
     * Equivalence: `async memcpy(remote.dst, local.src, size);`
     *
     * @param dst Remote virtual address, the destination to place the written content. Must have
     * been registered by the peer.
     * @param src Local virtual address, the source of write content. Must have been registered
     * locally.
     * @param size Bytes to write.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_post_send` function.
     */
    int post_write(uintptr_t dst, void const *src, size_t size, bool signaled = false,
                   uint32_t wr_id = 0);

    /**
     * @brief Post RDMA two-sided SEND verb to this QP.
     *
     * @param src Local virtual address, the source of send content. Must have been registered
     * locally.
     * @param size Bytes to send.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_post_send` function.
     */
    int post_send(void const *src, size_t size, bool signaled = false, uint32_t wr_id = 0);

    /**
     * @brief Post RDMA two-sided RECV verb to this QP.
     *
     * @param dst Local virtual address, the destination to place received content. Must have been
     * registered locally.
     * @param size Maximum bytes to receive.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_post_recv` function.
     */
    int post_recv(void *dst, size_t size, uint32_t wr_id = 0);

    /**
     * @brief Post RDMA one-sided ATOMIC COMPARE-AND-SWAP (CAS) verb to this QP.
     *
     * @param dst Remote virtual address, the start address of the 8-byte to CAS. Must have been
     * registered by the peer.
     * @param compare Local virtual address, the start address of the 8-byte to compare and to place
     * the value if "compare" fails. Must have been registered locally.
     * @param swap The 8-byte to swap if "compare" succeeds.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_post_send` function.
     */
    int post_atomic_cas(uintptr_t dst, void *compare, uint64_t swap, bool signaled = false,
                        uint32_t wr_id = 0);

    /**
     * @brief Post RDMA one-sided ATOMIC FETCH-AND-ADD (FAA) verb to this QP.
     *
     * @param dst Remote virtual address, the start address of the 8-byte to FAA. Must have been
     * registered by the peer.
     * @param fetch Local virtual address, the destination of the fetched 8-byte. Must have been
     * registered locally.
     * @param add The value to add.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_post_send` function.
     */
    int post_atomic_faa(uintptr_t dst, void *fetch, uint64_t add, bool signaled = false,
                        uint32_t wr_id = 0);

    /**
     * @brief Post RDMA experimental one-sided MASKED ATOMIC COMPARE-AND-SWAP (MASKED-CAS) verb to
     * this QP.
     *
     * @param dst Remote virtual address, the start address of the 8-byte to MASKED-CAS. Must have
     * been registered by the peer.
     * @param compare Local virtual address, the start address of the 8-byte to compare and to place
     * the value if "compare" fails. Must have been registered locally.
     * @param compare_mask The compare mask. Can be all-zero.
     * @param swap The 8-byte to swap if "compare" succeeds.
     * @param swap_mask The swap mask. Can be all-zero.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_exp_post_send` function.
     */
    int post_masked_atomic_cas(uintptr_t dst, void *compare, uint64_t compare_mask, uint64_t swap,
                               uint64_t swap_mask, bool signaled = false, uint32_t wr_id = 0);

    /**
     * @brief Post RDMA experimental one-sided MASKED ATOMIC FETCH-AND-ADD (MASKED-FAA) verb to this
     * QP.
     * This is a decorated interface, and should be called if the 8-byte is split into many fields
     * and the user only wants to increment ONE OF THEM.
     *
     * @param dst Remote virtual address, the start address of the 8-byte to MASKED-FAA. Must have
     * been registered by the peer.
     * @param fetch Local virtual address, the destination of the fetched 8-byte. Must have been
     * registered locally.
     * @param add The value to add. Note that it will be added inside the designated field. Do not
     * shift-left beforehand.
     * @param highest_bit The highest bit of the field to add.
     * @param lowest_bit The lowest bit of the field to add.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_exp_post_send` function.
     */
    int post_field_atomic_faa(uintptr_t dst, void *fetch, uint64_t add, int highest_bit = 63,
                              int lowest_bit = 0, bool signaled = false, uint32_t wr_id = 0);

    /**
     * @brief Post RDMA experimental one-sided MASKED ATOMIC FETCH-AND-ADD (MASKED-FAA) verb to this
     * QP.
     *
     * @param dst Remote virtual address, the start address of the 8-byte to MASKED-FAA. Must have
     * been registered by the peer.
     * @param fetch Local virtual address, the destination of the fetched 8-byte. Must have been
     * registered locally.
     * @param add The value to add.
     * @param boundary The boundary bitmap of the fields.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @param wr_id The work request ID associated with this request. Will appear in the CQE polled
     * if `signaled` is true.
     * @return int The status code returned by `ibv_exp_post_send` function.
     */
    int post_masked_atomic_faa(uintptr_t dst, void *fetch, uint64_t add, uint64_t boundary,
                               bool signaled = false, uint32_t wr_id = 0);

    /**
     * @brief Post RDMA experimental WAIT verb to this QP.
     *
     * @param cq CQ to wait.
     * @param cqe Number of CQEs to wait.
     * @param signaled If true, this request will generate a completion event in the CQ which must
     * be later polled.
     * @return int The status code returned by `ibv_exp_post_send` function.
     */
    int post_wait(ibv_cq *cq, int cqe = 1, bool signaled = false);

    /**
     * @brief Post a batch of RDMA one-sided READ verbs to this QP.
     * User is responsible to ensure that QP send queue will not overflow.
     * Only the last READ will generate a CQE.
     *
     * @param dst_arr An array of local destination addresses.
     * @param src_arr An array of remote source addresses.
     * @param size_arr An array of the numbers of bytes to read.
     * @param count Number of READ verbs to post.
     * @param wr_id_start The work request ID of the first READ. Each following READ will be
     * assigned an incremented ID.
     * @return int The status code returned by `ibv_post_send` function.
     */
    int post_batch_read(void **dst_arr, uintptr_t *src_arr, size_t *size_arr, int count,
                        uint32_t wr_id_start = 0);

    /**
     * @brief Post a batch of RDMA one-sided WRITE verbs to this QP.
     * User is responsible to ensure that QP send queue will not overflow.
     * Only the last WRITE will generate a CQE.
     *
     * @param dst_arr An array of remote destination addresses.
     * @param src_arr An array of local source addresses.
     * @param size_arr An array of the numbers of bytes to write.
     * @param count Number of WRITE verbs to post.
     * @param wr_id_start The work request ID of the first WRITE. Each following WRITE will be
     * assigned an incremented ID.
     * @return int The status code returned by `ibv_post_send` function.
     */
    int post_batch_write(uintptr_t *dst_arr, void **src_arr, size_t *size_arr, int count,
                         uint32_t wr_id_start = 0);

    /**
     * @brief Post a batch of RDMA experimental one-sided MASKED ATOMIC FETCH-AND-ADD (MASKED-FAA)
     * verbs to this QP.
     *
     * @param dst_arr An array of start addresses of remote 8-bytes to MASKED-FAA. Must have been
     * registered by the peer.
     * @param fetch_arr An array of destinations to store the fetched 8-bytes. Must have been
     * registered locally.
     * @param add_arr An array of values to add.
     * @param boundary_arr An array of boundary bitmaps.
     * @param count Number of MASKED-FAA verbs to post.
     * @param wr_id_start The work request ID of the first WRITE. Each following WRITE will be
     * assigned an incremented ID.
     * @return int The status code returned by `ibv_exp_post_send` function.
     */
    int post_batch_masked_atomic_faa(uintptr_t *dst_arr, void **fetch_arr, uint64_t *add_arr,
                                     uint64_t *boundary_arr, int count, uint32_t wr_id_start = 0);

    void fill_sge(ibv_sge *sge, void *addr, size_t length);
    int post_send(ibv_exp_send_wr *wr);
    int post_recv(ibv_recv_wr *wr);

    int poll_send_cq(int n = 1);
    int poll_send_cq(ibv_wc *wc_arr, int n = 1);
    int poll_send_cq_once(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq(int n = 1);
    int poll_recv_cq(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq_once(ibv_wc *wc_arr, int n = 1);

    inline ibv_cq *get_send_cq() const { return this->send_cq; }
    inline ibv_cq *get_recv_cq() const { return this->recv_cq; }

    int verbose() const;

  private:
    explicit ReliableConnection(Peer &peer, int id);
    explicit ReliableConnection(Peer &peer, int id, ibv_cq *send_cq, ibv_cq *recv_cq);
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
    bool cq_self;
};

}  // namespace rdma

#endif  // __RC_H__
