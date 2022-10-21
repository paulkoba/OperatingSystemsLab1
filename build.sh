#!/bin/bash
gcc main.c -L. ./liblab1.so -o executable -Ofast -Wall
LD_LIBRARY_PATH=. ./executable
