#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <atomic>
#include <cstdint>
#include <stack>
#include "../DynamicSet.h"
#include "../../common.h"
#include "../debra.h"
#include "prefixes.h"
#include <array>
#include <cassert>
#include <bit>




#define SR_SEQ_MASK 0xFFFFFFFFFFFFFFFE
#define SR_RES_MASK 0x1
struct alignas(64) DCSS_Desc{
    std::atomic<std::atomic<uintptr_t>*> addr1;
    std::atomic<uintptr_t> val1;
    std::atomic<uintptr_t> val2;
    std::atomic<uintptr_t> new2;
    std::atomic<uint64_t> seqResult; //stores seq, result
    DCSS_Desc(std::atomic<uintptr_t> *a1 = 0, uint64_t e1 = 0, uint64_t e2 = 0, uint64_t n2 = 0): addr1(a1), val1(e1), val2(e2), new2(n2), seqResult(0){

    }
};


DCSS_Desc descs[MAX_THREADS];
enum {DCSS_FLAG = 1,SUCC_FLAG = 2};
#define FLAG_MASK 3

//Implementation of a DCSS object.
template <typename TYPE>
struct DCSS_PTR{
    std::atomic<uintptr_t> addr;
    DCSS_PTR(TYPE initialValue){
        addr = (uintptr_t)initialValue;
    }
    void init(TYPE n){
        addr = (uintptr_t)n;
    }
    bool cas(TYPE exp, TYPE newVal){
        while(true){
            uintptr_t val = addr;
            if(val == (uintptr_t)exp){
                uintptr_t oldVal = (uintptr_t)exp;
                addr.compare_exchange_strong(oldVal, (uintptr_t)newVal);
                if(oldVal == (uintptr_t)exp){ //CAS succeeded.
                    return true;
                }
            }
            else if((val & FLAG_MASK) == 0){ //CAS failed since DCSS object pointed to a different value.
                return false;
            }
            else{
                uint64_t seq = (val & SEQ_MASK) >> 12;
                uint64_t proc = (val & PROC_MASK) >> 4;
                DCSS_Desc *otherDesc = &descs[proc];
                if((val & FLAG_MASK) == DCSS_FLAG){
                    uintptr_t actualVal = otherDesc->val2;
                    if(((otherDesc->seqResult & SR_SEQ_MASK) >> 1) == seq){
                        if(actualVal == (uintptr_t)exp){
                            //perform helping
                            dcssHelp(seq, proc);
                        }
                        else{ //CAS failed since actual value was not exp
                            return false;
                        }
                    }
                }
                else{//SUCC_FLAG
                    uintptr_t actualVal = otherDesc->new2;
                    if(((otherDesc->seqResult & SR_SEQ_MASK) >> 1) == seq){
                        if(actualVal == (uintptr_t)exp){
                            //perform helping
                            dcssCongrats(seq, proc);
                        }
                        else{ //CAS failed since actual value was not exp.
                            return false;
                        }
                    }
                }
            }
        }
    }

    bool dcss(std::atomic<uintptr_t> *a1, uint64_t e1, TYPE e2, TYPE n2){
        DCSS_Desc *desc = &descs[threadID];
        desc->addr1 = a1;
        desc->val1 = e1;
        desc->val2 = (uintptr_t)e2;
        desc->new2 = (uintptr_t)n2;
        uint64_t seqNum = (desc->seqResult & SR_SEQ_MASK) >> 1;
        uint64_t flagVal = (seqNum << 12) + (threadID << 4) + DCSS_FLAG;

        while(true){
            uintptr_t val = addr;
            if(val == (uintptr_t)e2){
                uintptr_t oldVal = (uintptr_t)e2;
                addr.compare_exchange_strong(oldVal, flagVal);
                if(oldVal == (uintptr_t)e2){ //CAS succeeded.
                    break;
                }
            }
            else if((val & FLAG_MASK) == 0){ //DCSS failed since DCSS object pointed to a different value.
                return false;
            }
            else{
                uint64_t seq = (val & SEQ_MASK) >> 12;
                uint64_t proc = (val & PROC_MASK) >> 4;
                DCSS_Desc *otherDesc = &descs[proc];
                if((val & FLAG_MASK) == DCSS_FLAG){
                    uintptr_t actualVal = otherDesc->val2;
                    if(((otherDesc->seqResult & SR_SEQ_MASK) >> 1) == seq){
                        if(actualVal == (uintptr_t)e2){
                            //perform helping
                            dcssHelp(seq, proc);
                        }
                        else{
                            return false;
                        }
                    }
                }
                else{//SUCC_FLAG
                    uintptr_t actualVal = otherDesc->new2;
                    if(((otherDesc->seqResult & SR_SEQ_MASK) >> 1) == seq){
                        if(actualVal == (uintptr_t)e2){
                            //perform helping
                            dcssCongrats(seq, proc);
                        }
                        else{
                            return false;
                        }
                    }
                }
            }
        }
        //A CAS has successfully inserted our DCSS_DESC.
        uintptr_t oldVal = flagVal;
        if(a1->load() == e1){
            addr.compare_exchange_strong(oldVal, (uintptr_t)n2); //Try to CAS to complete successful DCSS
            if(oldVal == flagVal){ //CAS succeeded; DCSS was successful.
                desc->seqResult = (seqNum + 1) << 1;
                return true; 
            }
        }
        else{
            addr.compare_exchange_strong(oldVal, (uintptr_t)e2); //Try to CAS to complete successful DCSS
            if(oldVal == flagVal){ //CAS succeseded; DCSS was not successful.
                desc->seqResult = (seqNum + 1) << 1;
                return false;
            }
        }

        //When CAS was performed, DCSS_DESC was no longer pointed to by addr.
        //There are three possibilities: 
        //The DCSS succeeded, and addr contains <seqNum, threadID, succFlag>.
        //The DCSS succeeded, and seqResult contains <seqNum, result>.
        //The DCSS failed.
        uint64_t succVal = (seqNum << 12) + (threadID << 4) + SUCC_FLAG;
        if(oldVal == succVal){
            addr.compare_exchange_strong(succVal, (uintptr_t)n2);
            desc->seqResult = (seqNum + 1) << 1;
            return true;
        }

        //If some process said that the DCSS succeeded, return true.
        if((desc->seqResult & SR_RES_MASK) == 1){
            desc->seqResult = (seqNum + 1) << 1;
            return true;
        }
        //The DCSS did not succeed.
        else{
            return false;
        }
    }
    //Congratulate a DCSS descriptor on a sucessful DCSS.
    //addr was equal to <seqNum, procID, succFlag>
    void dcssCongrats(uint64_t seqNum, uint64_t procID){
        DCSS_Desc *desc = &descs[procID];
        uintptr_t oldVal = seqNum << 1;
        uint64_t newVal = desc->new2;
        uint64_t seq = desc->seqResult;
        if(seq <= oldVal + 1){ //If newVal is valid and addr possibly still pointing to desc...
            if(seq == oldVal){
                desc->seqResult.compare_exchange_strong(oldVal, oldVal + 1);
            }
            //Regardless of success, try to put addr back to normal.
            uintptr_t val = addr;
            oldVal = (seqNum << 12) + (threadID << 4) + SUCC_FLAG;
            if(oldVal == val){
                addr.compare_exchange_strong(oldVal, newVal);
            }
        }
    }
    void dcssHelp(uint64_t seqNum, uint64_t procID){
        DCSS_Desc *desc = &descs[procID];
        std::atomic<uintptr_t> *a1 = desc->addr1;
        uint64_t e1 = desc->val1;
        uint64_t e2 = desc->val2;
        //uint64_t n2 = desc->new2;

        if(((desc->seqResult & SR_SEQ_MASK) >> 1) != seqNum){
            return;
        }
        uintptr_t flagVal = (seqNum << 12) + (threadID << 4) + DCSS_FLAG;
        uintptr_t oldVal = flagVal;
        if(a1->load() != e1){
            addr.compare_exchange_strong(oldVal, e2);
        }
        else{ //e1 was stored at a1.
            uintptr_t succVal = (seqNum << 12) + (threadID << 4) + SUCC_FLAG;
            addr.compare_exchange_strong(oldVal, succVal);
            if(oldVal == flagVal){ //CAS succeeded....
                //help congrats
                dcssCongrats(seqNum, procID);
            }
        }
    }
    TYPE read(){
        while(true){
            uintptr_t val = addr;
            if((val & FLAG_MASK) == 0){
                return (TYPE)val;
            }
            else{
                uint64_t seq = (val & SEQ_MASK) >> 12;
                uint64_t proc = (val & PROC_MASK) >> 4;
                DCSS_Desc *otherDesc = &descs[proc];
                TYPE actualVal;
                if((val & FLAG_MASK) == DCSS_FLAG){ //val had DCSS_FLAG set; actual value was val2.
                    actualVal = (TYPE)(uintptr_t)otherDesc->val2;
                }
                else{ //val had SUCC_FLAG set; actual value of DCSS_PTR was new2.
                    actualVal = (TYPE)(uintptr_t)otherDesc->new2;
                }

                if(((otherDesc->seqResult & SR_SEQ_MASK) >> 1) == seq){
                    //If seqNumber of otherDesc had not changed, return the value of the object.
                    return actualVal;
                }
            }
        }
    }
};

struct ReclaimableBase{
    virtual ~ReclaimableBase(){
        
    }
};

//Nodes of the skip trie.....
struct alignas(64) STNode : public ReclaimableBase{
    int64_t key;
    std::atomic<uintptr_t> nextState; //Contains <next, mark>
    std::atomic<STNode*> back;
    DCSS_PTR<STNode*> prev; 
    STNode* down;
    STNode* root;
    std::atomic<bool> stop;
    //std::atomic<bool> ready;
    //int orig_height;
    //volatile char padding[64 - 4 * sizeof(STNode*) + sizeof(bool)];
    STNode(int64_t k=0, int height=0) : key(k), nextState(0), back(nullptr), prev(nullptr), down(nullptr), root(nullptr), stop(false){

    }
};

struct alignas(64) TreeNode : public ReclaimableBase{
    DCSS_PTR<STNode*> pointers[2];
    // STNode *greatestZero;
    // STNode *largestOne;
    TreeNode(STNode *gt0 = nullptr, STNode *lg1 = nullptr): pointers{gt0,lg1}{

    }
};




//Type which is used to allow threads to reuse nodes they fail to insert...
struct STNodePool{
    STNode *node;
    volatile char padding[64 - 8];
    STNodePool(): node(new STNode()){

    }
    ~STNodePool(){
        delete node;
    }
};

STNodePool node_pool[MAX_THREADS];


Debra<ReclaimableBase,5> stDebra;

// template <typename Value>
// struct HTNode{
//     uintptr_t nextState;
//     uint64_t key;
//     Value val;
// };




//"Hashtable" for treenodes in array....
struct PrefixesTable{
    std::atomic<TreeNode*> * const array;
    int64_t universeSize;
    PrefixesTable(int64_t universeSize): array(new std::atomic<TreeNode*>[2 * universeSize]), universeSize(universeSize){
        for(int64_t i = 0;i < 2*universeSize;++i){
            array[i] = nullptr;
        }
    }
    ~PrefixesTable(){
        for(int64_t i = 1; i < 2*universeSize;++i){
            if(array[i]){
                delete array[i];
            }
        }
        delete[] array;
    }
    TreeNode *lookup(int64_t prefix){
        return array[prefix];
    }
    void compareAndDelete(int64_t prefix, TreeNode *tn){
        TreeNode *old = tn;
        array[prefix].compare_exchange_strong(old, nullptr);
        stDebra.reclaimLater(tn);
    }
    bool insert(int64_t prefix, TreeNode *tn){
        TreeNode *old = nullptr;
        return array[prefix].compare_exchange_strong(old, tn);
    }
};



//logU - max number of levels in the skip list....
template <int loglogU = 5>
struct SkipTrie : public DynamicSet {
    STNode head[loglogU]; //levels are numbered 0, ..., levels - 1
    STNode tail;
    int64_t universeSize;
    int64_t logU;
    PrefixesTable prefixes;
    SkipTrie(int log_U): tail(INT64_MAX), universeSize(1 << log_U), logU(log_U), prefixes(universeSize){
        //Initialize the skip trie...

        for(int l = 0; l < loglogU; ++l){
            head[l].key = -1;
            head[l].nextState = (uintptr_t)&tail;
            if(l > 0)head[l].down = &head[l - 1];
            else head[l].down = &head[l];
        }
        tail.prev.init(&head[loglogU - 1]);
        tail.root = &tail;
        tail.down = &tail;
    }

    ~SkipTrie(){
        for(int lv = loglogU - 1; lv >= 0;--lv){
            STNode *cur = (STNode *)(head[lv].nextState & NEXT_MASK); 
            STNode *next;
            while(cur != &tail){
                next = (STNode *)(cur->nextState & NEXT_MASK);
                delete cur;
                cur = next;
            }
        }
    }

    STNode *lowestAncestor(int64_t key){
        int64_t common_prefix = 1;
        int64_t start = 0; //index of first bit
        int64_t size = logU / 2;

        STNode *ancestor = &tail;
        while(size > 0){
            int64_t query = mergePrefix(common_prefix, key, start, size, logU);//TODO prefix + star....
            int dir = key >> (logU - (start + 1)) & 1;
            TreeNode *query_node = prefixes.lookup(query);
            if(query_node){
                STNode *candidate = query_node->pointers[dir].read();
                if(candidate && isPrefix(query, candidate->key, logU)){
                    if(std::abs(key - candidate->key) <= std::abs(key - ancestor->key)){
                        ancestor = candidate;
                    }
                    common_prefix = query;
                    start = start + size;
                }
            }
            
            size = size / 2;
        }
        //int64_t 
        return ancestor;
    }
    STNode *xFastTriePred(int64_t key){
        STNode *curr = lowestAncestor(key);
        while(curr->key > key){
            uintptr_t state = curr->nextState & STATUS_MASK;
            if(state == Marked)curr = curr->back;
            else curr = curr->prev.read();
        }
        return curr;
    }
    void fixPrev(STNode *pred, STNode *node){
        STNode *left;
        left = pred;
        searchRight(node->key - 1,left);
        uintptr_t nextState = node->nextState;
        uint64_t state = nextState & STATUS_MASK;
        while(state != Marked){
            STNode *prev = node->prev.read();
            
            if(node->prev.dcss(&left->nextState, (uintptr_t)node, prev, left)){
                break;
            }
            //search
            left = pred;
            searchRight(node->key - 1, left);

            nextState = node->nextState;
            state = nextState & STATUS_MASK;
        }
        //node->ready = 1;
    }
    bool topLevelDelete(STNode *pred, STNode *node){
        STNode *left, *right;
        // if(!node->ready){
        //     fixPrev(pred, node);
        // }
        bool result = skipListDelete(pred,node);
        //top to bottom
        left = pred;
        bool marked;
        do{
            right = searchRight(node->key-1, left);
            fixPrev(left, right);
            uintptr_t nextState = right->nextState;
            marked = (nextState & STATUS_MASK) == Marked;
        }while(marked);
        return result;
    }
    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point, and delNode is Marked.
    uintptr_t helpMarked(STNode *prev, STNode *delNode){
        STNode *next = (STNode*)(delNode->nextState & NEXT_MASK);
        uintptr_t expected = (uintptr_t)delNode + DelFlag;
        uintptr_t result = expected;
        prev->nextState.compare_exchange_strong(result, (uintptr_t)next);
        //assert(prev->key < next->key);
        //assert((STNode*)(next->nextState & NEXT_MASK) != prev);
        
        if(result == expected){
            stDebra.reclaimLater(delNode);
            return (uintptr_t)next;
        }
        else return result;
    }
    //Precondition: prev.successor was <delNode, DelFlag> at an earlier point.
    uintptr_t helpRemove(STNode *prev, STNode *delNode){
        delNode->back = prev;
        uintptr_t succ = delNode->nextState.load(); //The value of delNode's successor pointer
        uintptr_t state = succ & STATUS_MASK;
        uintptr_t next = succ & NEXT_MASK;

        while(state != Marked){ //While delNode is not marked...
            if(state == DelFlag){ //Help with deletion of its successor, if it is flagged....
                succ = helpRemove(delNode, (STNode*)next);
            }
            else{ //Attempt to mark the node if the status was normal...
                uintptr_t markedSuccessor = (uintptr_t)next + Marked;
                succ = next;
                delNode->nextState.compare_exchange_strong(succ, markedSuccessor); //Try to update from <next, Normal> to <next, Marked>
                //assert((STNode*)(delNode->nextState & NEXT_MASK) != prev);
                if(succ == next)break; //The CAS succeeded!
            }
            state = succ & STATUS_MASK;
            next = succ & NEXT_MASK;
        }
        succ = helpMarked(prev, delNode);
        return succ;
    }



    //Precondition: targetNode->root is marked, and prev->right was equal to targetNode.
    bool tryFlag(STNode *&prev, STNode *targetNode, bool &inList){
        while(1){
            uintptr_t succ = prev->nextState;
            if(succ == ((uintptr_t)targetNode) + DelFlag){
                inList = true;
                return false;
            }
            succ = (uintptr_t)targetNode;
            prev->nextState.compare_exchange_strong(succ, ((uintptr_t)targetNode) + DelFlag);
            if(succ == (uintptr_t)targetNode){
                inList = true;
                return true;
            }
            if(succ == (((uintptr_t)targetNode) + DelFlag)){
                inList = true;
                return false;
            }
            while((succ & STATUS_MASK) == Marked){
                prev = prev->back;
                succ = prev->nextState;
            }

            STNode *delNode = searchRight(targetNode->key - 1,prev);
            if(targetNode != delNode){
                inList = false;
                return false;
            }
        }
    }

    //Returns next and curr such that curr.key <= k < next.key
    STNode *searchRight(int64_t k, STNode *&curr){
        uintptr_t succ = curr->nextState;
        STNode *next = (STNode*)(succ & NEXT_MASK);      
        int64_t status = (succ & STATUS_MASK);  
        //assert((curr->key <= k) && (curr->key < next->key));
        while(true){
            if(status == DelFlag){ //If curr is flagged,, try to remove the node following it.
                succ = helpRemove(curr, next);
            }
            else if(status == Marked){ //If curr is marked, try to help remove it, and resume search from node preceding it.
                STNode *prev = curr->back;
                succ = prev->nextState;
                if(succ == (uintptr_t)next + DelFlag){
                    succ = helpMarked(prev, curr);
                }
                curr = prev;
            }
            else if(next->root->stop){ //If next is superfluous, try to flag the node preceding it.
                succ = (uintptr_t)next;
                curr->nextState.compare_exchange_strong(succ, (uintptr_t)next + DelFlag);
            }
            else{
                if(next->key <= k){
                    curr = next;
                    succ = curr->nextState;
                }
                else{
                    break;
                }
            }
            next = (STNode*)(succ & NEXT_MASK);
            status = (succ & STATUS_MASK);  
        }
        //assert(curr->key <= k);
        //assert(next->key > k);
        return next; //Return <curr, next> such that curr->key <= k < next->key
    }


    // STNode* findStart(int &level){
    //     int curLevel = level;
    //     uintptr_t succ = head[curLevel + 1].succ;
    //     STNode *next = (STNode*)(succ & NEXT_MASK);
    //     while(next != &tail && curLevel < logU - 1){
    //         ++curLevel;

    //         succ = head[curLevel + 1].succ;
    //         next = (STNode*)(succ & NEXT_MASK);
    //     }
    //     level = curLevel;
    //     return &head[curLevel];
    // }

    
    //Storing nodes with key <= k for each level on the stack
    STNode* searchToLevel(int64_t k, int level, STNode *&next, STNode*start, std::array<STNode*,loglogU> &levelStart){
        int curLevel = loglogU - 1;
        STNode *curr = start;
        while(curLevel > level){
            next = searchRight(k, curr);
            levelStart[curLevel] = curr;
            curr = curr->down;
            --curLevel;
        }
        next = searchRight(k, curr);
        levelStart[curLevel] = curr;
        return curr; //Return <curr, next>
    }
    STNode* searchToLevel(int64_t k, int level, STNode *&next, STNode*start){
        int curLevel = loglogU - 1;
        STNode *curr = start;
        while(curLevel > level){
            next = searchRight(k, curr);
            curr = curr->down;
            --curLevel;
        }
        next = searchRight(k, curr);
        return curr;
    }
    // STNode* searchToLevel(int64_t k, int level, STNode *&next){
    //     int curLevel = loglogU - 1;
    //     STNode *curr = &head[curLevel];
    //     while(curLevel > level){
    //         next = searchRight(k, curr);
    //         curr = curr->down;
    //         --curLevel;
    //     }
    //     next = searchRight(k, curr);
    //     return curr; //Return <curr, next>
    // }
    STNode *insertNode(STNode *newNode, STNode *&prev, STNode *next, int height){
        //assert(newNode->down);
        if(prev->key == newNode->key){
            return nullptr;
        }
        while(1){ //Keep on trying to insert until node is inserted
            uint64_t succ = prev->nextState;
            if((succ & STATUS_MASK) == DelFlag){
                helpRemove(prev, (STNode*)(succ & NEXT_MASK));
            }
            else{
                newNode->nextState = (uintptr_t)next;
                succ = (uintptr_t)next;
                if(height == loglogU - 1){
                    //assert(false);
                    newNode->prev.init(prev);// = prev;
                }
                //assert(prev->key < newNode->key);
                //assert(newNode->key < next->key);
                prev->nextState.compare_exchange_strong(succ, (uintptr_t)newNode);
                //assert((STNode*)(newNode->nextState & NEXT_MASK) != prev);
                if(succ == (uintptr_t)next){
                    if(height == loglogU - 1){
                        //assert(false);
                        fixPrev(newNode, next);
                    }
                    return newNode; //CAS succeeded in inserting new node :)
                }
                else{
                    if((succ & STATUS_MASK) == DelFlag){
                        succ = helpRemove(prev, (STNode*)(succ & NEXT_MASK));
                    }
                    while((succ & STATUS_MASK) == Marked){
                        prev = prev->back;
                        succ = prev->nextState;
                    }
                }
            }
            next = searchRight(newNode->key, prev);
            //assert(prev->key <= newNode->key);
            //assert(newNode->key < next->key);
            if(prev->key == newNode->key){
                return nullptr;
            }
        }
    }
    bool insert(int64_t k){
        stDebra.startOp();
        STNode *pred = xFastTriePred(k - 1);
        int height;
        STNode *node = skipListInsert(k,height, pred);
        if(!node){
            stDebra.endOp();
            return false; //node was already in list...
        }
        if(height != loglogU){
            stDebra.endOp();
            return true;
        }
        //assert(false);
        for(int64_t len = logU - 1; len >= 0; --len)
        {
            int64_t prefix = getPrefix(k, len, logU);
            int dir = (k >> (len + 1)) & 1;
            uint64_t nextState = node->nextState;
            uint64_t state = (nextState & STATUS_MASK);
            while(state != Marked){
                //int64_t prefix = k;
                TreeNode *tn = prefixes.lookup(prefix);
                if(tn == nullptr){
                    tn = new TreeNode(); //TODO reuse TreeNode...
                    tn->pointers[dir].init(node);
                    if(prefixes.insert(prefix,tn))break;
                }
                else if(tn->pointers[0].read() == nullptr && tn->pointers[1].read() == nullptr){
                    prefixes.compareAndDelete(prefix,tn);
                }
                else{
                    STNode *curr = tn->pointers[dir].read();
                    if(curr && ((dir == 0 && curr->key >= k) || (dir == 1 && curr->key <= k))){
                        break;
                    }
                    nextState = node->nextState;
                    STNode *next = (STNode*)(nextState & NEXT_MASK);

                    //DCSS:
                    // if tn->pointers[dir] == curr and node->nextState == <next, unmarked>, update 
                    //      tn->pointers[dir] = node
                    if(tn->pointers[dir].dcss(&node->nextState, (uintptr_t)next, curr, node)){
                        break;
                    }
                }

                nextState = node->nextState;
                state = (nextState & STATUS_MASK);
            }
        }
        stDebra.endOp();
        return true;
    }
    STNode *skipListInsert(int64_t k, int &height, STNode *curr){
        STNode *next;
        std::array<STNode*,loglogU> startingPlaces;
        curr = searchToLevel(k, 0, next, curr, startingPlaces);
        if(curr->key == k){
            return nullptr;
        }
        STNode *newRoot = node_pool[threadID].node;
        STNode *newNode = newRoot;
        STNode *lastNode;
        height = 1;
        //Continue increasing height up to max level while flipping a fair coin
        while(rng(2) == 0 && height < loglogU){
            ++height;
        }
        int level = 0;
        while(true){
            lastNode = newNode;
            newNode = node_pool[threadID].node;
            newNode->key = k;
            newNode->down = lastNode;
            newNode->root = newRoot;

            //If insertion of node has failed, and this is the first level, keep node for subsequent insertion...
            STNode *result = insertNode(newNode, curr, next, level);
            if(result == nullptr && level == 0){
                return nullptr;
            }
            node_pool[threadID].node = new STNode(k);
            if(newRoot->stop){
                //Help remove newnode...
                removeNode(curr, newNode);
                return newNode;
            }
            ++level;
            if(level >= height){ //Stop building the tower if we've already reached the desired height...
                //if(level == loglogU)fixPrev(newNode, newNode);
                return newNode;
            }

            curr = startingPlaces[level];
            next = searchRight(k,curr);
        }
        return newNode;
    }
    STNode *removeNode(STNode *prev, STNode *delNode){
        bool inList;
        bool result = tryFlag(prev, delNode, inList);
        if(inList){
            helpRemove(prev,delNode);
        }
        if(result)return delNode;
        else return nullptr;
    }


    bool remove(int64_t k){
        stDebra.startOp();

        //pred = predecessor(k - 1)
        STNode *pred = xFastTriePred(k-1);
        STNode *left = pred;
        STNode *node = searchRight(k-1, left);

        if(node->key != k){
            bool result = skipListDelete(left,k);
            stDebra.endOp();
            return result;
        }
        //delete node from skip list
        else if(!topLevelDelete(left, node)){
            stDebra.endOp();
            return false;
        }

        for(int64_t len = 0;len < logU;++len){
            int64_t prefix = getPrefix(k, len, logU);
            int dir = (k >> (len + 1)) & 1;

            TreeNode *tn = prefixes.lookup(prefix);
            if(!tn)continue;
            STNode *curr = tn->pointers[dir].read();
            while(curr == node){
                STNode *right = searchRight(k-1, left);

                // dir = 0: DCSS(pointers[direction], curr, left, left.succ, (right, 0))
                // dir = 1: DCSS(pointers[direction], curr, right, left.succ, (right, 0))
                STNode *newNode;
                if(dir == 0) newNode = left;
                else newNode = right;

                tn->pointers[dir].dcss(&left->nextState, (uintptr_t)right, curr, newNode);
                curr = tn->pointers[dir].read();
            }
            if(!isPrefix(prefix,curr->key, logU)){
                tn->pointers[dir].cas(curr, nullptr);
            }
            if(tn->pointers[0].read() == nullptr && tn->pointers[1].read() == nullptr){
                prefixes.compareAndDelete(prefix, tn);
            }
        }
        stDebra.endOp();
        return true;
    } 
    bool skipListDelete(STNode *prev, int64_t k){
        //stDebra.startOp();
        STNode *curr, *root;
        std::array<STNode*,loglogU> startingPlaces;
        curr = searchToLevel(k-1,0, root, prev, startingPlaces);
        if(root->key != k){ //root is not the node we are trying to delete...
            //stDebra.endOp();
            //return false;
            return false;
        }
        bool result = !root->stop.exchange(true);
        //removeNode(curr, next);
        //Search and delete delNode from other levels of the tree...
        for(int level = loglogU - 1; level >= 0;--level){
            curr = startingPlaces[level];
            searchRight(k,curr);
        }
        return result;
    }
    bool skipListDelete(STNode *prev, STNode *delNode){
        //stDebra.startOp();
        STNode *curr, *root;
        int64_t k = delNode->key;
        std::array<STNode*,loglogU> startingPlaces;
        curr = searchToLevel(k-1,0, root, prev, startingPlaces);
        if(root != delNode->root){ //root is not the node we are trying to delete...
            return false;
        }
        bool result = !root->stop.exchange(true);
        //removeNode(curr, next);
        //Search and delete delNode from other levels of the tree...
        for(int level = loglogU - 1; level >= 0;--level){
            curr = startingPlaces[level];
            searchRight(k,curr);
        }
        return result;
    }
    bool search(int64_t x){
        stDebra.startOp();
        STNode *start = xFastTriePred(x-1);
        STNode *next;
        searchToLevel(x-1,0,next,start);
        bool result = (next->key == x);
        stDebra.endOp();
        return result;
    }
    int64_t predecessor(int64_t x){
        stDebra.startOp();
        STNode *start = xFastTriePred(x-1);
        STNode *curr, *next;
        curr = searchToLevel(x-1,0,next,start);
        int64_t pred = curr->key;
        stDebra.endOp();
        return pred;
    }
    std::string name(){
        return "Oshman-Shavit SkipTrie";
    }
};

