#include <atomic>
#include <vector>
#include <array>
#include "../DynamicSet.h"
#include "../debra.h"
#include "../../common.h"
#include <iostream>
using std::cout;
//An Implementation of Fatourou and Ruppert's Augmented Static Trie
//Version objects which are pointed to by nodes of Augmented Static Trie
//A Version only becomes immutable once inserted.
struct alignas(64) Version : public ReclaimableBase{
    int sum;
    Version *left, *right;
    Version *root; 
    std::atomic<bool> completed;
    Version(int s = 0, Version *l = nullptr, Version *r = nullptr, Version *rt = nullptr) : sum(s), left(l), right(r), root(rt), completed(false) {

    }
    void init(int s = 0, Version *l = nullptr, Version *r = nullptr, Version *rt = nullptr){
        sum = s;
        left = l;
        right = r;
        root = rt;
    }
    ~Version(){

    }
};


#define space_nodes 5

//Augmented Static Trie Nodes, which contain only a pointer to their version objects.
struct alignas(64) AST_Node{
    std::atomic<Version*> version;
    //If the space_nodes constant is defined, 
    //we should avoid false sharing by putting padding in these nodes....
    #ifdef space_nodes 
        volatile char padding[64 - 8];
    #endif
    AST_Node(){
        Version *v = new Version();
        v->completed = true;
        v->root = v;
        version = v;
    }

    ~AST_Node(){
        delete version;
    }
};

//Structure used to store pointers to Versions that go unused after allocations.
//On subsequent insert/delete operations by the same thread, the previouslly allocated version can be reused.
struct VersionPool{
    Version *v;
    volatile char padding[64-sizeof(Version*)];
    VersionPool(): v(new Version()){
    }
    ~VersionPool(){
        delete v;
    }
};
std::array<VersionPool,MAX_THREADS> versionPool;

/*
* In my implementation, the AST_Nodes do not need left, right or parent pointers since the 
* nodes are stored in an array. My implementation uses the standard array representation of a binary tree.
*
* A trie of height 2 containing the universe {0, 1, 2, 3} would be represented 
* using a dynamically allocated sequential array of 7 Augmented Static Trie nodes.
*                         0
*                      1     2
*                     3 4   5  6

* 
*  
* For an AST_Tree of height k, the array has 2^(k + 1) - 1 elements.
* The root is at index 0.
* nodes at depth i in the trie are between index 2^i - 1 and 2^(i + 1) - 2
* leaves are between index 2^k - 1 and 2^(k + 1) - 2, inclusive
*
* For a node at index i:
* - its parent is at index (i - 1) / 2
* - its left child is at index  2i + 1
* - its right child is at index 2i + 2
*/
//One interesting aspect of this implementation is that contention is only encountered
//on nodes that are permanent; the version objects which are dynamically allocated never change.

//Augmented Static Trie class type.
struct AS_Trie : public DynamicSet{
    AST_Node *array;
    AST_Node *leaf; //Pointer to the part of array where the leaves start
    int64_t arraySize;
    const int trieHeight;
    const int64_t universeSize; //Equal to 2^trieHeight


    //height: the height of the AS_Trie.
    AS_Trie(int height) : trieHeight(height), universeSize((int64_t)1 << height) {
        
        arraySize = (1 << (uint64_t)(height + 1)) - 1;
        array = new AST_Node[arraySize]; //Dynamically allocate array of AST_Nodes...

        //Calculate the size of the universe minus one.
        int64_t universeSize_minOne = (1 << (uint64_t)(height)) - 1;

        //Initializing leaf to point to the first leaf in the array.
        leaf = array + universeSize_minOne;
        
        //For every internal AST_Node, set its version object to point to the version object of its children.
        for(int64_t i = 0; i < universeSize_minOne; ++i){
            Version *v = array[i].version;

            //Calculate the indices of the children of this internal node.
            int64_t leftChild = 2 * i + 1;
            int64_t rightChild = 2 * i + 2;

            v->left = array[leftChild].version;
            v->right = array[rightChild].version;
        }
    }

    ~AS_Trie(){
        delete[] array; //This will recursively call destructors...
    }

    //Refreshes the version of an internal trie node.
    //i is the index of the AST_Node in array.
    inline bool refresh(int64_t i, Version *root){
        AST_Node &node = array[i];
        AST_Node &left = array[(2 * i) + 1];
        AST_Node &right = array[(2 * i) + 2];

        Version *old = node.version;
        Version *result;
        Version *leftV = left.version;
        Version *rightV = right.version;
        int64_t newSum = leftV->sum + rightV->sum;

        VersionPool &pool = versionPool[threadID];
        Version *v = pool.v;
        v->init(newSum, leftV, rightV, root);

        result = old;
        node.version.compare_exchange_strong(result, v);
        //If the CAS was successful
        if(result == old){
            if(i == 0){
                root->completed = true;
                leftV->root->completed = true;
                rightV->root->completed = true;
            }
            debra.reclaimLater(old);
            pool.v = new Version(); //Allocate new version to be used...
            return true;
        }
        else return false;
    }
    //Propogates the version objects up the trie.
    //start is the index of an internal AST_Node in the array.
    inline void propogate(int64_t cur, Version *root){
        //Try to update every internal node between array[cur] and array[0] (the root)
        while(root->completed == false){
            if(!refresh(cur, root) && root->completed == false){
                refresh(cur, root);
            }
            if(cur == 0){ // We have just updated the root...
                root->completed = true;
                return;
            }
            cur = (cur - 1) / 2; //Go to cur's parent
        }
    }
    bool insert(int64_t x){
        bool success = false;
        debra.startOp();
        Version *cur = leaf[x].version;
        Version *v = cur;
        //If cur->sum == 1, key was already in set 
        if(cur->sum == 0){
            VersionPool &pool = versionPool[threadID];
            v = pool.v; //New version in which sum is 1
            v->init(1);

            Version *result = cur;
            leaf[x].version.compare_exchange_strong(result, v);
            //CAS was successful
            if(result == cur){
                success = true;
                //Will try to reclaim the version that was removed later....
                debra.reclaimLater(cur);
                //Allocate a new version for the pool
                pool.v = new Version();
            }
            else{
                v = result;
            }
        }
        else if(cur->completed){
            //No need to propogate....
            return false;
        }

        //Calculate array index for parent of x.
        int64_t arrayIndex = (x + universeSize  - 1); //Array index of x
        int64_t parent = (arrayIndex - 1) / 2; 
        propogate(parent, v);
        debra.endOp();
        return success;
    }
    bool remove(int64_t x){
        bool success = false;
        debra.startOp();
        Version *cur = leaf[x].version;
        Version *v = cur;
        //If cur->sum == 1, key was already in set 
        if(cur->sum == 1){
            VersionPool &pool = versionPool[threadID];
            v = pool.v; //New version in which sum is 0
            v->init(0);

            Version *result = cur;
            leaf[x].version.compare_exchange_strong(result, v);
            //CAS was successful
            if(result == cur){
                success = true;
                //Will try to reclaim the version that was removed later....
                debra.reclaimLater(cur);
                //Allocate a new version for the pool
                pool.v = new Version();
            }
            else {
                v = result;
            }
        }
        else if(cur->completed){
            //No need to propogate....
            return false;
        }

        //Calculate array index for parent of x.
        int64_t arrayIndex = (x + universeSize  - 1); //Array index of x
        int64_t parent = (arrayIndex - 1) / 2; 
        propogate(parent, v);
        debra.endOp();
        return success;
    }
    int64_t predecessor(int64_t x){
        debra.startOp();

        Version *v = array[0].version; //Read the root version
        std::vector<Version*> levels; //Internal nodes while traversing downwards.
        int height = trieHeight; //Height of v

        //Traverse down to the AS_Node for key x.
        while(height > 0){
            //Check the (height - 1)-th bit of x.
            levels.push_back(v);
            int bit = (x >> (height - 1)) & 1; 
            if(bit == 0)v = v->left;
            else v = v->right;
            --height;
        }

        //v is currently AT_Node for x.
        //Perform predecessor query starting from node for x...
        int depth = trieHeight; //Depth of v

        //x is now the index of v within its row.
        //While either v is the left sibling or v.parent.left has a 0 sum
        while((x & 1) == 0 || levels[depth - 1]->left->sum == 0){
            //Go up to v.parent
            x = x >> 1;
            --depth;
            if(depth == 0){
                debra.endOp();
                return -1;
            }
            v = levels[depth];
        }

        //v = v.parent.left
        v = levels[depth - 1]->left;
        x = x - (x & 1);
        while(depth < trieHeight){
            ++depth;
            if(v->right->sum > 0){
                x = 2 * x + 1;
                v = v->right;
            }
            else if(v->left->sum > 0){
                x = 2 * x;
                v = v->left;
            }
            else{
                debra.endOp();
                return -1;
            }
        }
        debra.endOp();
        return x;
    }
    bool search(int64_t x){
        debra.startOp();
        Version *v = array[0].version; //Read the root version
        int height = trieHeight; //Height of v
        while(height > 0){
            //Check the (height - 1)-th bit of x.
            int bit = (x >> (height - 1)) & 1; 
            if(bit == 0)v = v->left;
            else v = v->right;
            --height;
        }
        bool result = (v->sum == 1);
        debra.endOp();
        return result;
    }
    std::string name(){
        return "Fatourou and Ruppert's Augmented Static Trie";
    }


};