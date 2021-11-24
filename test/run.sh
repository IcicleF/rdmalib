#!/bin/bash
rm -f core
cd ../build; make -j; cd ../test;
mpirun --allow-run-as-root --mca btl tcp,self --hostfile ./hosts -n 3 ../build/bin/hello
