#if !defined(__CLUSTER_H__)
#define __CLUSTER_H__

#include "rdma_base.h"

namespace rdma {

/**
 * @brief Represents the whole RDMA cluster.
 * A pseudo-singleton design pattern is adopted to prevent the user from
 * creating multiple Cluster objects.
 */
class Cluster {
    friend class Context;
    friend class Peer;
    friend class ReliableConnection;

  public:
    /**
     * @brief Construct a Cluster object basing on an RDMA context.
     * The construction is allowed for only once. Further attempts will throw
     * `std::runtime_error`.
     *
     * @warning MPI context must be already set-up for a Cluster to be
     * constructed.
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
     * This method is allowed to be called only once. Further attempts will
     * throw `std::runtime_error`.
     *
     * @param connections Number of connections between each pair of peers.
     * Default to 1.
     * @param type Type of the connection. Currently, only RC and XRC are
     * supported.
     */
    void connect(int connections = 1, ConnectionConfig config = {1, 0});

    /**
     * @brief Synchronize among all peers with MPI_Barrier.
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
     * @brief Get the reference of peer with certain MPI rank.
     * Throw `std::runtime_error` if the rank is myself's rank.
     *
     * @param id The ID (MPI rank) of the peer.
     * @return Peer& Object reference representing the remote peer.
     */
    inline Peer &peer(int id) const { return *peers[id]; }

    /**
     * @brief Locally asks all peer connections to verbose themselves.
     * @note This function is completely local incurs no RDMA nor Ethernet
     * traffic.
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

}  // namespace rdma

#endif  // __CLUSTER_H__
