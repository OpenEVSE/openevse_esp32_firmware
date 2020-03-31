#!/bin/bash

mkdir -p output

./divert_sim < data/almostperfect.csv > output/almostperfect.csv
./divert_sim < data/CloudyMorning.csv > output/CloudyMorning.csv
./divert_sim < data/day1.csv > output/day1.csv
./divert_sim < data/day2.csv > output/day2.csv
./divert_sim < data/day3.csv > output/day3.csv
./divert_sim -g 2 < data/day1_grid_ie.csv > output/day1_grid_ie.csv
./divert_sim -g 2 < data/day2_grid_ie.csv > output/day2_grid_ie.csv
./divert_sim -g 2 < data/day3_grid_ie.csv > output/day3_grid_ie.csv
