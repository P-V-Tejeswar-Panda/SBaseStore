/*
* The Buffer Pool Manager runs in a loop:
* 1: Check for page requests the page_request_queue.
* 2: Request the page replacement algorithim for the page. If the page is in
*    memory the PageFrame is returned at once and return the step #1 else step 3
* 3: 
* The Buffer Pool Manager consists of the following components:
* 1: The 'Request Queue' containing 'Requests'
* 2: The Context Queue containing 'Contexts'
* 2: The 'Event Loop'
* 
* Event Loop:
*    1. Pick as many request from request_queue and convert it to a context object with the ready flag set.
*    2. Put the context objects at the back of the context queue.
*    3. pop out the front element from the context queue and check if the ready
*       flag is set. if not move it to the back of the queue. do this until you
*       one that has its ready flag set.
*    4. process the context untill you get to the end of a blocking step in the context
*       once there, enque the IO request and put the context object at the back of the context queue.
*       if the context object has no more steps left; signal the waiting thread that it can proceed.
*    5. Check for IO request that are complete and set the corresponding context to 'ready'.
*    6. return to step one if there are new requests else to step 3.
*
* Context Object:
*    Consits of:
*    1. The request
*    2. The state list like: [ (A) Idle
*                              (A)request the replacement algorithim to find the page, 
*                              (D) If page in memory, 
*                              (AB) Write back victim page,
*                              (AB) read in the requested page,
*                              (A) failure
*                              (A) Success
*                              (A) complete]
*      (A)  -> action State
*      (AB) -> Action and Blocking state. the is a good point to switch context
*      (D)  -> Decision state
*   3. The 'Next state pointer': pointing to the next state to be executed.
*   4. The 'Ready Flag': Marking whether the context is ready to be processed further.
*   5. The complete state: when this state is reached, the context is destroyed by the
*      event loop.
*   Functioning:
*   1. Init the Context object using request object and put in context queue.
*      Set the initial state to 'Idle' and 'Ready Flag' to true.
*   2. Once the event loop pick up the context, it starts processing the currently
*      set state to competion. If this state is marked as a blocking state, context
*      switch happens else we proceed to the next state.
*   3. Before going, to next state the current state is set to next state and Ready flag
*      is set to false.
*   4. If the event loop decides to process the state now it will set the ready flag to true
*      and proceed.
*   5. If a switch happens the context is put back in the context queue. When this happens,
*      the flag will be set to ready at a later time when the blocking IO call is complete.
*/
#ifndef _BUFFER_POOL_H_
#define _BUFFER_POOL_H_

#include <queue>
#include <atomic>
#include <pthread.h>
#include "page.h"

typedef struct {
    int fd;
    uint64_t offset;
}page_loc_t;
class PageFrame{
    public:
    Page                *page;
    page_loc_t          *location;
    std::atomic_flag     page_pin;
    std::atomic_flag     header_latch;
    pthread_mutex_t      page_latch;
};
class BufferPool{
    public:
    BufferPool(uint64_t pool_size);
    PageFrame* read_page(uint64_t page_no, int fd);
    int write_page(PageDirectory* directory, PageFrame* frame, int fd);
};

typedef struct{
    bool flushing_required;
    PageFrame* page_frame;
}replacement_algo_ret_t;
class ReplacementAlgo{
    BufferPool* pagePool;
    public:
    ReplacementAlgo(BufferPool*, void* args);
    replacement_algo_ret_t get_page(uint64_t page_no);
};
typedef enum{
    READ  = 0,
    WRITE = 1
} request_t;

class Request{
    public:
    request_t        request_type;
    PageFrame**      frame;
    pthread_cond_t  *beacon;
    Request(request_t, PageFrame**, pthread_cond_t*);
};
class Context;
class State{
    Context *cxt;
    bool is_run_complete;
    public:
    State(){};
    virtual ~State(){};
    virtual void run() = 0;
    virtual bool is_blocking() = 0;
    virtual State* get_next_state() = 0;
};

class IdleState: public State{
    Context *cxt;
    bool is_run_complete;
    State* next_state = NULL;
    public:
    IdleState(Context* cxt);
    ~IdleState();
    void run();
    bool is_blocking();
    State* get_next_state();
};

class ReadState: public State{
    Context *cxt;
    bool is_run_complete;
    public:
    ReadState(Context* cxt);
    ~ReadState();
    void run();
    bool is_blocking();
    State* get_next_state();
};

class WriteState: public State{
    Context *cxt;
    bool is_run_complete;
    public:
    WriteState(Context* cxt);
    ~WriteState();
    void run();
    bool is_blocking();
    State* get_next_state();
};
class Context{
    public:
    Request* req;
    bool     is_ready;
    State*   current_state;
    void run();
    Context(Request* req);
};
class EventLoop{
    std::queue<Context*> context_queue;
    std::queue<Request*> request_queue;
    ReplacementAlgo* replacement_algo;

    pthread_mutex_t *request_queue_lock;
    pthread_cond_t  *loop_wake_up_cond;
    pthread_mutex_t *loop_wake_up_lock;

    std::atomic_bool stop_flag;
    int req_to_context();
    void run_with_context(Context*);
    bool check_request_q_empty_locked();
    public:
    EventLoop(ReplacementAlgo*);
    ~EventLoop();
    void start();
    void stop();
    int enque_request(Request*);
};
#endif