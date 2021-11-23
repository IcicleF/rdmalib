#include <cstdio>
#include <cstdlib>

#include "cluster.h"
#include "context.h"
#include "peer.h"
#include "rc/rc.h"

namespace rdma {

Peer::Peer(Cluster &cluster, int id, ConnectionConfig config) {
    this->ctx = cluster.ctx;
    this->ctx->refcnt.fetch_add(1);

    this->cluster = &cluster;
    this->id = id;

    // Initiate connections
    int connections = config.num_rc;
    this->nconns = connections;
    this->rcs.assign(connections, nullptr);
    for (int i = 0; i < connections; ++i) {
        this->rcs[i] = new ReliableConnection(*this, i);
    }
}

Peer::~Peer() {
    for (int i = 0; i < this->nconns; ++i) {
        delete this->rcs[i];
    }
    this->rcs.clear();

    // Dereference the RDMA context
    this->ctx->refcnt.fetch_sub(1);
}

void Peer::fill_exchange(OOBExchange *xchg) {
    for (int i = 0; i < this->nconns; ++i) this->rcs[i]->fill_exchange(xchg);
}

void Peer::establish(OOBExchange *remote_xchg) {
    this->nrmrs = remote_xchg->num_mrs;
    for (int i = 0; i < remote_xchg->num_mrs; ++i) this->remote_mrs[i] = remote_xchg->mr[i];
    for (int i = 0; i < this->nconns; ++i)
        this->rcs[i]->establish(remote_xchg->gid, remote_xchg->lid, remote_xchg->qpn[i]);
}

int Peer::verbose() const {
    fprintf(stderr, " [node %d] peer %d:\n", this->cluster->whoami(), this->id);
    for (int i = 0; i < this->nconns; ++i) {
        int rc = this->rcs[i]->verbose();
        if (rc) {
            fprintf(stderr, "[node %d, peer %d] peer level halt\n", this->cluster->whoami(),
                    this->id);
            return rc;
        }
    }
    return 0;
}

}  // namespace rdma
