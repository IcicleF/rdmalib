#!/bin/bash

# Usage:
# ./run.sh <test-app-name> <number-of-hosts>
#
# Example:
# ./run.sh hello 3

rm -f core
cd ../build; make -j; cd ../test;
mpirun --allow-run-as-root --mca btl tcp,self --hostfile ./hosts -n $2 ../build/bin/$1
