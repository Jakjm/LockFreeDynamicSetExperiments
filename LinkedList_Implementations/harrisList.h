#include <atomic>
#include <stdint.h>
#include <iostream>
#include <sstream>
//Timothy L Harris's lock free linked list,
//Adapted from his paper "A Pragmatic Implementation of Non-Blocking Linked Lists"
uint64_t MARK_MASK = 0x1; //63 0s, then 1  = 0000 0000 0000 0001
uint64_t CLEAN_MASK = -2; //63 1s, 1 0    = FFFF FFFF FFFF FFFE

template <typename Key>
class HarrisNode{
    public:
    std::atomic<HarrisNode<Key>*> successor;  
    Key key;
    HarrisNode(HarrisNode<Key> *next, Key k): successor(next), key(k) {

    }
    HarrisNode(Key k) : HarrisNode(nullptr, k){

    }
    HarrisNode() : HarrisNode(nullptr,0){

    }
    void printNode(std::ostringstream &stream){
        stream << '<' << key << '>';
    }
};


template <typename Key, int(*compare)(Key, Key)>
class HarrisList{
    typedef HarrisNode<Key> HarrisNode_t;
    bool isMarkedRef(HarrisNode_t *ref){
        return ((uintptr_t)ref & MARK_MASK) == MARK_MASK;
    }
    HarrisNode_t* getMarkedRef(HarrisNode_t *ref){
        return (HarrisNode_t*)((uintptr_t)ref | MARK_MASK);
    }
    HarrisNode_t *getUnmarkedRef(HarrisNode_t *ref){
        return (HarrisNode_t*)((uintptr_t)ref & CLEAN_MASK);
    }
    HarrisNode_t tail;
    HarrisNode_t head;
    public:
    HarrisList(): tail(0), head(&tail,0) {

    }
    private: 
    HarrisNode_t *search(Key k, HarrisNode_t **leftNode){
        HarrisNode_t *rightNode, *leftNodeNext;
        do{
            HarrisNode_t *t = &head;
            HarrisNode_t *tNext = head.successor;
            HarrisNode_t *exp; 
            do{
                if(!isMarkedRef(tNext)){
                    *leftNode = t;
                    leftNodeNext = tNext;
                }
                t = getUnmarkedRef(tNext);
                if(t == &tail)break;
                tNext = t->successor;
            }while(isMarkedRef(tNext) || compare(t->key, k) < 0);
            rightNode = t;
            
            if(leftNodeNext == rightNode){
                if((rightNode != &tail) && (isMarkedRef(rightNode->successor))) continue;
                else return rightNode;
            }
            exp = leftNodeNext;
            if(leftNodeNext->successor.compare_exchange_strong(exp, rightNode)){
                if((rightNode != &tail) && (isMarkedRef(rightNode->successor))) continue;
                else return rightNode;
            }
        }while(true);

    }
    public:
    bool insert(Key k){
        HarrisNode_t *newNode = new HarrisNode_t(k);
        HarrisNode_t *right, *left, *exp;
        do{
            right = search(k, &left);
            if(right != &tail && right->key == k){
                return false; //Node already in the list...
            }
            newNode->successor = right;
            exp = right;
            if(left->successor.compare_exchange_strong(exp, newNode))return true;
        }while(true);
        return false; //unreachable
    }
    bool remove(Key k){
        HarrisNode_t *right, *left, *exp, *rightNext;
        do{
            right = search(k, &left);
            if(right == tail || right->key != k) return false;
            rightNext = right->successor;
            if(!isMarkedRef(rightNext)){
                exp = rightNext;
                //TODO
            }

        }while(true);
        return false; //unreachable
    }

    //Not thread safe way of printing the list.
    //There should be no marked references if no remove ops are in progress.
    void printList(){
        std::ostringstream stream;
        stream << "<head> - ";
        HarrisNode_t *cur = head.successor;
        while(cur != &tail){
            cur->printNode(stream);
            cur = cur->successor;
            stream << " - ";
        }
        stream << "<tail>\n";
        std::cout << stream.str();
    }
};