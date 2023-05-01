#!/bin/bash

PASS=0
FAIL=0

function test
{
  file=$1
  shift
  echo "Testing $file"
  ./divert_sim $@ < data/$file.csv > output/$file.csv
  diff -Z --side-by-side --suppress-common-lines snapshot/$file.csv output/$file.csv
  if [ $? -eq 0 ]; then
    echo "Test $file passed"
    ((PASS+=1))
  else
    echo "Test $file failed"
    ((FAIL+=1))
  fi
}

mkdir -p output

test almostperfect
test CloudyMorning
test day1
test day2
test day3
test day1_grid_ie -g 2
test day2_grid_ie -g 2
test day3_grid_ie -g 2
test solar-vrms -v 2
test Energy_and_Power_Day_2020-03-22 --sep \; --kw --config '{"divert_decay_smoothing_factor":0.4}'
test Energy_and_Power_Day_2020-03-31 --sep \; --kw --config '{"divert_decay_smoothing_factor":0.4}'
test Energy_and_Power_Day_2020-04-01 --sep \; --kw --config '{"divert_decay_smoothing_factor":0.4}'
echo Passed $PASS tests, failed $FAIL tests
exit $FAIL
