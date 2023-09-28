#include <cstdint>
#include <stdint.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iterator>
#include <atomic>
#include <cassert>
#include "../setbench/common/recordmgr/record_manager.h"
#include "../common.h"
#pragma once
using std::string;



//Eric Ruppert and Michhail Fomitchev's Lock-Free Linked List
//From their paper: Lock-Free Linked Lists and Skip Lists 2004
uint64_t MARK_MASK = 0x2; //62 0s, then 10 = 0000 0000 0000 0002
uint64_t FLAG_MASK = 0x1; //63 0s, then 1  = 0000 0000 0000 0001
uint64_t CLEAN_MASK = -4; //62 1s, 2 0s    = FFFF FFFF FFFF FFFC


/**
* ListNode class
*/
template <typename Key, bool padding=true>
class ListNode{
    public:
        Key key;
        std::atomic<ListNode<Key, padding> *> successor; //Contains <right, mark, flag> 
        std::atomic<ListNode<Key, padding> *> backlink;
        ListNode(Key k, ListNode<Key, padding> *next) : key(k), successor(next) {

        }
        ListNode(Key k) : ListNode(k, nullptr){

        }
        ListNode<Key, padding> *right(){
            ListNode<Key, padding> *succ = successor;
            succ = (ListNode<Key, padding> *)((uintptr_t)succ & CLEAN_MASK);
            return succ;
        }
        int mark(){
            ListNode<Key, padding> *succ = successor;
            return (int)((uintptr_t)succ & MARK_MASK);
        }
        int flag(){
            ListNode<Key, padding> *succ = successor;
            return (int)((uintptr_t)succ & FLAG_MASK);
        }
        //TODO ifdef 
        std::string toString(std::string(*keyToStr)(Key)){
            std::string nodeString = keyToStr(key);
            if(mark())nodeString = nodeString + ", M";
            else if(flag())nodeString = nodeString + ", F";
            return nodeString;
        }
        //TODO ifdef 
        ~ListNode(){}
};

/**
* ListNode class that includes some extra space to ensure the ListNode fills an entire 64 byte cache line.
*/
template <typename Key>
class ListNode<Key, true>{
    public:
        Key key;
        std::atomic<ListNode<Key, true> *> successor; //Contains <right, mark, flag> 
        std::atomic<ListNode<Key, true> *> backlink;
    private:
        char padd[64 - sizeof(Key) - 2*sizeof(std::atomic<ListNode<Key, true> *>)];
    public:
        ListNode(Key k, ListNode<Key, true> *next) : key(k), successor(next){}
        ListNode(Key k) : ListNode(k, nullptr){}
        ListNode<Key, true> *right(){
            ListNode<Key, true> *succ = successor;
            succ = (ListNode<Key, true> *)((uintptr_t)succ & CLEAN_MASK);
            return succ;
        }
        //Return whether the node is marked.
        int mark(){
            ListNode<Key, true> *succ = successor;
            return (int)((uintptr_t)succ & MARK_MASK);
        }
        //Return whether the node is flagged.
        int flag(){
            ListNode<Key, true> *succ = successor;
            return (int)((uintptr_t)succ & FLAG_MASK);
        }
        //TODO ifdef 
        std::string toString(std::string(*keyToStr)(Key)){
            std::string nodeString = keyToStr(key);
            if(mark())nodeString = nodeString + ", M";
            else if(flag())nodeString = nodeString + ", F";
            return nodeString;
        }
        //TODO ifdef 
        ~ListNode(){}
};
template <typename Key, int(*compare)(Key, Key)>
class SortedList{
    public:
        virtual bool insert(Key k) = 0;
        virtual bool removeKey(Key k) = 0;
};
//Class for a linearizable lock-free sorted linked list based on the PODC Paper by Mikhail Fomitchev and Eric Ruppert
//Key is the type of keys stored inside the list.
//compare is the function used to compare the keys of the linked list, in order to sort the list.
//If "padding" is true, the nodes of the linked list are 64 bytes wide. Otherwise, the nodes will be sizeof(Key) + 2*sizeof(ptr) bytes wide.
template <typename Key, int(*compare)(Key, Key), bool padding=true>
class LinkedList : public SortedList<Key, compare> {
    typedef ListNode<Key, padding> ListNode_t; 
    typedef record_manager<reclaimer_debra<Key>, allocator_new<Key>, pool_none<Key>, ListNode_t> recordMgr_t;
    public:
        ListNode_t tail, head; //Head, tail of the linked list. 
        recordMgr_t *const recordMgr;
        const bool ownMgr; // If true, must delete record manager upon exit.
        //Head has -inf as a key, tail has + inf as a key.
    public:
        LinkedList(Key negativeInf, Key positiveInf, recordMgr_t *mgr=nullptr) : tail(positiveInf), head(negativeInf, &tail), recordMgr(mgr ? mgr : new recordMgr_t(NUM_THREADS)), ownMgr(mgr == nullptr){
        
        }
        void clearList(){
            recordMgr->initThread(threadID());
            Iterator it = begin();
            while(it != this->end()){
                ListNode_t *node = &*it;
                recordMgr->retire(threadID(), node);
                ++it;
            }
        }
        ~LinkedList(){ 
            if(ownMgr){
                //Clear the list and uninitialize any threads that were initialized.
                clearList();
                //Deinitialize all threads.
                for(int i = 0;i < NUM_THREADS;++i){
                    recordMgr->deinitThread(i);
                }
                delete recordMgr;
            }
        }
        //Iterator class used to traverse the list. 
        //It may delete nodes as it traverses.
        struct Iterator{
            using iterator_category     =   std::forward_iterator_tag;
            using difference_type       =   std::ptrdiff_t;
            using value_type            =   ListNode_t;
            using pointer               =   ListNode_t*;
            using reference             =   ListNode_t&;
            private:
                ListNode_t *ptr;
                LinkedList<Key, compare, padding> *list;
                const bool guard;
            public:
                Iterator( ListNode_t *start, LinkedList<Key, compare, padding> *l, bool startRecordMgrOp) : ptr(start), list(l), guard(startRecordMgrOp){
                    if(list->ownMgr && guard){
                        //std::cout << "Getting guard " << list->recordMgr << "\n";
                        list->recordMgr->startOp(threadID());
                    }
                }
                ~Iterator(){
                    if(list->ownMgr && guard){
                        //std::cout << "Closing guard " << list->recordMgr << "\n";
                        list->recordMgr->endOp(threadID());
                    }
                }

                // Prefix increment
                // Continue to next unmarked element with key >= ptr.key
                // Does nothing if ptr = tail.
                Iterator& operator++() {
                    ListNode_t *next = ptr->right(), *cleanNext;
                    if(!next)return *this;
                    Key key = ptr->key;
                    do{
                        next = ptr->successor;
                        while(((uintptr_t)next & MARK_MASK) || ((uintptr_t)next & FLAG_MASK)){
                            if((uintptr_t)next & MARK_MASK){
                                ptr = ptr->backlink;
                                next = ptr->successor;
                            }
                            else if((uintptr_t)next & FLAG_MASK){
                                cleanNext = (ListNode_t*)((uintptr_t)next & CLEAN_MASK);
                                list->helpFlagged(ptr, cleanNext);
                                next = ptr->successor;
                            }
                        }
                        ptr = next;
                    }while(ptr != &list->tail && compare(ptr->key, key) < 0); 
                    //Continue onto next node while is less than the initial key and pointer is not the tail.
                    
                    return *this;
                }  
                friend bool operator== (const Iterator& a, const Iterator& b) { return a.ptr == b.ptr; }
                friend bool operator!= (const Iterator& a, const Iterator& b) { return a.ptr != b.ptr; }
                //friend Iterator operator= (Iterator&a, const Iterator& b){a.ptr = b.ptr;};
                reference operator*() const { return *ptr; }
        };
        void tryFlag(ListNode_t *prevNode, ListNode_t *targetNode, ListNode_t **output1, bool &output2){ //Returns a node and a boolean
            ListNode_t *expected, *cleanTarget, *delNode;
            bool result;
            cleanTarget = (ListNode_t *)((uintptr_t)targetNode & CLEAN_MASK); //targetNode with mark and flag bits zeroed
            while(true){
                if(prevNode->successor == (ListNode_t *)((uintptr_t)cleanTarget | FLAG_MASK)){
                    //return <prevNode, false>
                    *output1 = prevNode;
                    output2 = false;
                    return;
                }
                expected = cleanTarget;
                result = prevNode->successor.compare_exchange_strong(expected, (ListNode_t *)((uintptr_t)cleanTarget | FLAG_MASK));
                if(result){
                    //return <prevNode, true>
                    *output1 = prevNode;
                    output2 = true;
                    return;
                }
                if(expected == (ListNode_t *)((uintptr_t)cleanTarget | FLAG_MASK)){
                    //return <prevNode, false>
                    *output1 = prevNode;
                    output2 = false;
                    return;
                }

                while(prevNode->mark()){
                    prevNode = prevNode->backlink;
                }
                searchFrom2(targetNode->key, prevNode, &prevNode, &delNode);
                if(delNode != cleanTarget){
                    //return <null, false>
                    *output1 = nullptr;
                    output2 = false;
                    return;
                }
            }
        }
        void tryFlag2(ListNode_t *prevNode, ListNode_t *targetNode, ListNode_t **output1, bool &output2){
            ListNode_t *expected, *cleanTarget, *delNode;
            bool result;
            cleanTarget = (ListNode_t *)((uintptr_t)targetNode & CLEAN_MASK); //targetNode with mark and flag bits zeroed
            while(true){
                if(prevNode->successor == (ListNode_t *)((uintptr_t)cleanTarget | FLAG_MASK)){
                    //return <prevNode, false>
                    *output1 = prevNode;
                    output2 = false;
                    return;
                }
                expected = cleanTarget;
                result = prevNode->successor.compare_exchange_strong( expected, (ListNode_t *)((uintptr_t)cleanTarget | FLAG_MASK));
                if(result){
                    //return <prevNode, true>
                    *output1 = prevNode;
                    output2 = true;
                    return;
                }
                if(expected == (ListNode_t *)((uintptr_t)cleanTarget | FLAG_MASK)){
                    //return <prevNode, false>
                    *output1 = prevNode;
                    output2 = false;
                    return;
                }

                while(prevNode->mark()){
                    prevNode = prevNode->backlink;
                }
                searchKey(targetNode->key, prevNode, &prevNode, &delNode);
                if(delNode != cleanTarget){
                    //return <null, false>
                    *output1 = nullptr;
                    output2 = false;
                    return;
                }
            }
        }
        void tryMark(ListNode_t *delNode){
            ListNode_t *expected, *cleanResult;
            do{
                //Attempt to mark delNode.
                expected = delNode->right();
                //TODO replace with compare_exchange_weak??
                delNode->successor.compare_exchange_strong(expected, (ListNode_t*)((uintptr_t)expected | MARK_MASK));

                //If the result was flagged....
                if(((uintptr_t)expected) & FLAG_MASK){
                    cleanResult = (ListNode_t*)((uintptr_t)expected & CLEAN_MASK);
                    helpFlagged(delNode, cleanResult);
                }
            }while(delNode->mark() == 0); //While the delNode's successor is not marked.
        }
        void helpMarked(ListNode_t *prevNode, ListNode_t *delNode){
            ListNode_t *nextNode = delNode->right();
            ListNode_t *flaggedDelNode = (ListNode_t *)((uintptr_t)delNode | FLAG_MASK);
            bool success = prevNode->successor.compare_exchange_strong(flaggedDelNode, nextNode);
            if(success)recordMgr->retire(threadID(), delNode); //If this was the process that freed the memory, retire the record.
        }
        void helpFlagged(ListNode_t *prevNode, ListNode_t *delNode){
            delNode->backlink = prevNode;
            if(delNode->mark() == 0){
                tryMark(delNode);
            }
            helpMarked(prevNode, delNode);
        }
        /**
        * Traverses the list starting from currNode.
        * @param k, the key to be searched
        * @param currNode, the node to start from
        * @returns n1, n2
        *
        * Postcondition: 
        *   
        * 1) then compare(n1.key, k) <= 0 and compare(k, n2.key) < 0, OR compare(n1.key, k) <= 0 *and* k = n2.key 
        * 2) there exists a time during the execution of the function where n1.right = n2
        * 3) if currNode was unmarked at some time T' before SearchFrom invoked, 
        * there exists time T after T' but before searchFrom returns such that n1 is unmarked and n1.right = n2.
        *
        * Not linearizable...
        */
        void searchFrom(Key k, ListNode_t *currNode, ListNode_t **output1, ListNode_t **output2){
            ListNode_t *nextNode = currNode;
            nextNode = currNode->right();
            while(currNode->key != k && nextNode != &tail && compare(nextNode->key, k) <= 0){
                while(nextNode->mark() && (currNode->mark() == 0 || currNode->right() != nextNode)){
                    if(currNode->right() == nextNode){
                        helpMarked(currNode, nextNode);
                    }
                    nextNode = currNode->right();
                }
                //Continue on to next node if nextNode's key <= k
                if(nextNode != &tail && compare(nextNode->key, k) <= 0){
                    currNode = nextNode;
                    nextNode = nextNode->right();
                }
            }
            
            //return <currNode, nextNode>
            *output1 = currNode;
            *output2 = nextNode;
        }
        /**
        * Traverses the list starting from currNode.
        * @param k, the key to be searched
        * @param currNode, the node to start from
        * @returns n1, n2
        *
        * Postcondition: if compare(currNode.key, k) <= 0, 
        * 1) then n1.key < k <= n2.key, 
        * 2) there exists a time during the execution of the function where n1.right = n2
        * 3) if currNode was unmarked at some time T' before SearchFrom2 invoked, 
        * there exists time T after T' but before searchFrom returns such that n1 is unmarked and n1.right = n2.
        */
        void searchFrom2(Key k, ListNode_t *currNode, ListNode_t **output1, ListNode_t **output2){
            ListNode_t *nextNode = currNode;
            nextNode = currNode->right();
            //Continue on to next node if nextNode's key < k
            while(nextNode != &tail && compare(nextNode->key, k) < 0){
                //While the next node is marked AND either currNode is unmarked or currNode's right is not nextNode->right
                while(nextNode->mark() && (currNode->mark() == 0 || currNode->right() != nextNode)){
                    if(currNode->right() == nextNode){
                        helpMarked(currNode, nextNode);
                    }
                    nextNode = currNode->right();
                }
                //Continue on to next node if nextNode's key < k
                if(nextNode != &tail && compare(nextNode->key, k) < 0){
                    currNode = nextNode;
                    nextNode = nextNode->right();
                }
            }
            
            //return <currNode, nextNode>
            *output1 = currNode;
            *output2 = nextNode;
        }
        /**
        * Inserts a node into the sorted list.
        * @param k, the key to be associated with the node
        */
        bool insert(Key k){
            if(ownMgr){
                threadInit();
                recordMgr->startOp(threadID());
            }

            ListNode_t *prevNode, *nextNode, *newNode;
            ListNode_t *prevSucc, *expected;

            searchFrom(k, &head, &prevNode, &nextNode);

            //At some time during function, prevNode->right = nextNode and unmarked, 
            //compare(prevNode->key,k) <= 0 and compare(k, nextNode->key) < 0,
            //*or* prevNode->key == k.
            if(prevNode->key == k){
                if(ownMgr)recordMgr->endOp(threadID());
                return false; //A node with that key is already in the list.
            } 
            newNode = recordMgr->template allocate<ListNode_t>(threadID(), k);
            while(true){
                prevSucc = prevNode->successor;
                prevSucc = (ListNode_t*)((uintptr_t)prevNode & CLEAN_MASK);

                //If predecessor flagged, help delete the predecessor...
                if((uintptr_t)prevSucc & FLAG_MASK){
                    helpFlagged(prevNode, prevSucc);
                }
                else{
                    //newNode.succ = <nextNode, 0, 0>
                    newNode->successor = nextNode;
                    expected = nextNode;
                    prevNode->successor.compare_exchange_strong(expected, newNode);
                    if(expected == nextNode){
                        if(ownMgr)recordMgr->endOp(threadID());
                        return true; //Insert successful.
                    }
                    else{
                        //If prevNode was flagged, help with removing nextNode...
                        if((uintptr_t)expected & FLAG_MASK){
                            ListNode_t *cleanResult = (ListNode_t*)((uintptr_t)expected & CLEAN_MASK);
                            helpFlagged(prevNode, cleanResult);
                        }
                        //While prevNode is marked, continue following backlinks.
                        while(prevNode->mark()){
                            prevNode = prevNode->backlink;
                        }
                    }
                }
                searchFrom(k, prevNode, &prevNode, &nextNode);
                if(prevNode->key == k){
                    if(ownMgr)recordMgr->endOp(threadID());
                    delete newNode;
                    return false; //A node with key k was already in the linked list.
                }
            }
        }
        //DO NOT USE!
        //TODO comment... 
        bool remove(Key k){
            assert(false); //Function should not be used for the moment.
            if(ownMgr){
                threadInit();
                recordMgr->startOp(threadID());
            }

            ListNode_t *prevNode, *delNode;
            bool result;
            searchFrom2(k, &head, &prevNode, &delNode);
            if(compare(delNode->key,k) != 0){
                if(ownMgr)recordMgr->endOp(threadID());
                return false;   //No such key was in the list....
            }
            tryFlag(prevNode, delNode, &prevNode, result);
            if(prevNode)
                helpFlagged(prevNode, delNode);


            if(ownMgr)recordMgr->endOp(threadID());
            return result;
        }
        /**
        * Attempts to find two consecutive nodes n1 and n2 such that 
        * n1.right = n2, n2 was unmarked and n2.key = k
        */
        void searchKey(Key k, ListNode_t *currNode, ListNode_t **n1, ListNode_t **n2){
            ListNode_t *nextNode = currNode->right();

            //Continue iterating while nextNode's element is not e, and nextNode is not the tail of the list.
            while(compare(nextNode->key, k) <= 0 && (nextNode->key != k) &&  (nextNode != &tail)){
                while(nextNode->mark() && (currNode->mark() == 0 || currNode->right() != nextNode)){
                    if(currNode->right() == nextNode){
                        helpMarked(currNode,nextNode);
                    }
                    nextNode = currNode->right();
                }
                if(compare(nextNode->key, k) <= 0 && (nextNode->key != k) && (nextNode != &tail)){
                    currNode = nextNode;
                    nextNode = nextNode->right();
                }
            }

            //Return <currNode, nextNode>
            *n1 = currNode;
            *n2 = nextNode;
        }

        //Attempts to remove a node with exactly the key k from the list.
        //TODO comment...
        bool removeKey(Key k){
            if(ownMgr){
                threadInit();
                recordMgr->startOp(threadID());
            }

            ListNode_t *prevNode, *delNode;
            bool result;
            searchKey(k, &head, &prevNode, &delNode);
            //Element was not found, or element was within the tail node.
            if(delNode->key != k || delNode == &tail){
                if(ownMgr)recordMgr->endOp(threadID());
                return false; 
            }

            //Try to flag the predecessor of delNode. 
            //Recover using searchKey instead of searchFrom2.
            tryFlag2(prevNode, delNode, &prevNode, result);
            if(prevNode){
                helpFlagged(prevNode, delNode);
            }

            if(ownMgr)recordMgr->endOp(threadID());
            if(result == false)return false;
            return true;
        }

        //NOT USED
        bool search(Key k){
            assert(false);
            if(ownMgr){
                threadInit();
                recordMgr->startOp(threadID());
            }

            ListNode_t *currNode, *nextNode;
            searchFrom(k, &head, &currNode, &nextNode);


            if(ownMgr)recordMgr->endOp(threadID());
            return currNode->key == k; //If a node with the key was found, return true.
        }
        Iterator begin() { 
            threadInit();
            Iterator start(&head, this, true);
            ++start;
            return start;
        }
        Iterator end()   { 
            threadInit();
            return Iterator(&tail, this, false);
        }
        
        std::string dumpList(){
            if(ownMgr){
                threadInit();
                recordMgr->startOp(threadID());
            }
            std::ostringstream os;
            os << '<' << &head;
            if(head.mark())os << "-M-";
            else if(head.flag())os << "-F-";
            Iterator it = begin();
            while(it != end()){
                ListNode_t *next = &(*it);
                os << ", " << next;
                if(next->mark())os << "-M-";
                else if(next->flag())os << "-F-";
                ++it;
            }
            os << ", " << &tail << ">";

            if(ownMgr)recordMgr->endOp(threadID());
            if(tail.mark())os << "-M-";
            else if(tail.flag())os << "-F-";
            return os.str();
        }
        //TODO ifdef
        std::string toString(std::string(*keyToStr)(Key), bool newline=true){
            std::ostringstream os;
            os << "<";
            auto it = begin();
            int numNodes = 0;
            if(it != end()){
                ++numNodes;
                ListNode_t &curr = *it;  
                os << keyToStr(curr.key);
            }
            ++it;
            while(it != end()){
                ++numNodes;
                ListNode_t &curr = *it;
                os << ", " << keyToStr(curr.key);
                ++it;
            }
            os << "> " << std::to_string(numNodes)  ;
            if(newline)os << std::endl;
            return os.str();
        }
        

        //Initialize this thread with the record manager if this hasn't happened already.
        void threadInit(){
            assert(threadID() != -1);
            recordMgr->initThread(threadID());	
        }
};