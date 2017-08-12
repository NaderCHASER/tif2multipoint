#!/bin/bash

g++ -g -O3 -L/usr/lib/x86_64-linux-gnu/ -I/usr/include/geotiff Tif2MultiPoint.cpp TifGrid.cpp BoundingBox.cpp -o tif2multipoint -ltiff -lgeotiff -lz
