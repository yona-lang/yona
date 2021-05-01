#!/usr/bin/env bash

PORT=55555
REPETITIONS=50

../../yona -f server.yona $PORT $REPETITIONS > server.output 2> server.output &
pids[0]=$!

for i in $(seq 1 $REPETITIONS); do
  ../../yona -f client.yona $PORT > client.output 2> client.output & pids[${i}]=$!
done


# wait for all pids
for pid in ${pids[*]}; do
    wait $pid
done

[ -s server.output ] && exit 1
[ -s client.output ] && exit 1

rm server.output client.output
