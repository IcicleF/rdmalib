#include <mpi.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "../rdma.h"

using namespace std;

const int CLIENT = 0, SERVER = 1;
const size_t MEM_SIZE = 1048576;

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    char *buf;
    int rc = posix_memalign(reinterpret_cast<void **>(&buf), 64, MEM_SIZE);
    if (rc) {
        return -1;
    }
    memset(buf, 0, MEM_SIZE);

    {
        rdma::Context ctx;
        ctx.reg_mr(buf, MEM_SIZE);

        rdma::Cluster cluster(ctx);
        cluster.establish();

        // Send to next
        int id = cluster.whoami();
        int n = cluster.size();

        if (n != 2) {
            fprintf(stderr, "error: cas-ordering must run with only 2 hosts\n");
            exit(-1);
        }

        if (id == CLIENT) {
            const int NTests = 100000;
            const int Batch = 64;

            auto &svr = cluster.peer(SERVER);
            auto [dst, buf_size] = svr.remote_mr(0);
            auto &rc = svr.rc(0);

            uint64_t *local = reinterpret_cast<uint64_t *>(buf);
            uint64_t cur = 0, check = 0;

            auto exp_start = std::chrono::steady_clock::now();
            for (int i = 0; i <= NTests; ++i) {
                // Post
                if (i < NTests) {
                    int offset = (i % 2) * Batch;
                    for (int j = 0; j < Batch; ++j) {
                        local[j + offset] = cur++;
                        rc.post_atomic_cas(dst, local + offset + j, cur, j + 1 == Batch, j);
                    }
                }

                // Poll
                if (i > 0) {
                    rc.poll_send_cq();

                    int offset = (1 - (i % 2)) * Batch;
                    for (int j = 0; j < Batch; ++j) {
                        if (local[j + offset] != check++)
                            fprintf(stderr, "order check failed (expected %lu, get %lu)\n",
                                    check - 1, local[j + offset]);
                    }
                }
            }
            auto exp_end = std::chrono::steady_clock::now();
            auto microsecs =
                std::chrono::duration_cast<std::chrono::microseconds>(exp_end - exp_start).count();
            fprintf(stderr, "cas: %.3lf op per sec\n",
                    1.0 * NTests * Batch / (1.0 * microsecs / 1e6));
        }

        cluster.sync();
    }

    free(buf);

    MPI_Finalize();
}