#if !defined(__RDMA_BASE_H__)
#define __RDMA_BASE_H__

#include <infiniband/verbs.h>
#include <mpi.h>
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
     * @brief Maximum number of allowed threads on each node.
     * rdmalib allows `MaxThread * MaxThread` RCs and `MaxThreads` XRCs per node.
     */
    static const int MaxThreads = 32;

    /**
     * @brief Maximum number of outstanding WR/CQE of each QP/SRQ/CQ.
     */
    static const int MaxQueueDepth = 256;

    /**
     * @brief Maximum number of WRs to be posted at the same time.
     */
    static const int MaxPostWR = 32;
};

class Context;
class Cluster;
class Peer;

class ReliableConnection;
class ExtendedReliableConnection;

class Emergency {
    friend class Context;
    friend class Cluster;
    friend class Peer;
    friend class ReliableConnection;
    friend class ExtendedReliableConnection;

    [[noreturn]] inline static void abort(const std::string &message, int retval = -1)
    {
        int rank;
        int rc = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rc != MPI_SUCCESS)
            fprintf(stderr, "%s\n", message.c_str());
        else
            fprintf(stderr, "[node %d] %s\n", rank, message.c_str());

        // exit(retval);
        throw retval;
    }
};

}  // namespace rdma

#endif  // __RDMA_BASE_H__
