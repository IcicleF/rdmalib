#if !defined(__CONTEXT_H__)
#define __CONTEXT_H__

#include "rdma_base.h"

namespace rdma {

/**
 * @brief Represent an RDMA context (`ibv_context *`).
 * Context does not maintain RDMA protection domains; they are maintained by
 * peers. Therefore, please avoid using this library for high fan-in, high
 * fan-out scenarios.
 */
class Context {
    friend class Cluster;
    friend class Peer;

    friend class ReliableConnection;
    friend class ExtendedReliableConnection;

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
     * This allows customized modifications to the RDMA context, but can be
     * dangerous.
     *
     * @note This function does not hand the ownership of the context object to
     * the user; do not close the context.
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
            Emergency::abort("cannot match local mr");
        }
    }

    /**
     * @brief Match a given address range to MR and return its lkey.
     */
    inline int match_mr_lkey(uintptr_t addr, size_t size = 0) const
    {
        return this->match_mr_lkey(reinterpret_cast<void *>(addr), size);
    }

    ibv_device_attr device_attr;
    ibv_port_attr port_attr;
    ibv_context *ctx;
    ibv_gid gid;
    ibv_pd *pd;
    ibv_xrcd *xrcd;

    int nmrs = 0;
    std::array<ibv_mr *, Consts::MaxMrs> mrs;
    std::atomic<unsigned> refcnt;
};

}  // namespace rdma

#endif  // __CONTEXT_H__
