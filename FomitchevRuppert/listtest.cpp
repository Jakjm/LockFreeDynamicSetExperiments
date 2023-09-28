#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <random>
#include "list_extension.h"
using std::cout;

//Produces a random integer between 0 and range (inclusive).
inline int randomNum(int range){
    //A random number seed that is local to this thread.
	thread_local static std::random_device seed;
	//Random number generator using seed.
	thread_local static std::mt19937 rng(seed());
	//Ask for a distribution from this random number generator.
	std::uniform_int_distribution<int> dist(0,range);
	return dist(rng);
}

class IntNode : public ListNode{
    public:
    int key;
    IntNode(int k): key(k){

    }

};

std::string intNodeToString(ListNode *node){
    IntNode *iNode = (IntNode*)node;
    return std::to_string(iNode->key); 
}
inline int __attribute__((always_inline)) compareInt(ListNode *node1, ListNode *node2) {
    IntNode *i1 = ((IntNode*)node1);
    IntNode *i2 = ((IntNode*)node2);
    return i1->key - i2->key;
}

void randomTask(LinkedList_FRE<compareInt> *list, ListNode **array, int arraySize, int testSize, int id){
    threadID(id);
    for(int i = 0;i < testSize;++i){
        int randomNode = randomNum(arraySize - 1);
        ListNode *node = array[randomNode];
        int randomChoice = randomNum(1);
        if(randomChoice == 1){
            list->insert(node);
        }
        else{
            list->remove(node);
        }

        if(i == (testSize / 2)){
            list->printList(intNodeToString);
        }
    }
    list->printList(intNodeToString);
}

void randomTest(){
    int arraySize = 8000;
    int testSize = 50000;
    IntNode *node[arraySize];
    //Allobreak ate nodes...
    for(int i = 0;i < arraySize; ++i){
        node[i] = new IntNode(i);
    }
    LinkedList_FRE<compareInt> list;
    std::thread t0(randomTask, &list, (ListNode**)node, arraySize, testSize, 0);
    std::thread t1(randomTask, &list, (ListNode**)node, arraySize, testSize, 1);
    std::thread t2(randomTask, &list, (ListNode**)node, arraySize, testSize, 2);
    std::thread t3(randomTask, &list, (ListNode**)node, arraySize, testSize, 3);
    t0.join();
    t1.join();
    t2.join();
    t3.join();
    //Free nodes
    for(int i = 0;i < arraySize;++i){
        delete node[i];
    }
}

void basicTest(){
    threadID(0);
    IntNode i1(1);
    IntNode i2(2);
    IntNode i3(3);
    IntNode i4(4);
    IntNode i5(5);

    LinkedList_FRE<compareInt> list;

    list.printList(intNodeToString);

    //Insert 1, 4, 2, 5, 3
    list.insert(&i1);
    list.printList(intNodeToString);

    list.insert(&i4);
    list.printList(intNodeToString);

    list.insert(&i2);
    list.printList(intNodeToString);

    list.insert(&i5);
    list.printList(intNodeToString);

    list.insert(&i3);
    list.printList(intNodeToString);

    //Remove 2, 5, 1, 3, 4

    list.remove(&i2);
    list.printList(intNodeToString);

    list.remove(&i5);
    list.printList(intNodeToString);

    list.remove(&i1);
    list.printList(intNodeToString);
    
    list.remove(&i3);
    list.printList(intNodeToString);

    list.remove(&i4);
    list.printList(intNodeToString);
}

int main(){
    basicTest();
    randomTest();
    return 0;
}
