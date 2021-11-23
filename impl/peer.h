#if !defined(__PEER_H__)
#define __PEER_H__

#include "rdma_base.h"

namespace rdma {

/**
 * @brief Represents a remote node that has RDMA connection with me.
 * I may have multiple connections with a peer.
 */
class Peer {
    friend class Cluster;
    friend class ReliableConnection;
    friend class ExtendedReliableConnection;

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
     * @brief On destruction, disconnects all its connections and frees up all
     * its memory regions.
     */
    ~Peer();

    inline std::pair<uintptr_t, size_t> remote_mr(int id) const {
        return {reinterpret_cast<uintptr_t>(this->remote_mrs[id].addr),
                this->remote_mrs[id].length};
    }

    /**
     * @brief Get the reference of an RDMA reliable connection with certain ID.
     * If the ID is not specified, return the first reliable connection.
     *
     * @param id The ID of the RDMA reliable connection.
     * @return ReliableConnection& Object reference representing the RDMA
     * reliable connection.
     */
    inline ReliableConnection &rc(int id = 0) const { return *rcs[id]; }

    /**
     * @brief Get the reference of connection with certain ID.
     * If the ID is not specified, return the first connection.
     *
     * @param id The ID of the connection.
     * @return ReliableConnection& Object reference representing the RDMA
     * @deprecated Because of the introduction of XRC, this method has been deprecated to avoid
     * misunderstand.
     */
    inline ReliableConnection &connection(int id = 0) const { return this->rc(id); }

    int verbose() const;

  private:
    explicit Peer(Cluster &cluster, int id, ConnectionConfig config);

    void fill_exchange(OOBExchange *xchg);
    void establish(OOBExchange *remote_xchg);

    /**
     * @brief Match a given remote address range to MR and return its rkey.
     */
    inline uint32_t match_remote_mr_rkey(void const *addr, size_t size = 0) const {
        switch (this->nrmrs) {
        case 4:
            if (addr >= this->remote_mrs[3].addr &&
                reinterpret_cast<char const *>(addr) + size <=
                    reinterpret_cast<char const *>(this->remote_mrs[3].addr) +
                        this->remote_mrs[3].length)
                return this->remote_mrs[3].rkey;
            [[fallthrough]];
        case 3:
            if (addr >= this->remote_mrs[2].addr &&
                reinterpret_cast<char const *>(addr) + size <=
                    reinterpret_cast<char const *>(this->remote_mrs[2].addr) +
                        this->remote_mrs[2].length)
                return this->remote_mrs[2].rkey;
            [[fallthrough]];
        case 2:
            if (addr >= this->remote_mrs[1].addr &&
                reinterpret_cast<char const *>(addr) + size <=
                    reinterpret_cast<char const *>(this->remote_mrs[1].addr) +
                        this->remote_mrs[1].length)
                return this->remote_mrs[1].rkey;
            [[fallthrough]];
        case 1:
            if (addr >= this->remote_mrs[0].addr &&
                reinterpret_cast<char const *>(addr) + size <=
                    reinterpret_cast<char const *>(this->remote_mrs[0].addr) +
                        this->remote_mrs[0].length)
                return this->remote_mrs[0].rkey;
            [[fallthrough]];
        default:
            throw std::runtime_error("cannot match remote mr");
        }
    }

    /**
     * @brief Match a given remote address range to MR and return its rkey.
     */
    inline uint32_t match_remote_mr_rkey(uintptr_t addr, size_t size = 0) const {
        return this->match_remote_mr_rkey(reinterpret_cast<void *>(addr), size);
    }

    Cluster *cluster;
    Context *ctx;
    int id;
    int nconns;
    int nrmrs;
    std::array<ibv_mr, Consts::MaxMrs> remote_mrs;

    std::vector<ReliableConnection *> rcs;
    std::vector<ExtendedReliableConnection *> xrcs;
};

}  // namespace rdma

#endif  // __PEER_H__
