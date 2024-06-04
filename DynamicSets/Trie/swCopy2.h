#include <stdint.h>
#include <atomic>
#include <queue>
#include "common.h"
#include <set>
#include <vector>
#include <list>
#include <forward_list>
#include "debra.h"

struct Data{
    uintptr_t val;
    std::atomic<uintptr_t> *ptr; //Pointer to atomic pointer
    Data(uintptr_t v = 0, std::atomic<uintptr_t> *p = 0) : val(v), ptr(p){

    }
};

struct Buffer{
    Data data; //Atomic pointer to atomic pointer
    volatile char padding[64 - sizeof(Data)];
    Buffer(Data d = Data()): data(d){

    }
    void init(Data d){
        data = d;
    }
};

//Declaration for structure used by each process to perform Weak LL/SC operations.
struct LLSC_Thread_Data{
    Buffer* link;
    Buffer *reuse;
    volatile char padding[64 - 2*sizeof(Buffer*)]; 
    LLSC_Thread_Data(): link(nullptr), reuse(new Buffer()){
    }
    ~LLSC_Thread_Data(){
        delete reuse;
    }
};

//Memory used by each process to perform WeakLLSC
LLSC_Thread_Data llscData[MAX_THREADS];
Debra<Buffer, 5> bufferDebra;

//Definition of the LLSC object.
struct LLSC{
    std::atomic<Buffer*> buf;
    LLSC(Data initialValue){
        buf = new Buffer(initialValue);
    }
    ~LLSC(){
        delete buf;
    }
    void LL(Data &val){
        LLSC_Thread_Data &data = llscData[threadID];
        Buffer *tmp = buf;
        data.link = tmp;
        val = tmp->data;
    }
    bool SC(Data newVal){
        LLSC_Thread_Data &data = llscData[threadID];
        Buffer *old = data.link;
        Buffer *newBuf = data.reuse;
        newBuf->init(newVal);

        Buffer *result = old;
        buf.compare_exchange_strong(result,newBuf);
        if(result == old){ //CAS succeeded
            data.reuse = new Buffer();
            bufferDebra.retire(old);
        }
        data.link = nullptr;
        return result == old; //Return whether SC succeeded.
    }
    bool VL(){
        LLSC_Thread_Data &data = llscData[threadID];
        Buffer *old = data.link;
        return buf == old;
    }
    void CL(){
        LLSC_Thread_Data &data = llscData[threadID];
        data.link = nullptr;
    }
};

//Definition of the single writer atomic copy object.
struct SW_AtomicCopy{
    LLSC data;
    volatile char padding[64 - sizeof(LLSC)];
    std::atomic<uint64_t> old;
    volatile char padding2[64 - sizeof(std::atomic<uint64_t>)];
    SW_AtomicCopy(uintptr_t initVal): data(Data(0,0)), old(0){
        write(initVal);
    }
    void swcopy(std::atomic<uintptr_t> *src){
        bufferDebra.startOp();
        Data d;
        data.LL(d);
        old = d.val;
        data.SC(Data(0,src));
        uintptr_t val = *src;
        data.LL(d);
        if(d.ptr != 0){
            data.SC(Data(val,0));
        }
        else{
            data.CL();
        }
        bufferDebra.endOp();
    }
    void write(uintptr_t newVal){
        bufferDebra.startOp();
        Data d;
        data.LL(d); //WeakLL guaranteed to succeed.
        old = d.val;

        Data newData(newVal, 0);
        data.SC(newData);
        bufferDebra.endOp();
    }
    intptr_t read(){
        bufferDebra.startOp();
        Data d;
        data.LL(d);
        if(d.ptr == 0){
            data.CL();
            bufferDebra.endOp();
            return d.val;
        }
        uintptr_t v = d.ptr->load();
        if(data.SC(Data(v,0))){
            bufferDebra.endOp();
            return v;
        }
        data.LL(d);
        data.CL();
        bufferDebra.endOp();
        if(d.ptr == 0){
            return d.val;
        }
        return old;
    }
};
