#Header file dependencies of all data structures and of dynamicSetTest 
common = common.h debra.h 
#Header file dependencies of the other data structures 
dataStructures = OtherDynamicSets/FRList.h OtherDynamicSets/FRSkipList.h 
#Header files used for trie data structure
linkedLists = LinkedLists/ListNode.h LinkedLists/P_ALL.h LinkedLists/RU_ALL_notifDesc.h LinkedLists/RU_ALL_swCopy.h LinkedLists/UALL.h
trie = swCopy.h swCopy2.h BoundedMinReg/minreg.h trieNodeTypes.h $(linkedLists) trie.h 

compileFlags = -Wall -pedantic -pthread -Werror
optimizationFlags = -O3
testingFlags = -fdump-lang-class -fno-omit-frame-pointer -g

dynamicSetTest : $(dataStructures) $(trie) $(common) dynamicSetTest.cpp  Makefile 
	g++ $(compileFlags) $(optimizationFlags) $(testingFlags) -o dynamicSetTest dynamicSetTest.cpp

clean: 
	rm dynamicSetTest
