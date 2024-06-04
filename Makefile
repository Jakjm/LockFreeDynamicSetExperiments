#Header file dependencies of all data structures and of dynamicSetTest 
common = common.h DynamicSets/debra.h 
#Header file dependencies of the other data structures 
dataStructures = DynamicSets/FR_List/FRList.h DynamicSets/FR_SkipList/FRSkipList.h 
#Header files used for trie data structure
triePath=DynamicSets/Trie
linkedLists = $(triePath)/LinkedLists/PALL.h $(triePath)/LinkedLists/RUALL_notifDesc.h $(triePath)/LinkedLists/RUALL_swCopy.h $(triePath)/LinkedLists/UALL.h
trie = $(triePath)/BoundedMinReg/minreg.h $(triePath)/trieNodeTypes.h $(linkedLists) $(triePath)/trie.h 

compileFlags = -Wall -pedantic -pthread -Werror
optimizationFlags = -O3
testingFlags = -fdump-lang-class -fno-omit-frame-pointer -g
countersFlags = -D COUNT_CONTENTION

dynamicSetTest : $(dataStructures) $(trie) $(common) dynamicSetTest.cpp  Makefile 
	g++ $(compileFlags) $(optimizationFlags) $(testingFlags) $(countersFlags) -o dynamicSetTest dynamicSetTest.cpp

clean: 
	rm dynamicSetTest
