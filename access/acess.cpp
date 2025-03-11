#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "access.h"
#include <stdio.h>
#include <stdlib.h>
#include <linux/fs.h>
#include <unistd.h>

#define read_barrier()  __asm__ __volatile__("":::"memory")
#define write_barrier() __asm__ __volatile__("":::"memory")

int syscall_io_uring_setup(unsigned entries, struct io_uring_params *p){
    return (int) syscall(__NR_io_uring_setup, entries, p);
}

int syscall_io_uring_enter(int ring_fd, unsigned int to_submit,
                           unsigned int min_complete, unsigned int flags){
    return (int) syscall(__NR_io_uring_enter, ring_fd, to_submit,
                         min_complete, flags, NULL, 0);
}

int IOHandler::_setup_uring(){
    struct io_uring_params p;
    void* sq_ptr = NULL;
    void* cq_ptr = NULL;
    char* err_buff = new char[256];

    memset(&p, 0, sizeof(p));
    if((this->ring_fd = syscall_io_uring_setup(this->queue_depth, &p)) < 0){
        perror("syscall_io_uring_setup");
        memset(err_buff, '\0', 256);
        strerror_r(errno, err_buff, 255);
        throw AccessFailure(INIT, std::string(err_buff));
    }

    int sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    int cring_sz = p.cq_off.cqes  + p.cq_entries * sizeof(struct io_uring_cqe);

    if(p.features & IORING_FEAT_SINGLE_MMAP){
        if(cring_sz > sring_sz){
            sring_sz = cring_sz;
        }
        cring_sz = sring_sz;
    }

    sq_ptr = mmap(NULL, sring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE, this->ring_fd, IORING_OFF_SQ_RING);
    if(sq_ptr == MAP_FAILED){
        memset(err_buff, '\0', 256);
        strerror_r(errno, err_buff, 255);
        throw AccessFailure(INIT, std::string(err_buff));
    }

    if(p.features & IORING_FEAT_SINGLE_MMAP){
        cq_ptr = sq_ptr;
    }
    else{
        cq_ptr = mmap(NULL, cring_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, this->ring_fd, IORING_OFF_CQ_RING);
        if(cq_ptr == MAP_FAILED){
            memset(err_buff, '\0', 256);
            strerror_r(errno, err_buff, 255);
            throw AccessFailure(INIT, std::string(err_buff));
        }
    }

    sq_ring.head = (unsigned*) sq_ptr + p.sq_off.head;
    sq_ring.tail = (unsigned*) sq_ptr + p.sq_off.tail;
    sq_ring.ring_mask = (unsigned*) sq_ptr + p.sq_off.ring_mask;
    sq_ring.ring_entries = (unsigned*) sq_ptr + p.sq_off.ring_entries;
    sq_ring.flags = (unsigned*) sq_ptr + p.sq_off.flags;
    sq_ring.array = (unsigned*) sq_ptr + p.sq_off.array;

    sqes = (io_uring_sqe*) mmap(NULL, p.sq_entries*sizeof(struct io_uring_sqe),
                PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, this->ring_fd, IORING_OFF_SQES);

    if(sqes == MAP_FAILED){
        memset(err_buff, '\0', 256);
        strerror_r(errno, err_buff, 255);
        throw AccessFailure(INIT, std::string(err_buff));
    }

    cq_ring.head = (unsigned*) cq_ptr + p.cq_off.head;
    cq_ring.tail = (unsigned*) cq_ptr + p.cq_off.tail;
    cq_ring.ring_mask = (unsigned*) cq_ptr + p.cq_off.ring_mask;
    cq_ring.ring_entries = (unsigned*) cq_ptr + p.cq_off.ring_entries;
    cq_ring.cqes = (io_uring_cqe*) cq_ptr + p.cq_off.cqes;

    return 0;
}
IOHandler::IOHandler(u_int16_t queue_depth){
    this->queue_depth = queue_depth;
    _setup_uring();
}

int IOHandler::enque_access_request(int fd, const io_request_t* request){
    unsigned index      = 0;
    unsigned tail       = 0;
    unsigned next_tail  = 0;
    char* err_buff = new char[256];

    next_tail = tail = *sq_ring.tail;
    next_tail++;
    read_barrier();
    index = tail & *sq_ring.ring_mask;
    struct io_uring_sqe *sqe = &sqes[index];

    sqe->fd         = request->fd;
    sqe->flags      = 0;
    sqe->opcode     = (request->type == ACCESS_IO_READ)?IORING_OP_READV:IORING_OP_WRITEV;
    sqe->addr       = (unsigned long) request->io_vec;
    sqe->len        = request->req_count;
    sqe->off        = request->offset;
    sqe->user_data  = (unsigned long long) request->user_data;
    sq_ring.array[index] = index;
    tail            = next_tail;

    if(*sq_ring.tail != tail){
        *sq_ring.tail = tail;
        write_barrier();
    }

    if(syscall_io_uring_enter(this->ring_fd, 1, 0, IORING_ENTER_GETEVENTS) < 0){
        memset(err_buff, '\0', 256);
        strerror_r(errno, err_buff, 255);
        throw AccessFailure(INIT, std::string(err_buff));
    }
    return 0;
}

completed_io_t* IOHandler::get_completed_request(){
    unsigned head   = 0;
    unsigned reaped = 0;
    struct io_uring_cqe* cqe;
    completed_io_t *ret = NULL;

    head = *cq_ring.head;
    read_barrier();

    if(head == *cq_ring.tail)
        return ret;
    
    cqe = &cq_ring.cqes[head & *cq_ring.ring_mask];
    head++;
    ret = new completed_io_t();
    ret->user_data = (void*) cqe->user_data;
    ret->retcode   = cqe->res;
    ret->flags     = cqe->flags;
    *cq_ring.head = head;
    write_barrier();
    return ret;
}