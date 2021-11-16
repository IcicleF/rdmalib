#if !defined(__RDMA_H__)
#define __RDMA_H__

#include <infiniband/verbs.h>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace rdma
{

class Consts
{
public:
    /**
     * @brief Maximum number of allowed memory regions per Context.
     */
    static const int MaxMrs = 4;

    /**
     * @brief Maximum number of allowed peers (including myself) per Cluster.
     */
    static const int MaxPeers = 256;

    /**
     * @brief Maximum number of allowed connections between any pair of peers.
     */
    static const int MaxConnectionsPerPeer = 32;
};

class Context;
class Cluster;
class Peer;
class Connection;

/**
 * @brief Inner structure for out-of-band QP information exchange.
 */
struct OOBExchange
{
    friend class Cluster;

    int lid;
    int num_mrs;
    int num_qps;
    ibv_mr mr[Consts::MaxMrs];
    uint8_t gid[sizeof(ibv_gid)];
    uint32_t qpn[Consts::MaxConnectionsPerPeer];

    inline void verbose()
    {
        printf("--- Verbose RDMA exchange info ---\n");
        printf("- mr (%d):\n", num_mrs);
        for (int i = 0; i < num_mrs; i++)
            printf("[%d] addr = %p, lkey = %u, rkey = %u\n", i, mr[i].addr, mr[i].lkey, mr[i].rkey);
        printf("- lid: %d\n", lid);
        printf("- qpn (%d):", num_qps);
        for (int i = 0; i < num_qps; i++)
            printf((i + 1 == num_qps ? " %u": " %u\n"), qpn[i]);
        printf("- gid: ");
        for (int i = 0; i < 16; i++)
            printf((i + 1 == 16 ? "%02X\n" : "%02X:"), gid[i]);
    }

private:
    explicit OOBExchange() = default;
};

/**
 * @brief Represent an RDMA context (`ibv_context *`).
 * Context does not maintain RDMA protection domains; they are maintained by peers.
 * Therefore, please avoid using this library for high fan-in, high fan-out scenarios.
 */
class Context
{
    friend class Cluster;
    friend class Peer;
    friend class Connection;

public:
    /**
     * @brief Construct an RDMA context. 
     * Throws std::runtime_error if cannot find the specified device.
     * 
     * @param dev_name Name of the wanted RDMA NIC device.
     */
    explicit Context(char const *dev_name = nullptr);

    /**
     * @brief Copying the RDMA context is prohibited.
     */
    Context(Context const &) = delete;

    /**
     * @brief Moving the RDMA context is also prohibited.
     * Do not try to change the ownership of the Context object.
     */
    Context(Context &&) = delete;

    /**
     * @brief On destruction, there may be no references to the RDMA context.
     * Otherwise, throw `std::runtime_error`.
     */
    ~Context();

    /**
     * @brief Register an memory region.
     * 
     * @param addr Start address of the memory region.
     * @param size Length in bytes of the memory region.
     * @param perm Access permission (defaulted to all necessary).
     * @return int ID of the memory region, -1 on any error.
     */
    int reg_mr(void *addr, size_t size, int perm = 0xF);
    
    /**
     * @brief Register an memory region.
     * 
     * @param addr Start address of the memory region.
     * @param size Length in bytes of the memory region.
     * @param perm Access permission (defaulted to all necessary).
     * @return int ID of the memory region, -1 on any error.
     */
    int reg_mr(uintptr_t addr, size_t size, int perm = 0xF);

    /**
     * @brief Get the count of currently registered memory regions.
     * @return size_t Count of registered memory regions.
     */
    inline size_t mr_count() const { return nmrs; }

    /**
     * @brief Get the original RDMA context object.
     * This allows customized modifications to the RDMA context, but can be dangerous.
     * 
     * @note This function does not hand the ownership of the context object to the user;
     * do not close the context.
     * 
     * @return ibv_context* Original RDMA context.
     */
    inline ibv_context *get_ctx() const { return ctx; }

private:
    /**
     * @brief Check RNIC device attributes (might print to stderr).
     */
    void check_dev_attr();

    /**
     * @brief Match a given address range to MR and return its lkey. 
     */
    inline uint32_t match_mr_lkey(void const *addr, size_t size = 0) const
    {
        switch (this->nmrs) {
        case 4:
            if (addr >= this->mrs[3]->addr && 
                reinterpret_cast<char const *>(addr) + size <=
                reinterpret_cast<char const *>(this->mrs[3]->addr) + this->mrs[3]->length)
                return this->mrs[3]->lkey;
            [[fallthrough]];
        case 3:
            if (addr >= this->mrs[2]->addr && 
                reinterpret_cast<char const *>(addr) + size <=
                reinterpret_cast<char const *>(this->mrs[2]->addr) + this->mrs[2]->length)
                return this->mrs[2]->lkey;
            [[fallthrough]];
        case 2:
            if (addr >= this->mrs[1]->addr && 
                reinterpret_cast<char const *>(addr) + size <=
                reinterpret_cast<char const *>(this->mrs[1]->addr) + this->mrs[1]->length)
                return this->mrs[1]->lkey;
            [[fallthrough]];
        case 1:
            if (addr >= this->mrs[0]->addr && 
                reinterpret_cast<char const *>(addr) + size <=
                reinterpret_cast<char const *>(this->mrs[0]->addr) + this->mrs[0]->length)
                return this->mrs[0]->lkey;
            [[fallthrough]];
        default:
            throw std::runtime_error("cannot match local mr");
        }
    }

    /**
     * @brief Match a given address range to MR and return its lkey. 
     */
    inline int match_mr_lkey(uintptr_t addr, size_t size = 0) const
    {
        return this->match_mr_lkey(reinterpret_cast<void *>(addr), size);
    }

    void fill_exchange(OOBExchange *xchg);

    ibv_exp_device_attr device_attr;
    ibv_port_attr       port_attr;
    ibv_context         *ctx;
    ibv_gid             gid;
    ibv_pd              *pd;

    int nmrs = 0;
    std::array<ibv_mr *, Consts::MaxMrs> mrs;
    std::atomic<unsigned> refcnt;
};

/**
 * @brief Represents the whole RDMA cluster.
 * A pseudo-singleton design pattern is adopted to prevent the user from
 * creating multiple Cluster objects.
 */
class Cluster
{
    friend class Context;
    friend class Peer;
    friend class Connection;

public:
    /**
     * @brief Construct a Cluster object basing on an RDMA context.
     * The construction is allowed for only once. Further attempts will throw `std::runtime_error`.
     * 
     * @warning MPI context must be already set-up for a Cluster to be constructed.
     */
    explicit Cluster(Context &ctx);
    
    /**
     * @brief Copying the cluster is prohibited. 
     */
    Cluster(Cluster &) = delete;

    /**
     * @brief Moving the cluster is also prohibited.
     * Do not try to change the ownership of the Cluster object.
     */
    Cluster(Cluster &&) = delete;

    /**
     * @brief On destruction, frees up all RDMA connections.
     */
    ~Cluster();

    /**
     * @brief Synchronize among all peers and establish full RDMA connections.
     * No RPC wrappings are provided. In case of RPC needs, use eRPC instead.
     * This method is allowed for only once. Further attemptes will throw `std::runtime_error`.
     * 
     * @param connections Number of connections between each pair of peers. Default to 1.
     */
    void connect(int connections = 1);

    /**
     * @brief Synchronize among all peers with MPI.
     */
    void sync();

    /**
     * @brief Get the ID of this node.
     * 
     * @return int ID of this node.
     */
    inline int whoami() const { return this->id; }

    /**
     * @brief Get the size of the whole cluster.
     * 
     * @return int Size of the whole cluster.
     */
    inline int size() const { return this->n; }

    /**
     * @brief Retrieve reference of peer with certain MPI rank.
     * Throw `std::runtime_error` if the rank is myself's rank.
     * 
     * @param id The ID (MPI rank) of the peer.
     * @return Peer& Object reference representing the remote peer.
     */
    inline Peer &peer(int id) const { return *peers[id]; }

    /**
     * @brief Locally asks all peer connections to verbose themselves.
     * @note This function is completely local incurs no RDMA nor Ethernet traffic.
     * 
     * @return int 0 if no issues found, errno otherwise.
     */
    int verbose() const;

private:
    Context *ctx;
    int n;
    int id;
    std::atomic<bool> connected;
    std::vector<Peer *> peers;
};

/**
 * @brief Represents a remote node that has RDMA connection with me.
 * I may have multiple connections with a peer.
 */
class Peer
{
    friend class Cluster;
    friend class Connection;

public:
    /**
     * @brief Copying a peer object is prohibited.
     */
    Peer(Peer const &) = delete;

    /**
     * @brief Moving a peer object is prohibited.
     */
    Peer(Peer &&) = delete;

    /**
     * @brief On destruction, disconnects all its connections and frees up all its memory regions.
     */
    ~Peer();

    inline std::pair<uintptr_t, size_t> remote_mr(int id) const
    {
        return { reinterpret_cast<uintptr_t>(this->remote_mrs[id].addr), this->remote_mrs[id].length };
    }

    /**
     * @brief Retrieve reference of connection with certain ID.
     * Throw `std::runtime_error` if the ID is out of range.
     * If the ID is not specified, return the first connection.
     * 
     * @param id The ID of the connection.
     * @return Connection& Object reference representing the RDMA connection.
     */
    inline Connection &connection(int id = 0) const { return *conns[id]; }

    int verbose() const;

private:
    explicit Peer(Cluster &cluster, int id, int connections);

    void fill_exchange(OOBExchange *xchg);
    void establish(OOBExchange *remote_xchg);

    /**
     * @brief Match a given remote address range to MR and return its rkey. 
     */
    inline uint32_t match_remote_mr_rkey(void const *addr, size_t size = 0) const
    {
        switch (this->nrmrs) {
        case 4:
            if (addr >= this->remote_mrs[3].addr && 
                reinterpret_cast<char const *>(addr) + size <= 
                reinterpret_cast<char const *>(this->remote_mrs[3].addr) + this->remote_mrs[3].length)
                return this->remote_mrs[3].rkey;
            [[fallthrough]];
        case 3:
            if (addr >= this->remote_mrs[2].addr && 
                reinterpret_cast<char const *>(addr) + size <= 
                reinterpret_cast<char const *>(this->remote_mrs[2].addr) + this->remote_mrs[2].length)
                return this->remote_mrs[2].rkey;
            [[fallthrough]];
        case 2:
            if (addr >= this->remote_mrs[1].addr && 
                reinterpret_cast<char const *>(addr) + size <=
                reinterpret_cast<char const *>(this->remote_mrs[1].addr) + this->remote_mrs[1].length)
                return this->remote_mrs[1].rkey;
            [[fallthrough]];
        case 1:
            if (addr >= this->remote_mrs[0].addr && 
                reinterpret_cast<char const *>(addr) + size <=
                reinterpret_cast<char const *>(this->remote_mrs[0].addr) + this->remote_mrs[0].length)
                return this->remote_mrs[0].rkey;
            [[fallthrough]];
        default:
            throw std::runtime_error("cannot match remote mr");
        }
    }

    /**
     * @brief Match a given remote address range to MR and return its rkey. 
     */
    inline uint32_t match_remote_mr_rkey(uintptr_t addr, size_t size = 0) const
    {
        return this->match_remote_mr_rkey(reinterpret_cast<void *>(addr), size);
    }

    Cluster *cluster;
    Context *ctx;
    int id;
    int nconns;
    int nrmrs;
    std::array<ibv_mr, Consts::MaxMrs> remote_mrs;
    std::vector<Connection *> conns;
};

/**
 * @brief Represent an RDMA RC connection.
 */
class Connection
{
    friend class Peer;
    friend class Cluster;

public:
    Connection(Connection const &) = delete;
    Connection(Connection &&) = delete;

    int post_read(void *dst, uintptr_t src, size_t size, bool signaled = false, int wr_id = 0);
    int post_write(uintptr_t dst, void const *src, size_t size, bool signaled = false, int wr_id = 0);
    int post_send(void const *src, size_t size, bool signaled = false, int wr_id = 0);
    int post_recv(void *dst, size_t size, int wr_id = 0);

    int post_atomic_cas(uintptr_t dst, void *expected, uint64_t desire, bool signaled = false,  int wr_id = 0);
    int post_atomic_fa(uintptr_t dst, void *before, uint64_t delta, bool signaled = false, int wr_id = 0);
    int post_masked_atomic_cas(uintptr_t dst, void *expected, uint64_t expected_mask, 
                                uint64_t desire, uint64_t desire_mask, bool signaled = false, int wr_id = 0);
    int post_masked_atomic_fa(uintptr_t dst, void *before, uint64_t delta, int highest_bit = 63, int lowest_bit = 0,
                                bool signaled = false, int wr_id = 0);

    int poll_send_cq(int n = 1);
    int poll_send_cq(ibv_wc *wc_arr, int n = 1);
    int poll_send_cq_once(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq(int n = 1);
    int poll_recv_cq(ibv_wc *wc_arr, int n = 1);
    int poll_recv_cq_once(ibv_wc *wc_arr, int n = 1);
 
    int verbose() const;

    static const int MaxQueueDepth = 256;

private:
    explicit Connection(Peer &peer, int id);
    ~Connection();

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

}

#endif // __RDMA_H__    