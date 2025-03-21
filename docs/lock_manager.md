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
4. an enum to show in which phase of 2PL the transaction is in. [GROWING, SHRINKING]
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
4. a map for each locking mode that says 1. how many locks in the subtree are locked in this mode, 2. which transaction is the majority holder of locks in this mode.
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
A lock tree will be a hearachy of locks like database_locks -> database_file_locks -> record_locks
Its a red black BST with nodes that are the generic base class of all nodes.

It will consist of a head pointer that will point to the first lock that is accuired. All transaction MUST accuire a read lock on the database, then some type of lock on the database file, then on database page. It is not allowed to accuire a lock directly on a page without first accuiring the lock on the file and the database. 

This tree will be traversed from top to buttom by a lock accuire request. New nodes will be inserted if the resource is accuired for the first time.

#### Lock Escalation:
If we maintain two metadata about who is the majority lock holder below this lock level for each locking mode supported by a perticular node and the total number of locks in this subtree we can escalate the lock automatically if:
1. the total number of locks crosses a certain threshold and
2. We have a majority lock holder.

For example, a transaction that is going to modify a billion records in one table. It would be very wasteful to try and accuire a billion locks.

So each transaction must keep a record of all the locks it is holding so as to avoid asking for locks in a subtree when it already has a lock on the root of the subtree.

A transaction must accomodate the fact that it can get a more coarse grained lock than it has requested.

#### Ownership & Cleanup:
The tree is owned by the Lock Manager and will be cleaned by by the same.

### Transaction & all its locks.
There will be a map: [Transaction Indentifier: 
                        List of Accuired locks:[]
                        List of locks its waiting on: []
                     ]
#### Ownership & Cleanup:
This map will be owned by The lock manager and will be cleaned up when a transaction releases all its locks or if it aborts. 
## External Datastructures & Objects
### Request & Response object