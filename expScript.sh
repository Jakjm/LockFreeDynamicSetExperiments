#!/bin/bash
for s in {--trie,--skip,--augTrie}
do
	#First experiment:  Even
	#Even distribution, 2^20 keys, procs pinned to first numProcs processes
	for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
	do
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/evenDist.csv -O 1 1 1 1 &> errors.txt
		done
	done
	#Second: Even no HT
	#Even distribution, 2^20 keys, procs pinned to first numProcs even hardware threads
	for numProcs in {1,2,3,4,5,6,8,16,32,64,128}
	do
		threads=
		for i in $(eval echo {0..$((2*numProcs-1))..2})
		do
			threads=$threads,$i
		done
		threads=${threads:1}
		for t in {1..5}
		do
			taskset -c "$threads" ./dynamicSetTest -k 20 $s -n $numProcs --output results/evenDist_no_HT.csv -O 1 1 1 1 &> errors.txt
		done
	done
	for numProcs in {1,2,3,4,5,6,8,16,32,64,128,256}
	do
		#Third: Update heavy, 4, 4, 1, 1
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/updateHeavy.csv -O 4 4 1 1 &> errors.txt
		done
		#Insert heavy, 4 1 2 2 
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/insertHeavy.csv -O 4 1 2 2 &> errors.txt
		done
		#Remove heavy 1 4 2 2 
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/removeHeavy.csv -O 1 4 2 2 &> errors.txt
		done
		#Query heavy 1 1 4 4
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/queryHeavy.csv -O 1 1 4 4 &> errors.txt
		done
		#Search heavy 1 1 8 0 
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/searchHeavy.csv -O 1 1 8 0 &> errors.txt
		done
		#Pred heavy 1 1 0 8 
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 20 $s -n $numProcs --output results/predHeavy.csv -O 1 1 0 8 &> errors.txt 
		done 
		#Even dist, bigger set
		for t in {1..5}
		do
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest -k 22 $s -n $numProcs --output results/evenDistBig.csv -O 1 1 1 1 &> errors.txt
		done
	done 
done

