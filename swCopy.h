#include <stdint.h>
#include <atomic>
#include <queue>
#include "common.h"
#include <set>
#include <vector>
#include <list>
#include <forward_list>

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
    std::atomic<int> pid;
    std::atomic<bool> seen;
    volatile char padding[64 - sizeof(bool) - sizeof(int) - sizeof(Data)];
    Buffer(Data v = Data(), int pid=-1, bool seen=false): val(v.val), ptr(v.ptr), pid(-1), seen(false){

    }
    void init(Data &v){
        val.store(v.val);
        ptr.store(v.ptr);
        pid = -1;
        seen = false;
    }
};

//Declaration for structure used by each process to perform Weak LL/SC operations.
struct Weak_LLSC_Thread_Data{
    std::atomic<Buffer*> announcement; 
    volatile char padding1[64 - sizeof(std::atomic<Buffer*>)];
    std::list<Buffer*> rList;
    std::vector<Buffer*> fList;
    volatile char padding[64 - 2*sizeof(std::vector<Buffer*>)]; 
    Weak_LLSC_Thread_Data(): announcement(nullptr){
        //Initially push 2 elements onto the stack...
        for(int i = 0;i < 2*MAX_THREADS;++i){
            fList.push_back(new Buffer());
        }
    }
};

//Memory used by each process to perform WeakLLSC
Weak_LLSC_Thread_Data llscData[MAX_THREADS];

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
        Buffer *newBuf = data.fList.back();
        newBuf->init(newVal);

        //If CAS
        Buffer *result = old;
        buf.compare_exchange_strong(result,newBuf);
        if(result == old){
            data.fList.pop_back();
            retire(old);
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
    void retire(Buffer *old){
        Weak_LLSC_Thread_Data &data = llscData[threadID];
        data.rList.push_front(old);
        if(data.fList.size() == 0){ //If we are out of buffers to use, free ones that are in use...
            std::vector<Buffer*> reserved;
            for(int j = 0; j < MAX_THREADS;++j){
                //TODO if announce non-null?
                Buffer *announceJ = llscData[j].announcement;
                if(announceJ)reserved.push_back(announceJ);
            }

            //Put pointers to buffers that are not announced by processes into fList to be reused.
            for(Buffer *buf : data.rList){
                buf->pid = threadID;
                buf->seen = false; 
            }
            for(Buffer *buf : reserved){
                if(buf->pid == threadID){
                    buf->seen = true; //Mark that we saw this when going through announcment list.
                }
            }
            auto it = data.rList.begin();
            while(it != data.rList.end()){
                Buffer *buf = *it;
                buf->pid = -1;
                if(!buf->seen){ //If this buffer was not seen, we can reuse it!
                    auto prev = it++;
                    data.rList.erase(prev);
                    data.fList.push_back(buf);
                }
                else{
                    ++it;
                }
            }
        }
    }
};

//Definition of the single writer atomic copy object.
struct SW_AtomicCopy{
    WeakLLSC data;

    volatile char padding1[64 - sizeof(WeakLLSC)];
    std::atomic<uint64_t> old;
    volatile char padding2[64 - sizeof(std::atomic<uint64_t>)];
    SW_AtomicCopy(uintptr_t initVal): data(Data(0,0)){
        write(initVal);
    }
    void swcopy(std::atomic<uintptr_t> *src){
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
    }
    void write(uintptr_t newVal){
        data.wLL(); //WeakLL guaranteed to succeed.
        Data newData(newVal, 0);
        data.SC(newData);
    }
    intptr_t read(){
        bool succ;
        Data d = data.wLL(&succ);
        if(!succ){
            d = data.wLL(&succ);
            if(!succ)return old;
        }
        if(d.ptr == 0){
            data.CL();
            return d.val;
        }
        uintptr_t v = d.ptr->load();
        if(data.SC(Data(v,0))){
            return v;
        }
        d = data.wLL(&succ);
        data.CL();
        if(succ && d.ptr == 0){
            return d.val;
        }
        return old;
    }
};
