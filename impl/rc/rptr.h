#if !defined(__RPTR_H__)
#define __RPTR_H__

#include <atomic>
#include <chrono>
#include <cstdint>
#include <type_traits>

#include "rc.h"

/**
 * @brief An abstraction for a non-thread-safe pointer to remote memory.
 * It aims to provide semantics similar to normal pointers.
 *
 * @note
 * Users must associate an RDMA-registered memory region (also with enough permission) with the
 * pointer to store a local copy. Users should ensure that different `rptr` objects associate with
 * different memory regions, unless there are special needs.
 *
 * Because we cannot monitor writes to local buffers, users needs to commit the changes after they
 * write the buffer. Macros starting with `rptr_update` are used to modify the object as a whole or
 * modify a member variable and then commit.
 *
 * If T has no volatile-qualifier, then reads/writes will cause the result to be cached locally for
 * future reads. Otherwise, each read will reach the remote side (in this case, returned type will
 * not contain volatile-qualifier).
 *
 * If T satisfies all requirements for a lock-free std::atomic<T> and sizeof(T) <= 8, then atomic
 * operations are also supported.
 *
 * @tparam T Object type, default to uint8_t (memory semantics). Remember constantly that object
 * with type T is AT REMOTE SIDE.
 */
template <typename T = uint8_t>
class rptr {
  public:
    /**
     * @brief The object type being pointed to.
     */
    using object_type = T;

    /**
     * @brief The actual object type.
     */
    using real_object_type = std::remove_volatile_t<T>;

    explicit rptr(rdma::ReliableConnection &rc, uintptr_t remote_ptr, void *local_ptr)
        : rc(&rc), remote_ptr(remote_ptr), local_ptr(reinterpret_cast<uint8_t *>(local_ptr))
    {
        valid = false;
    }

    inline real_object_type &operator*()
    {
        if (!valid || std::is_volatile_v<T>) {
            rc->post_read(local_ptr, remote_ptr, sizeof(T), true);
            rc->poll_send_cq();
            valid = true;
        }
        return *reinterpret_cast<real_object_type *>(local_ptr);
    }

    inline real_object_type *operator->() { return &(*(*this)); }

    inline rptr<T> &operator=(uintptr_t remote_ptr)
    {
        if (remote_ptr != this->remote_ptr) {
            this->remote_ptr = remote_ptr;
            valid = false;
        }
        return *this;
    }

    inline rptr<T> &operator=(const rptr<T> &b)
    {
        rc = b.rc;
        remote_ptr = b.remote_ptr;
        local_ptr = b.local_ptr;
        valid = b.valid;
        return *this;
    }

    inline operator uintptr_t() const { return remote_ptr; }
    inline operator bool() const { return !!remote_ptr; }

    /**
     * @brief Get the local buffer, no matter whether it is valid.
     *
     * @return real_object_type* Local buffer.
     */
    inline real_object_type *dereference() const
    {
        return reinterpret_cast<real_object_type *>(local_ptr);
    }

    /**
     * @brief Commit the content to remote side.
     * This also causes the current local copy to be valid.
     *
     * @param sync If set, this will be a synchronous operation.
     */
    inline void commit(bool sync = false)
    {
        commit(0, sizeof(T), sync);
        this->validate();
    }

    /**
     * @brief Commit part of the content to remote side.
     * This does not cause further accesses to the same part to be served locally.
     *
     * @param offset Offset.
     * @param len Length.
     * @param sync If set, this will be a synchronous operation.
     */
    inline void commit(size_t offset, size_t len, bool sync = false)
    {
        if (valid) {
            rc->post_write(remote_ptr + offset, local_ptr + offset, len, true);
            if (sync)
                rc->poll_send_cq();
        }
    }

    /**
     * @brief Perform RDMA compare-and-swap. Cause the local buffer to become valid.
     * Note that both arguments are passed-as-value, so `compare` will not be modified.
     *
     * @param compare The value to be compared. Local buffer will be filled in with this value
     * first.
     * @param exchange The value to be exchanged.
     * @param sync If set (default), this will be a synchronous operation.
     * @return true If success.
     * @return false If failed, or T does not support compare-and-swap.
     */
    inline bool compare_exchange(real_object_type compare, real_object_type exchange,
                                 bool sync = true)
    {
        if constexpr (sizeof(real_object_type) == sizeof(uint64_t)) {
            *(this->dereference()) = compare;
            rc->post_atomic_cas(remote_ptr, local_ptr, exchange, true);
            if (sync)
                rc->poll_send_cq();
            this->validate();
            return *(this->dereference()) == compare;
        }
        return false;
    }

    /**
     * @brief Perform RDMA masked compare-and-swap. Cause the local buffer to become valid.
     * Note that both arguments are passed-as-value, so `compare` will not be modified.
     *
     * @param compare The value to be compared. Local buffer will be filled in with this value
     * first.
     * @param compare_mask Compare mask.
     * @param exchange The value to be exchanged.
     * @param exchange_mask Exchange mask.
     * @param sync If set (default), this will be a synchronous operation.
     * @return true If success.
     * @return false If failed, or T does not support compare-and-swap.
     */
    inline bool masked_compare_exchange(real_object_type compare, uint64_t compare_mask,
                                        real_object_type exchange, uint64_t exchange_mask,
                                        bool sync = true)
    {
        if constexpr (sizeof(real_object_type) == sizeof(uint64_t)) {
            *(this->dereference()) = compare;
            rc->post_masked_atomic_cas(remote_ptr, local_ptr, compare_mask, exchange, exchange_mask,
                                       sync);
            if (sync)
                rc->poll_send_cq();
            this->validate();
            return *(this->dereference()) == compare;
        }
        return false;
    }

    /**
     * @brief Perform RDMA fetch-and-add. Cause the local buffer to become valid.
     *
     * @param add The value to be added.
     * @param sync If set (default), this will be a synchronous operation.
     * @return real_object_type The value fetched (also filled in the local buffer). If T
     * does not support fetch-and-add, return empty object by calling its default constructor.
     */
    inline real_object_type fetch_add(uint64_t add, bool sync = true)
    {
        if constexpr (sizeof(real_object_type) == sizeof(uint64_t)) {
            rc->post_atomic_faa(remote_ptr, local_ptr, add, sync);
            if (sync)
                rc->poll_send_cq();
            this->validate();
            return *(this->dereference());
        }
        return real_object_type{};
    }

    /**
     * @brief Perform RDMA masked fetch-and-add. Cause the local buffer to become valid.
     *
     * @param add The value to be added (subject to the field between `highest_bit` and
     * `lowest_bit`).
     * @param highest_bit Highest bit of this field.
     * @param lowest_bit Lowest bit of this field.
     * @param sync If set (default), this will be a synchronous operation.
     * @return T The value fetched (also filled in the local buffer). If T does not support
     * fetch-and-add, return empty object by calling its default constructor.
     */
    inline real_object_type field_fetch_add(uint64_t add, int highest_bit = 63, int lowest_bit = 0,
                                            bool sync = true)
    {
        if constexpr (sizeof(real_object_type) == sizeof(uint64_t)) {
            rc->post_field_atomic_faa(remote_ptr, local_ptr, add, highest_bit, lowest_bit, sync);
            if (sync)
                rc->poll_send_cq();
            this->validate();
            return *(this->dereference());
        }
        return real_object_type{};
    }

    /**
     * @brief Perform RDMA masked fetch-and-add. Cause the local buffer to become valid.
     *
     * @param time_limit_us The time limit for this networked request. If exceeded, return early.
     * @param success An output parameter indicating whether the time limit specified has exceeded.
     * @param add The value to be added (subject to the field between `highest_bit` and
     * `lowest_bit`).
     * @param highest_bit Highest bit of this field.
     * @param lowest_bit Lowest bit of this field.
     * @param sync If set (default), this will be a synchronous operation.
     * @return real_object_type The value fetched (also filled in the local buffer). If T
     * does not support fetch-and-add, return empty object by calling its default constructor.
     */
    inline real_object_type field_fetch_add_timelimit(unsigned time_limit_us, bool *success,
                                                      uint64_t add, int highest_bit = 63,
                                                      int lowest_bit = 0, bool sync = true)
    {
        if constexpr (sizeof(real_object_type) == sizeof(uint64_t)) {
            rc->post_field_atomic_faa(remote_ptr, local_ptr, add, highest_bit, lowest_bit, sync);
            if (sync) {
                ibv_wc wc[2];
                auto start = std::chrono::steady_clock::now();
                while (true) {
                    if (std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count() < time_limit_us) {
                        *success = false;
                        return real_object_type{};
                    }
                    int res = rc->poll_send_cq_once(wc);
                    if (res)
                        break;
                }
            }
            *success = true;
            this->validate();
            return *(this->dereference());
        }
        *success = false;
        return real_object_type{};
    }

    /**
     * @brief Perform RDMA masked fetch-and-add. Cause the local buffer to become valid.
     *
     * @param add The value to be added (subject to `boundary_mask`).
     * @param boundary_mask Set bits indicate left boundaries of FAA operations.
     * @param sync If set (default), this will be a synchronous operation.
     * @return real_object_type The value fetched (also filled in the local buffer). If T
     * does not support fetch-and-add, return empty object by calling its default constructor.
     */
    inline real_object_type masked_fetch_add(uint64_t add, uint64_t boundary_mask, bool sync = true)
    {
        if constexpr (sizeof(real_object_type) == sizeof(uint64_t)) {
            rc->post_masked_atomic_faa(remote_ptr, local_ptr, add, boundary_mask, sync);
            if (sync)
                rc->poll_send_cq();
            this->validate();
            return *(this->dereference());
        }
        return real_object_type{};
    }

    /**
     * @brief Perform RDMA masked fetch-and-add. Cause the local buffer to become valid.
     *
     * @param time_limit_us The time limit for this networked request. If exceeded, return early.
     * @param success An output parameter indicating whether the time limit specified has exceeded.
     * @param add The value to be added (subject to `boundary_mask`).
     * @param highest_bit Highest bit of this field.
     * @param lowest_bit Lowest bit of this field.
     * @param sync If set (default), this will be a synchronous operation.
     * @return real_object_type The value fetched (also filled in the local buffer). If T
     * does not support fetch-and-add, return empty object by calling its default constructor.
     */
    inline real_object_type field_fetch_add_timelimit(unsigned time_limit_us, bool *success,
                                                      uint64_t add, uint64_t boundary_mask,
                                                      bool sync = true)
    {
        if constexpr (sizeof(real_object_type) == sizeof(uint64_t)) {
            rc->post_masked_atomic_faa(remote_ptr, local_ptr, add, boundary_mask, sync);
            if (sync) {
                ibv_wc wc[2];
                auto start = std::chrono::steady_clock::now();
                while (true) {
                    if (std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count() < time_limit_us) {
                        *success = false;
                        return real_object_type{};
                    }
                    int res = rc->poll_send_cq_once(wc);
                    if (res)
                        break;
                }
            }
            *success = true;
            this->validate();
            return *(this->dereference());
        }
        *success = false;
        return real_object_type{};
    }

    /**
     * @brief Manually set the validation flag of the local copy.
     *
     * @param _valid Whether the local copy should be valid.
     * @return rptr<T>& Self.
     */
    inline rptr<T> &validate(bool _valid = true)
    {
        valid = _valid;
        return *this;
    }

    /**
     * @brief Invalidate the local copy.
     * Equivalent to `validate(false)`.
     *
     * @return rptr<T>& Self.
     */
    inline rptr<T> &invalidate() { return validate(false); }

    /**
     * @brief Reinterpret the pointer at a specified offset to get a `rptr` instance to its member
     * (or subpart).
     *
     * @tparam Tp The type to be reinterpreted as. If this is a pointer, the pointer will be
     * removed.
     * @param offset Offset of the member or the subpart.
     * @return rptr<std::remove_pointer_t<Tp>> `rptr` instance as wish.
     */
    template <typename Tp>
    inline rptr<std::remove_pointer_t<Tp>> reinterpret_at(size_t offset = 0)
    {
        return rptr<std::remove_pointer_t<Tp>>(*rc, remote_ptr + offset, local_ptr + offset)
            .validate(valid);
    }

  private:
    rdma::ReliableConnection *rc;
    uintptr_t remote_ptr;
    uint8_t *local_ptr;

    bool valid;
};

#define rptr_update(p, value)         \
    do {                              \
        *(p.dereference()) = (value); \
        (p).commit();                 \
    } while (false)

#define rptr_update_sync(p, value)    \
    do {                              \
        *(p.dereference()) = (value); \
        (p).commit(true);             \
    } while (false)

#define rptr_update_member(p, member, value)                                                  \
    do {                                                                                      \
        (p.dereference())->member = (value);                                                  \
        (p).commit(offsetof(typename decltype(p)::object_type, member), sizeof((p)->member)); \
    } while (false)

#define rptr_update_member_sync(p, member, value)                                            \
    do {                                                                                     \
        (p.dereference())->member = (value);                                                 \
        (p).commit(offsetof(typename decltype(p)::object_type, member), sizeof((p)->member), \
                   true);                                                                    \
    } while (false)

#endif  // __RPTR_H__
