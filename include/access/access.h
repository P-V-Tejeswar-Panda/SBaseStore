#ifndef _ACCESS_H_
#define _ACCESS_H_
#include <sys/uio.h>
#include <linux/io_uring.h>
#include <exception>
#include <string>

#define __do_syscall2(NUM, ARG1, ARG2) ({	\
	intptr_t rax;				\
						\
	__asm__ volatile(			\
		"syscall"			\
		: "=a"(rax)	/* %rax */	\
		: "a"((NUM)),	/* %rax */	\
		  "D"((ARG1)),	/* %rdi */	\
		  "S"((ARG2))	/* %rsi */	\
		: "rcx", "r11", "memory"	\
	);					\
	rax;					\
})

typedef struct{
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    unsigned *flags;
    unsigned *array;
}access_sq_ring_t;

typedef struct{
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    struct io_uring_cqe *cqes;
}access_cq_ring_t;
typedef enum{
    INIT  = 1,
    READ  = 2,
    WRITE = 4
}access_failure_t;
class AccessFailure: public std::exception{
    public:
    access_failure_t failure_type;
    std::string failure_msg;
    AccessFailure(access_failure_t failure_type, std::string msg){
        failure_msg = msg;
        this->failure_type = failure_type;
    }
    inline const char* what(){
        return failure_msg.c_str();
    }
};

#include <vector>
typedef enum{
    ACCESS_IO_READ  = 1,
    ACCESS_IO_WRITE = 2
}io_access_t;
typedef struct{
    io_access_t type;
    struct iovec* io_vec;
    void* user_data;
    int fd;
    u_int64_t offset;
    u_int16_t req_count;
}io_request_t;

typedef struct{
    void* user_data;
    int   retcode;
    int   flags;
}completed_io_t;

int syscall_io_uring_setup(unsigned entries, struct io_uring_params *p);
int syscall_io_uring_enter(int ring_fd, unsigned int to_submit,
                                    unsigned int min_complete, unsigned int flags);
class IOHandler{
    private:
    int ring_fd;
    int queue_depth;
    access_sq_ring_t sq_ring;
    access_cq_ring_t cq_ring;
    io_uring_sqe    *sqes;
    int _setup_uring();
    public:
    IOHandler(u_int16_t queue_depth);
    ~IOHandler();
    int enque_access_request(int fd, const io_request_t* request);
    completed_io_t* get_completed_request();
    int get_all_completed_requests(std::vector<completed_io_t*>* vec);
};
#endif