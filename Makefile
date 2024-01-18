#Header file dependencies of all data structures and of dynamicSetTest 
common = common.h debra.h 
#Header file dependencies of the other data structures 
dataStructures = OtherDynamicSets/FRList.h OtherDynamicSets/FRSkipList.h 
#Header files used for trie data structure
linkedLists = LinkedLists/ListNode.h LinkedLists/P_ALL.h LinkedLists/RU_ALL.h LinkedLists/UALL.h
trie = swCopy.h swCopy2.h BoundedMinReg/minreg.h trieNodeTypes.h $(linkedLists) trie.h 

dynamicSetTest : $(dataStructures) $(trie) $(common) dynamicSetTest.cpp  Makefile 
	g++ -g -O3 -Wall -fdump-lang-class -pthread -o dynamicSetTest dynamicSetTest.cpp
