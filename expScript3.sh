#!/bin/bash
		for i in {1,2,3}
		do	
			taskset -c 0-$((numProcs-1)) ./dynamicSetTest $s -n $numProcs
		done
