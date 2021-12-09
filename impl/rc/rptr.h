#if !defined(__RPTR_H__)
#define __RPTR_H__

#include <atomic>
#include <cstdint>
#include <type_traits>

#include "rc.h"

/**
 * @brief An abstraction for a non-thread-safe pointer to remote memory.
 * It aims to provide semantics similar to normal pointers.
 * Users must associate an RDMA-registered memory region (also with enough permission) with the
 * pointer to store what is read. Users should ensure that different `rptr` objects associates with
 * different memory regions, unless they have special needs.
 *
 * Because we cannot monitor writes to local buffers, users needs to commit the changes after they
 * write the buffer. Macros starting with `rptr_update` are used to modify the object as a whole or
 * modify a member variable and then commit.
 *
 * If T has no volatile-qualifier, then reads/writes will cause the result to be cached locally for
 * future reads. Otherwise, each read will reach the remote side.
 *
 * If T satisfies all requirements for a lock-free std::atomic<T> and sizeof(T) <= 8, then atomic
 * operations are also supported.
 *
 * @tparam T Object type.
 */
template <typename T>
class rptr {
  public:
    using object_type = T;

    explicit rptr(rdma::ReliableConnection &rc, uintptr_t remote_ptr, void *local_ptr)
        : rc(rc), remote_ptr(remote_ptr), local_ptr(reinterpret_cast<uint8_t *>(local_ptr))
    {
        valid = false;
    }

    T &operator*()
    {
        if (!valid || std::is_volatile<T>::value) {
            rc.post_read(local_ptr, remote_ptr, sizeof(T), true);
            rc.poll_send_cq();
            valid = true;
        }
        return *reinterpret_cast<T *>(local_ptr);
    }

    T *operator->() { return &(*this); }

    rptr<T> &operator=(uintptr_t remote_ptr)
    {
        if (remote_ptr != this->remote_ptr) {
            this->remote_ptr = remote_ptr;
            valid = false;
        }
    }

    void commit(bool notified = false) { commit(0, sizeof(T), notified); }

    void commit(size_t offset, size_t len, bool notified = false)
    {
        if (valid) {
            rc.post_write(remote_ptr + offset, local_ptr + offset, len, notified);
            if (notified)
                rc.poll_send_cq();
        }
    }

    inline rptr<T> &validate(bool _valid = true)
    {
        valid = _valid;
        return *this;
    }

    inline rptr<T> &invalidate() { return validate(false); }

    template <typename Tp>
    rptr<std::remove_pointer<Tp>> reinterpret_at(size_t offset)
    {
        return rptr<Tp>(rc, remote_ptr + offset, local_ptr + offset).validate(valid);
    }

  private:
    rdma::ReliableConnection &rc;
    uintptr_t remote_ptr;
    uint8_t *local_ptr;

    bool valid;
};

#define rptr_update(p, value) \
    do {                      \
        *(p) = (value);       \
        (p).commit();         \
    } while (false)

#define rptr_update_member(p, member, value)                                                  \
    do {                                                                                      \
        (p)->member = (value);                                                                \
        (p).commit(offsetof(typename decltype(p)::object_type, member), sizeof((p)->member)); \
    } while (false)

#endif  // __RPTR_H__
