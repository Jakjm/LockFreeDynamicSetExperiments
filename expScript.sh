#!/bin/bash
for s in {--trie,--skip,--augTrie}
do
	for numProcs in {1,2,3,4,5,6,8,16,32,64}
	do
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/bigUniverseHT.csv -k 23
		done
	done
done
