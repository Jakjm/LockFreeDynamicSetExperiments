#Header file dependencies of all data structures and of dynamicSetTest 
common = common.h DynamicSets/debra.h DynamicSets/DynamicSet.h
#Header file dependencies of the other data structures 
dataStructures = DynamicSets/FR_List/FRList.h DynamicSets/FR_SkipList/FRSkipList.h DynamicSets/FR_Augmented_Trie/augmentedTrie.h
#Header files used for trie data structure
triePath=DynamicSets/Trie
linkedLists = $(triePath)/LinkedLists/PALL.h $(triePath)/LinkedLists/RUALL.h $(triePath)/LinkedLists/UALL.h
trie = $(triePath)/BoundedMinReg/minreg.h $(triePath)/trieNodeTypes.h $(linkedLists) $(triePath)/trie.h 

#sanitizer = -fsanitize=address
compileFlags = -Wall -pedantic -pthread -Werror -faligned-new -std=c++17
testingFlags = -fdump-lang-class -fno-omit-frame-pointer -g
#countersFlags = -D COUNT_CONTENTION
optimizationFlags  = -O3

dynamicSetTest : $(dataStructures) $(trie) $(common) dynamicSetTest.cpp  Makefile 
	g++ $(sanitizer) $(compileFlags) $(optimizationFlags) $(testingFlags) $(countersFlags) -o dynamicSetTest dynamicSetTest.cpp

seqTest : $(dataStructures) $(trie) $(common) sequentialTest.cpp Makefile
	g++ $(sanitizer) $(compileFlags) -g $(countersFlags) -o seqTest sequentialTest.cpp

clean: 
	rm dynamicSetTest
