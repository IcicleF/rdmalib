#!/bin/bash
rm -f core
cd ../build; make -j; cd ../test;
mpirun --allow-run-as-root --mca btl_openib_warn_no_device_params_found 0 --hostfile ./hosts -n 3 ../build/bin/hello
