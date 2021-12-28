#include <fcntl.h>
#include <cstdio>
#include <cstdlib>

#include "context.h"

namespace rdma {

Context::Context(char const *dev_name) : nmrs(0), refcnt(0)
{
    int n_devices;
    ibv_device **dev_list = ibv_get_device_list(&n_devices);
    if (!n_devices || !dev_list)
        Emergency::abort("cannot find any RDMA device");

    int target = -1;
    if (dev_name == nullptr)
        target = 0;
    else {
        for (int i = 0; i < n_devices; ++i)
            if (!strcmp(ibv_get_device_name(dev_list[i]), dev_name)) {
                target = i;
                break;
            }
    }
    if (target < 0)
        Emergency::abort("cannot find device: " + std::string(dev_name));

    ibv_context *ctx = ibv_open_device(dev_list[target]);
    ibv_free_device_list(dev_list);

    this->ctx = ctx;
    this->check_dev_attr();
    ibv_query_port(this->ctx, 1, &this->port_attr);
    ibv_query_gid(this->ctx, 1, 1, &this->gid);

    // Protected Domain
    this->pd = ibv_alloc_pd(ctx);
}

Context::~Context()
{
    if (refcnt.load() > 0) {
        fprintf(stderr, "destructing RDMA context with dependency!\n");
        return;
    }

    // MR -> XRCD -> PD -> Context
    for (int i = 0; i < this->nmrs; ++i)
        ibv_dereg_mr(this->mrs[i]);
    ibv_dealloc_pd(this->pd);
    ibv_close_device(this->ctx);
}

int Context::reg_mr(void *addr, size_t size, int perm)
{
    if (this->nmrs >= Consts::MaxMrs)
        return -1;

    ibv_mr *mr = ibv_reg_mr(this->pd, addr, size, perm);
    if (mr == nullptr)
        return -1;

    this->mrs[this->nmrs] = mr;
    return this->nmrs++;
}

int Context::reg_mr(uintptr_t addr, size_t size, int perm)
{
    return this->reg_mr(reinterpret_cast<void *>(addr), size, perm);
}

void Context::check_dev_attr()
{
    // No-op
}

}  // namespace rdma
