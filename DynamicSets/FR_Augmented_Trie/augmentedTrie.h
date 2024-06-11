#include <atomic>
#include "../DynamicSet.h"
#include "../debra.h"

//An Implementation of Fatourou and Ruppert's Augmented Static Trie
//Version objects which are pointed to by nodes of Augmented Static Trie
//A Version only becomes immutable once inserted.
struct Version{
    int sum;
    Version *left, *right;
    Version(int s = 0, Version *l = nullptr, Version *r = nullptr) : sum(s), left(l), right(r){

    }
};

Debra<Version, 7> versionDebra;

//Augmented Static Trie Nodes, which contain only a pointer to their version objects.
struct AST_Node{
    std::atomic<Version*> version;
    //If the space_nodes constant is defined, 
    //we should avoid false sharing by putting padding in these nodes....
    #ifdef space_nodes 
        volatile char padding[64 - 8];
    #endif
    AST_Node() : version(new Version()){

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
VersionPool versionPool[MAX_THREADS];

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
    AST_Node *leaf;
    int64_t arraySize;
    int64_t universeSize;

    //height: the height of the AS_Trie.
    AS_Trie(int height) {
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
    bool refresh(int64_t i){
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
        *v = Version(newSum, leftV, rightV);

        result = old;
        node.version.compare_exchange_strong(result, v);
        //If the CAS was successful
        if(result == old){
            //TODO insert old into limbo bag....
            pool.v = new Version(); //Allocate new version to be used...
            return true;
        }
    }
    //Propogates the version objects up the trie.
    //start is the index of an internal AST_Node in the array.
    inline void propogate(int64_t cur){
        //Try to update every internal node between array[cur] and array[0] (the root)
        while(cur > 0){
            if(!refresh(cur))refresh(cur);
            cur = (cur - 1) / 2; //Go to cur's parent
        }
        //Cur is now the root
        //Try twice to update root
        if(!refresh(cur))refresh(cur); 
    }
    void insert(int64_t x){
        versionDebra.startOp();
        versionDebra.endOp();
    }
    void remove(int64_t x){
        versionDebra.startOp();
        versionDebra.endOp();

    }
    int64_t predecessor(int64_t x){
        versionDebra.startOp();
        versionDebra.endOp();
    }
    bool search(int64_t x){
        versionDebra.startOp();
        int64_t index = 0;
        Version *rootVersion = 
        versionDebra.endOp();
    }
    virtual std::string name(){
        return "Fatourou and Ruppert's Augmented Static Trie";
    }
};