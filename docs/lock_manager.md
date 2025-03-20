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

### Transaction Identifier
This is used to encapsulate the following:
1. The transaction id.
2. a pointer to Conditional variable the thread is waiting on.
3. a pointer to the Response field.
#### Ownership:

### Lock Queue Entry
This the node of the doubly linked list that forms the queues of the lock.
Contains the following:
1. a pointer to a transaction identifier
2. a pointer to the lock
3. a forward pointer.
4. a backward pointer.

### Interface:
If we put the logic here, the exiting thread can directly call exit on this rather than going through the request queue. but there
are a few challenges:
1. The locking mechanism won't be single threaded anymore.
2. This will expose the internal workings of the lock manager to the rest of the system. this can be avoided if we can use a static method the the lock manager class to do the handling.

This will be passed to the transaction thread and will be returned to the lock manager while requesting release of a lock.
#### Ownership:
This is owned by the lock.
#### Cleanup strategy:
Each time a transaction is taken off the queue, this is cleaned up by the Lock Manager.

### Locks
This represents a resource that is or will be locked by one or more transaction. This is a generic data structure that consists of the following parts:

1. A resource identifier
2. left & right child pointers
3. parent pointer
4. A set of accuired lists:

   - there will be a doubly linked list for each indivisual locking mode like: `Shared`, `eXclusive`, `Intention Shared`, `Intention Exclusive`, `Shared Intention eXclusive` etc.
   - Each queue will contain one or more Lock Queue Entry who have accuired the lock.
5. A waiting Queue:

   - there will be a doubly linked list for each indivisual lock request.
   - Will contain one or more Lock Queue Entry who are waiting to accuire the lock.
#### Interfaces:
1. set of `accuire_lock()` methods for each kind of supported lock.
2. `lock_released()` notification method.
3. `drop_lock()`: deletes the lock and releases all the resources.
4. `accept()`: this will accept a lock granting algorithim. Visitor pattern.

#### Ownership and Cleanup:
This will be owned by the lock tree and if all the queues are empty it will be cleaned up by the lock manager.

### Lock Trees
### Transaction & all its locks.

## External Datastructures & Objects
### Request & Response object