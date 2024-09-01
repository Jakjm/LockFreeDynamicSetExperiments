#!/bin/bash
for s in {--trie,--skip,--augTrie}
do
	for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
	do
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/queryHeavy.csv -O 1 1 4 4  
		done
	done
done
