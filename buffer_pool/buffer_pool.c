#include "buffer_pool.h"
#include "access.h"

IdleState::IdleState(Context* cxt){
    this->cxt = cxt;
}
void IdleState::run(){
    if(cxt->req->request_type == READ){
        next_state = new ReadState(cxt);
    }
    else{
        next_state = new WriteState(cxt);
    }
    cxt->is_ready = false;
    is_run_complete = true;
}

bool IdleState::is_blocking(){
    return false;
}

State* IdleState::get_next_state(){
    if(is_run_complete)
        return next_state;
}

ReadState::ReadState(Context* cxt){
    this->cxt = cxt;
}

void ReadState::run(){
    // enque_read_request
    cxt->is_ready = false;
}

bool ReadState::is_blocking(){
    return true;
}

State* ReadState::get_next_state(){
    if(is_run_complete){
        
    }
}

/*---------------------- Event Loop implemetations ---------------------------*/

int EventLoop::req_to_context(){
    pthread_mutex_lock(this->request_queue_lock);
    while(!request_queue.empty()){
        context_queue.push(new Context(request_queue.front()));
        request_queue.pop();
    }
    pthread_mutex_unlock(this->request_queue_lock);
}

bool EventLoop::check_request_q_empty_locked(){
    bool ret = false;
    pthread_mutex_lock(this->request_queue_lock);
    ret = request_queue.empty();
    pthread_mutex_unlock(this->request_queue_lock);
    return ret;
}

EventLoop::EventLoop(ReplacementAlgo* rpl_algo){
    this->replacement_algo = rpl_algo;
    pthread_mutex_init(this->request_queue_lock, NULL);
}

int EventLoop::enque_request(Request* req){
    // TODO: add a lock around this.
    pthread_mutex_lock(this->request_queue_lock);
    this->request_queue.push(req);
    pthread_mutex_unlock(this->request_queue_lock);
}

void EventLoop::start(){
    while(stop_flag != true){
        // TODO: suround this check with request_queue_lock else it
        // might lead to deadlocks. 
        if(check_request_q_empty_locked() && request_queue.empty()){
            // going to sleep.
            pthread_mutex_lock(this->loop_wake_up_lock);
            // check if calling check_request_q_empty_locked while holding the
            // loop lock may lead to deadlock.
            while(!check_request_q_empty_locked()){
                pthread_cond_wait(this->loop_wake_up_cond, this->loop_wake_up_lock);
            }
            pthread_mutex_unlock(this->loop_wake_up_lock);
        }
        // Does checking request_queue.empty require a lock ?
        // Probably not, we will take care of the new request in the next iteration.
        // The above check guarentees that there will be atleast one item
        // in the request queue.
        if(!request_queue.empty()){
            req_to_context();
        }
        // checking the context_queue for being empty is not required as the
        // previous checks ensure that the queue is not empty or if it was empty
        // in the beginning, it must have been filled by req_to_context.
        Context* cxt = context_queue.front(); context_queue.pop();
        run_with_context(cxt);
    }
}

void EventLoop::stop(){
    stop_flag = true;
}