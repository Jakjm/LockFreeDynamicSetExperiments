#include <atomic>
#include <vector>
#include <array>
#include "../DynamicSet.h"
#include "../debra.h"
#include "../../common.h"
#include <iostream>
using std::cout;

enum Colour{black, red};
//An Implementation of Fatourou and Ruppert's Augmented Static Trie
//RBVersion objects which are pointed to by nodes of Augmented Static Trie
//A RBVersion only becomes immutable once inserted.
struct RBVersion{
    int64_t sum;
    int64_t key;
    int64_t rank;
    RBVersion *left, *right;
    Colour colour;
    RBVersion(int64_t s = 0, int64_t k = 0, RBVersion *l = nullptr, RBVersion *r = nullptr) : sum(s), key(k), rank(0), left(l), right(r), colour(black){

    }
};

Debra<RBVersion, 5> RBVersionDebra;

#define space_nodes 5

//Augmented Static Trie Nodes, which contain only a pointer to their RBVersion objects.
struct RB_AST_Node{
    std::atomic<RBVersion*> version;
    //If the space_nodes constant is defined, 
    //we should avoid false sharing by putting padding in these nodes....
    #ifdef space_nodes 
        volatile char padding[64 - 8];
    #endif
    RB_AST_Node() : version(new RBVersion()){

    }

    ~RB_AST_Node(){
        delete version;
    }
};

//Structure used to store pointers to RBVersions that go unused after allocations.
//On subsequent insert/delete operations by the same thread, the previouslly allocated RBVersion can be reused.
struct RBVersionPool{
    RBVersion *v;
    volatile char padding[64-sizeof(RBVersion*)];
    RBVersionPool(): v(new RBVersion()){

    }
    ~RBVersionPool(){
        delete v;
    }
};
std::array<RBVersionPool,MAX_THREADS> versionPool;

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
//on nodes that are permanent; the RBVersion objects which are dynamically allocated never change.

//Augmented Static Trie class type.
struct AS_Trie : public DynamicSet{
    RB_AST_Node *array;
    RB_AST_Node *leaf; //Pointer to the part of array where the leaves start
    int64_t arraySize;
    const int trieHeight;
    const int64_t universeSize; //Equal to 2^trieHeight


    //height: the height of the AS_Trie.
    AS_Trie(int height) : trieHeight(height), universeSize((int64_t)1 << height) {
        
        arraySize = (1 << (uint64_t)(height + 1)) - 1;
        array = new RB_AST_Node[arraySize]; //Dynamically allocate array of AST_Nodes...

        //Calculate the size of the universe minus one.
        int64_t universeSize_minOne = (1 << (uint64_t)(height)) - 1;

        //Initializing leaf to point to the first leaf in the array.
        leaf = array + universeSize_minOne;
        
        //Initially, every node points to a RBVersion with null left and right childs, sum 0 and color black.
    }

    ~AS_Trie(){
        delete[] array; //This will recursively call destructors...
    }

    //TODO: fix this
    //Refreshes the RBVersion of an internal trie node.
    //i is the index of the RB_AST_Node in array.
    inline bool refresh(int64_t i){
        RB_AST_Node &node = array[i];
        RB_AST_Node &left = array[(2 * i) + 1];
        RB_AST_Node &right = array[(2 * i) + 2];

        RBVersion *old = node.version;
        RBVersion *result;
        RBVersion *leftV = left.version;
        RBVersion *rightV = right.version;
        RBVersionPool &pool = versionPool[threadID];
        RBVersion *v;

        if(leftV->sum == 0){
            v = leftV;
        }
        else if(rightV->sum == 0){
            v = rightV;
        }
        else{//Both rightV and leftV have children...
            //v = pool.v;
            //Redblack join of leftV and rightV...
            //v = join(leftV, rightV);
            //std::vector<RBVersion*> parents;
            RBVersion *x, *parent = nullptr;
            if(leftV->rank >= rightV->rank){
                x = leftV;
                while(x->rank > rightV->rank){
                    parent = x;
                    //parents.push_back(x);
                    x = x->right;
                }
                v->left = x;
                v->right = rightV;
                if(parent){
                    //parent->right = x;
                    v = leftV;
                }
                //Otherwise, just replace node.version with v?
            }
            else{
                x = rightV;
                while(x->rank > leftV->rank){
                    parent = x;
                    x = x->left;
                }
                v->right = x;
                v->left = leftV;
                if(parent){
                    //parent->left = x;
                    v = rightV;
                }
                //Otherwise just replace node.version with v?
            }
        }
        //int64_t newSum = leftV->sum + rightV->sum;


        result = old;
        node.version.compare_exchange_strong(result, v);
        //If the CAS was successful, return true.
        if(result == old){
            if(v == pool.v){ //
                RBVersionDebra.reclaimLater(old);
                pool.v = new RBVersion(); //Allocate new RBVersion to be used...
            }
            return true;
        }
        else return false; //Else, return false.
    }
    //Propogates the RBVersion objects up the trie.
    //start is the index of an internal RB_AST_Node in the array.
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
    bool insert(int64_t x){
        bool success = false;
        RBVersionDebra.startOp();
        RBVersion *old = leaf[x].version;

        //If old->sum == 1, key was already in set 
        if(old->sum == 0){
            RBVersionPool &pool = versionPool[threadID];
            RBVersion *v = pool.v;
            *v = RBVersion(1,x); //New RBVersion in which sum is 1

            RBVersion *result = old;
            leaf[x].version.compare_exchange_strong(result, v);
            //CAS was successful
            if(result == old){
                success = true;
                //Will try to reclaim the RBVersion that was removed later....
                RBVersionDebra.reclaimLater(old);
                //Allocate a new RBVersion for the pool
                pool.v = new RBVersion();
            }
        }

        //Calculate array index for parent of x.
        int64_t arrayIndex = (x + universeSize  - 1); //Array index of x
        int64_t parent = (arrayIndex - 1) / 2; 
        propogate(parent);
        RBVersionDebra.endOp();
        return success;
    }
    bool remove(int64_t x){
        bool success = false;
        RBVersionDebra.startOp();
        RBVersion *old = leaf[x].version;

        //If old->sum == 1, key was already in set 
        if(old->sum == 1){
            RBVersionPool &pool = versionPool[threadID];
            RBVersion *v = pool.v;
            *v = RBVersion(0,0); //New RBVersion in which sum and key are 0

            RBVersion *result = old;
            leaf[x].version.compare_exchange_strong(result, v);
            //CAS was successful
            if(result == old){
                success = true;
                //Will try to reclaim the RBVersion that was removed later....
                RBVersionDebra.reclaimLater(old);
                //Allocate a new RBVersion for the pool
                pool.v = new RBVersion();
            }
        }

        //Calculate array index for parent of x.
        int64_t arrayIndex = (x + universeSize  - 1); //Array index of x
        int64_t parent = (arrayIndex - 1) / 2; 
        propogate(parent);

        RBVersionDebra.endOp();
        return success;
    }
    // int64_t predecessor(int64_t x){
    //     RBVersionDebra.startOp();

    //     RBVersion *v = array[0].version; //Read the root RBVersion
    //     std::vector<RBVersion*> levels; //Internal nodes while traversing downwards.
    //     int height = trieHeight; //Height of v

    //     //Traverse down to the AS_Node for key x.
    //     while(height > 0){
    //         //Check the (height - 1)-th bit of x.
    //         levels.push_back(v);
    //         int bit = (x >> (height - 1)) & 1; 
    //         if(bit == 0)v = v->left;
    //         else v = v->right;
    //         --height;
    //     }

    //     //v is currently AT_Node for x.
    //     //Perform predecessor query starting from node for x...
    //     int depth = trieHeight; //Depth of v

    //     //x is now the index of v within its row.
    //     //While either v is the left sibling or v.parent.left has a 0 sum
    //     while((x & 1) == 0 || levels[depth - 1]->left->sum == 0){
    //         //Go up to v.parent
    //         x = x >> 1;
    //         --depth;
    //         if(depth == 0){
    //             RBVersionDebra.endOp();
    //             return -1;
    //         }
    //         v = levels[depth];
    //     }

    //     //v = v.parent.left
    //     v = levels[depth - 1]->left;
    //     x = x - (x & 1);
    //     while(depth < trieHeight){
    //         ++depth;
    //         if(v->right->sum > 0){
    //             x = 2 * x + 1;
    //             v = v->right;
    //         }
    //         else if(v->left->sum > 0){
    //             x = 2 * x;
    //             v = v->left;
    //         }
    //         else{
    //             RBVersionDebra.endOp();
    //             return -1;
    //         }
    //     }
    //     RBVersionDebra.endOp();
    //     return x;
    // }
    // bool search(int64_t x){
    //     RBVersionDebra.startOp();
    //     RBVersion *v = array[0].version; //Read the root RBVersion
    //     int height = trieHeight; //Height of v
    //     while(height > 0){
    //         //Check the (height - 1)-th bit of x.
    //         int bit = (x >> (height - 1)) & 1; 
    //         if(bit == 0)v = v->left;
    //         else v = v->right;
    //         --height;
    //     }
    //     bool result = (v->sum == 1);
    //     RBVersionDebra.endOp();
    //     return result;
    // }
    std::string name(){
        return "Fatourou and Ruppert's Augmented Static Trie";
    }


};