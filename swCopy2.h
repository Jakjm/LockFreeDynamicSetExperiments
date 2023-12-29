#include <stdint.h>
#include <atomic>
#include <queue>
#include "common.h"
#include <set>
#include <vector>
#include <list>
#include <forward_list>
#include "debra.h"

//Definition of the data stored by the weak LL/SC objects used for our SW_AtomicCopy implementation.
struct Data{
    uintptr_t val;
    std::atomic<uintptr_t> *ptr; //Pointer to an atomic uintptr_t.
    Data(): val(0), ptr(0){

    }
    Data(uintptr_t v, std::atomic<uintptr_t> *p): val(v), ptr(p){

    }
};



struct Buffer{
    std::atomic<uintptr_t> val; //Atomic pointer
    std::atomic<std::atomic<uintptr_t> *> ptr; //Atomic pointer to atomic pointer
    volatile char padding[64 - sizeof(bool) - sizeof(int) - sizeof(Data)];
    Buffer(Data v = Data(), int pid=-1, bool seen=false): val(v.val), ptr(v.ptr){

    }
    void init(Data &v){
        val.store(v.val);
        ptr.store(v.ptr);
    }
};

//Declaration for structure used by each process to perform Weak LL/SC operations.
struct Weak_LLSC_Thread_Data{
    std::atomic<Buffer*> announcement; 
    volatile char padding1[64 - sizeof(std::atomic<Buffer*>)];
    Buffer *reuse;
    volatile char padding[64 - sizeof(Buffer*)]; 
    Weak_LLSC_Thread_Data(): announcement(nullptr){
        reuse = new Buffer();
    }
    ~Weak_LLSC_Thread_Data(){
        delete reuse;
    }
};

//Memory used by each process to perform WeakLLSC
Weak_LLSC_Thread_Data llscData[MAX_THREADS];
Debra<Buffer, 4> bufferDebra;

//Definition of the WeakLLSC object.
struct WeakLLSC{
    std::atomic<Buffer*> buf;
    WeakLLSC(Data initialVal){
        buf = new Buffer(initialVal);
    }
    ~WeakLLSC(){
        delete buf;
    }
    Data wLL(bool *succ = nullptr){

        Weak_LLSC_Thread_Data &data = llscData[threadID];
        Buffer *tmp = buf;
        data.announcement.store(tmp);

        if(buf == tmp){
            if(succ)*succ = true;
            return Data(tmp->val,tmp->ptr);
        }
        else{
            if(succ)*succ = false;
            return Data();
        }
    }
    bool SC(Data newVal){
        Weak_LLSC_Thread_Data &data = llscData[threadID];
        Buffer *old = data.announcement.load();
        Buffer *newBuf = data.reuse;
        newBuf->init(newVal);

        Buffer *result = old;
        buf.compare_exchange_strong(result,newBuf);
        if(result == old){ //CAS succeeded
            data.reuse = new Buffer();
            bufferDebra.retire(old);
        }
        data.announcement.store(nullptr);
        return result == old; //Return whether SC succeeded.
    }
    bool VL(){
        Weak_LLSC_Thread_Data &data = llscData[threadID];
        Buffer *old = data.announcement.load();
        return buf == old;
    }
    void CL(){
        Weak_LLSC_Thread_Data &data = llscData[threadID];
        data.announcement.store(nullptr);
    }
};

//Definition of the single writer atomic copy object.
struct SW_AtomicCopy{
    WeakLLSC data;
    volatile char padding[64 - sizeof(WeakLLSC)];
    std::atomic<uint64_t> old;
    volatile char padding2[64 - sizeof(std::atomic<uint64_t>)];
    SW_AtomicCopy(uintptr_t initVal): data(Data(0,0)){
        write(initVal);
    }
    void swcopy(std::atomic<uintptr_t> *src){
        bufferDebra.startOp();
        bool succ;
        Data d = data.wLL(); //WeakLL guaranteed to succeed.
        old = data.wLL().val;
        data.SC(Data(0,src));
        uintptr_t val = *src;
        d = data.wLL(&succ);
        if(succ && d.ptr != 0){
            data.SC(Data(val,0));
        }
        else{
            data.CL();
        }
        bufferDebra.endOp();
    }
    void write(uintptr_t newVal){
        bufferDebra.startOp();
        data.wLL(); //WeakLL guaranteed to succeed.
        Data newData(newVal, 0);
        data.SC(newData);
        bufferDebra.endOp();
    }
    intptr_t read(){
        bufferDebra.startOp();
        bool succ;
        Data d = data.wLL(&succ);
        if(!succ){
            d = data.wLL(&succ);
            if(!succ){
                bufferDebra.endOp();
                return old;
            }
        }
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
        d = data.wLL(&succ);
        data.CL();
        bufferDebra.endOp();
        if(succ && d.ptr == 0){
            return d.val;
        }
        return old;
    }
};
