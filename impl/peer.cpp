#include <cstdio>
#include <cstdlib>
#include "rdma_base.h"

namespace rdma
{

Peer::Peer(Cluster &cluster, int id, int connections)
{
    this->ctx = cluster.ctx;
    this->ctx->refcnt.fetch_add(1);
    
    this->cluster = &cluster;
    this->id = id;

    // Initiate connections
    this->nconns = connections;
    this->conns.assign(connections, nullptr);
    for (int i = 0; i < connections; ++i) {
        this->conns[i] = new Connection(*this, i);
    }
}

Peer::~Peer()
{
    for (int i = 0; i < this->nconns; ++i) {
        delete this->conns[i];
    }
    this->conns.clear();

    // Dereference the RDMA context
    this->ctx->refcnt.fetch_sub(1);
}

void Peer::fill_exchange(OOBExchange *xchg)
{
    for (int i = 0; i < this->nconns; ++i)
        this->conns[i]->fill_exchange(xchg);
}

void Peer::establish(OOBExchange *remote_xchg)
{
    this->nrmrs = remote_xchg->num_mrs;
    for (int i = 0; i < remote_xchg->num_mrs; ++i)
        this->remote_mrs[i] = remote_xchg->mr[i];
    for (int i = 0; i < this->nconns; ++i)
        this->conns[i]->establish(remote_xchg->gid, remote_xchg->lid, remote_xchg->qpn[i]);
}

int Peer::verbose() const
{
    fprintf(stderr, " [node %d] peer %d:\n", this->cluster->whoami(), this->id);
    for (int i = 0; i < this->nconns; ++i) {
        int rc = this->conns[i]->verbose();
        if (rc) {
            fprintf(stderr, "[node %d, peer %d] peer level halt\n", this->cluster->whoami(), this->id);
            return rc;
        }   
    }
    return 0;
}

}
