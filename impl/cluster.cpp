#include <mpi.h>
#include <cstdio>
#include <cstdlib>

#include "cluster.h"
#include "context.h"
#include "peer.h"

namespace rdma {

Cluster::Cluster(Context &ctx) : connected(false)
{
    int rc;
    rc = MPI_Comm_size(MPI_COMM_WORLD, &this->n);
    if (rc != MPI_SUCCESS) {
        Emergency::abort("cannot get MPI_Comm_size");
    }

    rc = MPI_Comm_rank(MPI_COMM_WORLD, &this->id);
    if (rc != MPI_SUCCESS) {
        Emergency::abort("cannot get MPI_Comm_rank");
    }

    // Reference the RDMA context
    ctx.refcnt.fetch_add(1);
    this->ctx = &ctx;

    // Initiate peers vector
    this->peers.assign(this->n, nullptr);
    for (int i = 0; i < this->n; ++i) {
        if (i == this->id)
            continue;
        this->peers[i] = new Peer(*this, i);
    }
}

Cluster::~Cluster()
{
    for (int i = 0; i < this->n; ++i) {
        if (this->peers[i])
            delete this->peers[i];
    }
    this->peers.clear();

    // Dereference the RDMA context
    this->ctx->refcnt.fetch_sub(1);
}

void Cluster::establish(int num_rc, int num_xrc)
{
    // Check validity
    if (num_rc < 0 || num_xrc < 0 || (num_rc == 0 && num_xrc == 0)) {
        Emergency::abort("no connections to establush");
    }

    // Allow only once
    bool _connected = false;
    if (!this->connected.compare_exchange_strong(_connected, true))
        return;

    // Before proceeding, barrier to ensure all peers are ready
    MPI_Barrier(MPI_COMM_WORLD);

    // Establish connection
    for (int i = 0; i < this->n; ++i) {
        if (i == this->id)
            continue;
        this->peers[i]->establish(num_rc, num_xrc);
    }

    // Now all connections has been established, barrier
    MPI_Barrier(MPI_COMM_WORLD);
}

void Cluster::sync()
{
    int rc = MPI_Barrier(MPI_COMM_WORLD);
    if (rc != MPI_SUCCESS)
        Emergency::abort("failed to sync");
    asm volatile("" ::: "memory");
}

}  // namespace rdma
