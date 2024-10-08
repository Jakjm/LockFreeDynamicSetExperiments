#!/bin/bash
s=--augTrie	
	for numProcs in {1,2,3,4,5,6,8,16,32,64,128}
	do
		for t in {1..5}
		do	
			list=($(seq 0 2 $((2*numProcs - 1))))
			listStr=""
			for i in ${list[@]}
			do
				listStr="$listStr,$i"
			done
			listStr=${listStr:1} #Remove the first comma.
			taskset -c "$listStr" ./dynamicSetTest $s -n $numProcs --output results/evenDist.csv 2> errors.txt
		done
	done
