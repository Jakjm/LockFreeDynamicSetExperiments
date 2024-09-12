#!/bin/bash
s=--augTrie
for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
do
	for t in {1..5}
	do
		taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/evenDistHT.csv 2> errors.txt
	done
done
for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
do
	for t in {1..5}
	do
		taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 22 $s -n $numProcs --output results/evenBigHT.csv 2> errors.txt  
	done
done
for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
do
	for t in {1..5}
	do
		taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/queryHeavy.csv -O 1 1 4 4 2> errors.txt
	done
done
for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
do
	for t in {1..5}
	do
		taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/updateHeavy.csv -O 4 4 1 1 2> errors.txt
	done
done
