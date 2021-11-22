#include <cstdio>
#include <cstdlib>
#include <mpi.h>

#include "rdma_base.h"

namespace rdma
{

Cluster::Cluster(Context &ctx): connected(false)
{
    int rc;
    rc = MPI_Comm_size(MPI_COMM_WORLD, &this->n);
    if (rc != MPI_SUCCESS) {
        throw std::runtime_error("cannot get MPI_Comm_size");
    }
    
    rc = MPI_Comm_rank(MPI_COMM_WORLD, &this->id);
    if (rc != MPI_SUCCESS) {
        throw std::runtime_error("cannot get MPI_Comm_rank");
    }

    // Reference the RDMA context
    ctx.refcnt.fetch_add(1);
    this->ctx = &ctx;
}

Cluster::~Cluster()
{
    if (this->peers.size()) {
        for (int i = 0; i < this->n; ++i) {
            if (i == this->id)
                continue;
            if (this->peers[i])
                delete this->peers[i];
        }
        this->peers.clear();
    }

    // Dereference the RDMA context
    this->ctx->refcnt.fetch_sub(1);
}

void Cluster::connect(int connections, ConnectionType type)
{
    bool _connected = false;
    if (!this->connected.compare_exchange_strong(_connected, true))
        return;

    // Initiate peers vector
    this->peers.assign(this->n, nullptr);
    for (int i = 0; i < this->n; ++i) {
        if (i == this->id)
            continue;

        this->peers[i] = new Peer(*this, i, connections);
    }

    // Before proceeding, barrier to ensure all peers are ready
    MPI_Barrier(MPI_COMM_WORLD);

    // Collect QP data
    OOBExchange my_qp_info[Consts::MaxPeers], remote_qp_info[Consts::MaxPeers];
    memset(my_qp_info, 0, sizeof(my_qp_info));
    memset(remote_qp_info, 0, sizeof(remote_qp_info));
    for (int i = 0; i < this->n; ++i) {
        if (i == id)
            continue;
        
        my_qp_info[i].num_qps = connections;
        this->ctx->fill_exchange(&my_qp_info[i]);
        this->peers[i]->fill_exchange(&my_qp_info[i]);
    }

    // Exchange QP data
    MPI_Datatype XchgQPInfoTy;
    MPI_Type_contiguous(sizeof(OOBExchange), MPI_BYTE, &XchgQPInfoTy);
    MPI_Type_commit(&XchgQPInfoTy);

    int send_cnts[Consts::MaxPeers] = {0}, send_displs[Consts::MaxPeers] = {0};
    int recv_cnts[Consts::MaxPeers] = {0}, recv_displs[Consts::MaxPeers] = {0};
    for (int i = 0; i < this->n; ++i) {
        send_cnts[i] = recv_cnts[i] = 1;
        send_displs[i] = recv_displs[i] = i;
    }

    MPI_Alltoallv(my_qp_info, send_cnts, send_displs, XchgQPInfoTy,
        remote_qp_info, recv_cnts, recv_displs, XchgQPInfoTy, MPI_COMM_WORLD);

    // Establish RDMA connection
    for (int i = 0; i < this->n; ++i) {
        if (i == id)
            continue;
        
        this->peers[i]->establish(&remote_qp_info[i]);
    }

    // Now all connections has been established, barrier
    MPI_Barrier(MPI_COMM_WORLD);
}

void Cluster::sync()
{
    int rc = MPI_Barrier(MPI_COMM_WORLD);
    if (rc != MPI_SUCCESS)
        throw std::runtime_error("failed to sync");
    asm volatile ("" ::: "memory");
}

int Cluster::verbose() const
{
    fprintf(stderr, "[node %d] *** VERBOSE ***\n", this->id);
    for (int i = 0; i < this->n; ++i) {
        if (i == this->id)
            continue;
        
        int rc = this->peers[i]->verbose();
        if (rc) {
            fprintf(stderr, "[node %d] *** VERBOSE: halt, detected issue ***\n", this->id);
            return rc;
        }
    }
    return 0;
}

}
