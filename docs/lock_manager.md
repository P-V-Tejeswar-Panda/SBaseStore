# Lock Manager
This page describes the inner working of the database's lock manager.

## Input / Output
- A transaction make a lock(X, ConditionVariable T_Cond, Response* response) request on resource X:
  Resource X has the following properties:

  1. Resource X should resolve to a unique lock in the tree herarchy. Example: X could be: Tuple 3 of page 9 in file accounts.main of the database 'banking'. X should also have the information on what kind of lock it requires.
  2. T_Cond is the conditional variable that will be used by the lock manager to wake up the waiting tansaction thread.
  3. response is a object that is owned by the transaction thread and will be used by Lock-Manager to send information to the thread on whether lock is granted, or the transaction must abort, or its a unlawful request, etc.
  
  Object X will be owned by transaction thread.
  
  T_Cond will be owned by the transaction thread.
  
  Response object will be created and owned by transaction thread.

  Response: The response to this request could be delayed.

  1. If and when the lock is accuired, the lock manager will put accuired message in response object and  call signal on the conditional variable T_Cond.
  2. If the transaction needs to abort, it puts abort in response object and calls signal on T_Cond.

- A transaction makes a unlock(X, Response* response):
  
  Response: The thread doesn't need to wait for response from the lock manager while releasing the lock on an object X.
  The lock manager will put its response in the `response` object and the transaction may do a sanity check on this response before committing or exiting.

The request and response object may be combined into one object as that will make cleanup easier and response will be more contextulized.

## Internal Datastructures
### Request Queue
This is the queue that holds all the lock and unlock requests. Since this is shared between multiple transaction or vaccuum threads and the lock manager it will have latches. A spin lock should be enough.
#### Interfaces:
1. `enqueue_request()`:
2. `dequeue_request()`:

#### Owneship:
This resource will be owned by the Lock Manager and will be cleaned up by the same.
### Locks
This represents a resource that is or will be locked by one or more transaction. This is a generic data structure that consists of the following parts:

1. A resource identifier
2. A set of accuired queues.

   - there will a queue for each indivisual locking mode like: `Shared`, `eXclusive`, `Intention Shared`, `Intention Exclusive`, `Shared Intention eXclusive` etc.
   - Each queue will contain one or more ThreadsIDs who have accuired the lock.
3. A set of waiting Queues.

   - there will a queue for each indivisual locking mode like: `Shared`, `eXclusive`, `Intention Shared`, `Intention Exclusive`, `Shared Intention eXclusive` etc.
   - Each queue will contain one or more ThreadsIDs who are waiting to accuire the lock.
### Lock Trees
### Transaction & all its locks.

## External Datastructures & Objects
### Request & Response object