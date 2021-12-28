#include <cstdio>
#include <cstdlib>

#include "cluster.h"
#include "context.h"
#include "peer.h"
#include "rc/rc.h"

namespace rdma {

Peer::Peer(Cluster &cluster, int id)
{
    this->ctx = cluster.ctx;
    this->ctx->refcnt.fetch_add(1);

    this->cluster = &cluster;
    this->id = id;
}

Peer::~Peer()
{
    if (this->rcs.size()) {
        for (size_t i = 0; i < this->rcs.size(); ++i)
            delete this->rcs[i];
        this->rcs.clear();
    }

    // Dereference the RDMA context
    this->ctx->refcnt.fetch_sub(1);
}

void Peer::establish(int num_rc)
{
    // Initiate connections
    this->rcs.assign(num_rc, nullptr);
    for (int i = 0; i < num_rc; ++i)
        this->rcs[i] = new ReliableConnection(*this, i);

    // Prepare out-of-band connection metadata
    MPI_Datatype XchgQPInfoTy;
    MPI_Type_contiguous(sizeof(OOBExchange), MPI_BYTE, &XchgQPInfoTy);
    MPI_Type_commit(&XchgQPInfoTy);

    OOBExchange xchg, remote_xchg;
    xchg.gid = this->ctx->gid;
    xchg.lid = this->ctx->port_attr.lid;
    xchg.num_mr = this->ctx->nmrs;
    for (int i = 0; i < this->ctx->nmrs; ++i)
        xchg.mr[i] = *(this->ctx->mrs[i]);

    xchg.num_rc = num_rc;
    for (int i = 0; i < num_rc; ++i)
        this->rcs[i]->fill_exchange(&xchg);

    // Exchange connection metadata
    MPI_Status mpirc;
    int rc = MPI_Sendrecv(&xchg, 1, XchgQPInfoTy, this->id, 0, &remote_xchg, 1, XchgQPInfoTy,
                          this->id, 0, MPI_COMM_WORLD, &mpirc);
    if (rc != MPI_SUCCESS)
        Emergency::abort("cannot perform MPI_Sendrecv with peer " + std::to_string(this->id));

    // Store remote MR
    this->nrmrs = remote_xchg.num_mr;
    for (int i = 0; i < this->nrmrs; ++i)
        this->remote_mrs[i] = remote_xchg.mr[i];

    // Connect
    for (int i = 0; i < num_rc; ++i)
        this->rcs[i]->establish(remote_xchg.gid, remote_xchg.lid, remote_xchg.rc_qp_num[i]);
}

}  // namespace rdma
