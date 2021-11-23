#include <mpi.h>
#include <cstdio>
#include <cstdlib>

#include "../rdma.h"

using namespace std;

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    char *buf;
    int rc = posix_memalign(reinterpret_cast<void **>(&buf), 64, 1024);
    if (rc) {
        return -1;
    }
    memset(buf, 0, 1024);

    {
        rdma::Context ctx;
        ctx.reg_mr(buf, 1024);

        rdma::Cluster cluster(ctx);
        cluster.connect();

        // Send to next
        int id = cluster.whoami();
        int n = cluster.size();
        int next_id = (id + 1) % n;

        auto &next = cluster.peer(next_id);
        auto [dst, dst_size] = next.remote_mr(0);
        auto &conn = next.connection(0);

        int nc = sprintf(buf, "hello from %d", id);
        conn.post_write(dst + 64, buf, strlen(buf), true);
        conn.poll_send_cq();

        cluster.sync();

        // Print what I get
        printf("%d: %s\n", id, buf + 64);
    }

    free(buf);

    MPI_Finalize();
}