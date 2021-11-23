#if !defined(__RDMA_BASE_H__)
#define __RDMA_BASE_H__

#include <infiniband/verbs.h>
#include <array>
#include <atomic>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rdma {

/**
 * @brief Constants used by rdmalib.
 * Follow the semantics of these constants, or rdmalib can crash.
 */
class Consts {
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
     * @brief Maximum number of allowed connections between any pair of peers of any connection
     * type.
     */
    static const int MaxConnections = 32;
};

/**
 * @brief Configuration of the connections to be established.
 */
class ConnectionConfig {
  public:
    /**
     * @brief Number of reliable connections to establish.
     * @note Must not exceed `Consts::MaxConnections`.
     */
    int num_rc = 0;

    /**
     * @brief Number of extended reliable connections to establish.
     * @note Must not exceed `Consts::MaxConnections`.
     */
    int num_xrc = 0;

    /**
     * @brief Get the total number of connections.
     * If the result is zero, the cluster will not connect.
     *
     * @return int The total number of connections to establish.
     */
    inline int sum_of_connections() const { return num_rc + num_xrc; }
};

class Context;
class Cluster;
class Peer;

class ReliableConnection;
class ExtendedReliableConnection;

/**
 * @brief Inner structure for out-of-band QP information exchange.
 */
struct OOBExchange {
    friend class Cluster;

    int lid;
    int num_mrs;
    int num_qps;
    ibv_mr mr[Consts::MaxMrs];
    uint8_t gid[sizeof(ibv_gid)];
    uint32_t qpn[Consts::MaxConnections];

    inline void verbose() {
        printf("--- Verbose RDMA exchange info ---\n");
        printf("- mr (%d):\n", num_mrs);
        for (int i = 0; i < num_mrs; i++)
            printf("[%d] addr = %p, lkey = %u, rkey = %u\n", i, mr[i].addr, mr[i].lkey, mr[i].rkey);
        printf("- lid: %d\n", lid);
        printf("- qpn (%d):", num_qps);
        for (int i = 0; i < num_qps; i++) printf((i + 1 == num_qps ? " %u" : " %u\n"), qpn[i]);
        printf("- gid: ");
        for (int i = 0; i < 16; i++) printf((i + 1 == 16 ? "%02X\n" : "%02X:"), gid[i]);
    }

  private:
    explicit OOBExchange() = default;
};

}  // namespace rdma

#endif  // __RDMA_BASE_H__
