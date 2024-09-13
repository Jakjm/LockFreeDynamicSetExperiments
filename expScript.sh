#!/bin/bash
for s in {--trie,--skip,--augTrie}
do
	for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
	do
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/insertHeavy.csv -O 8 2 4 4 2> errors.txt
		done
	done
	for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
	do
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/removeHeavy.csv -O 2 8 4 4 2> errors.txt
		done
	done
done
