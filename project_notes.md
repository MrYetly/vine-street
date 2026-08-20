Maybe there is some way to benchmark aio performance?

# The Vine Street HTTP Web Server

## Non-blocking file descriptors

Many of the technologies implemented here are represented by Linux as file descriptors. As such, any I/O operations associated with them can be set to be blocking or non-blocking. When a file descriptor is set to blocking, the process that calls `read()` or `write()` on it will be put to sleep by the kernel until the kernel has prepared the read or write buffer. When it is set to non-blocking, the kernel will either return a partial read or write if there is data present, or it will return -1 and set `errno` to `EAGAIN` OR `EWOULDBLOCK` if there is no data in or room available on the buffer, indicating the process should try again later.

It should be emphasized that this does not apply to "normal" files, such as CSV, txt, or HTML. Reading or writing these files always bblocks.

## Epoll

Epoll is an api provided by the linux OS. The main way a user interacts with it is through an epoll instance, which lives in memory and is entirely managed by the kernel. The instance is associated with a process and consists of two lists, an interest list containing file descriptors the process wants to monitor and a ready list containing references to a subset of the interest list indicating while file descriptors are ready for I/O. A file descriptor is ready for I/O if an I/O call won't block the thread or process that calls it; for example, a file's or and HTTP request's data is in the kernel's buffer and can be immediately accessed with `read`, or the kernel's write buffer is ready to be written to with `write` to send data to a file or create an HTTP response. The catch with `epoll` is that it can only track file descriptors that implement the `.poll()` operation, which most normal files don't (i.e. CSVs, txt, etc). In the style of Linux, an epoll instance is itself a file descriptor.

```C
int epoll_fd = epoll_create1(EPOLL_CLOEXEC);//using CLOEXEC is defensive, incase other libraries use fork() + exec()

struct epoll_event ev = {
	.events = EPOLLIN,
	.data.fd = server_df,
};
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
```

The epoll instance interfaces with events, created either by application logic, or by the user to configure the instance. Above, we use the `EPOLLIN` parameter to signal that `server_fd` is available for read operations. We pass that signal to the epoll instance `epoll_fd` with `epoll_ctl`, which adds the functionality signaled by the event. The result is that our socket will show up in the epoll instance's ready list whenever one or multiple HTTP requests are ready to be read. Epoll operates at the byte level, not the socket level, so it doesn't know that the bytes ready to be read are HTTP requests; it will simply keep marking the socket as ready as long as there is data from HTTP requests in its buffer.

```C
struct epoll_event events[MAX_EVENTS];

//inside dispatch loop:

int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); //-1 sets no timeout, blocks until events occur
```

We wait for file descriptors on the interest list to populate the ready list by calling `epoll_wait`, which returns up to `MAX_EVENTS` events in the `events` buffer.

## eventfd

As mentioned above, "normal" files cannot be tracked with `epoll`, so epoll cannot be used by itself to enable asynchronous I/O for "normal" files, including HTML. This is where eventfd comes in. `eventfd()` creates an eventfd object that can used to signal when a file is ready for I/O, and this object can be tracked by epoll.

```C
notify_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
ev.events = EPOLLIN;
ev.data.fd = notify_fd;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notify_fd, &ev);
```
An eventfd object is essentially a 64-bit counter. Every time write() is called on it with an 8 byte buffer, that buffer is added to the counter. Every time read() is called, the counter's value returned and it is reset to 0. We can see this in action in `worker_thread_func()`, in which after performing some blocking operation, the worker thread signals it is done by writing `1` to `notify_fd`, which causes epoll to present `notify_fd` on the ready list, signalling the dispatch loop that a response is ready to be made.

## Async worker threads

The main function of our server is to serve "normal" files. Opening and reading files always blocks whatever thread is carrying out the operation. To get around this, we will have multiple worker threads separate from the server's dispatch loop. These workers individually load the files to serve. We rely on the OS's scheduler to juggle which worker is active, letting it efficiently activate workers who whose data has been loaded and sleep those who are waiting for hardware to load a file from disk into memory. (For network I/O, the analogue is the CPU sleeps threads waiting for data to load from the network interface card to memory). By splitting off these workers, our server is freed to more or less continuously accept requests and send responses.

These workers need to coordinate so they do not try and carry out the same load operation. To enable this, we create mutexs and a condition variable.

**Mutex**: short for mutual exclusion lock, as in, if one thread has the lock, no other threads can. It is an object in memory, a struct, that tracks whether a lock is owned, which thread has ownership of the lock, and which threads are waiting for the lock. The programmer is responsible for implementing this lock properly, such that a thread must ask for the lock (`pthread_mutex_lock()`) before executing a critical section of code, and releasing the lock when it is done (`pthread_mutex_unlock()`).

**Condition Variable**: the primary purpose of a condition variable in this application is to coordinate waking and waiting among multiple threads. It does this by allowing the server thread to manage a condition (whether there is work to be done or not), and to signal that condition to worker threads waiting for it to become true (there is work to be done). It is a struct that carries within it a linked list of handles for threads waiting for the condition to become true, as well as other data to help prevent lost signals.

Note the queues within mutexs and condition variables are managed by the OS kernel and pthread library, so they have no max limit besides that dictated by memory.

In our server, mutexs are created for work to be done and for finished work. A condition variable is only created for work to be done. We do not create a conditoin variable for finished work becuase there is only one thread processing finished work, the server, and it already has its logic for when it waits and when it activates provided by epoll. 

```C
//create work queue
task_t work_queue[QUEUE_SIZE];
int work_head = 0, work_tail = 0;
pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;

//create done queue
task_t done_queue[QUEUE_SIZE];
int done_head = 0, done_tail = 0;
pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
```

### Create Worker to run on Separate Thread

```C
pthread_t worker;
pthread_create(&worker, NULL, worker_thread_func, NULL);
```

`pthread_create()` will create a new thread in the same process that `app` is running in. This new thread runs the worker loop that will do blocking work, freeing up our server to continue to process requests. Since threads in a process share memory, both the app thread and the worker thread can access `work_queue` and `done_queue`. It is important to understand that the threads created by `pthread_create` are software abstractions, the number you create is not limited by hardware: they are not the same as the number of simultaneous instructions sets that can be executed by your machine, these are hardware threads. As a software abstraction, a `pthreads` thread is handled by the OS scheduler, and so if the scheduler notices that the thread is waiting, it will let another thread proceed until all of your hardware threads are executing a software thread or all remaining software threads are blocked. In the context of multiple workers loading HTML for the server, the OS scheduler will process other work whenever it notices that a worker is waiting for hardware to deliver the data it requested from the disk to memory. So, if we create multiple worker threads, the OS scheduler can go on dispatching reads across those workers, and our server can more or less continuously process reading requests and writing responses without every waiting for I/O to finish.

### Read Client request and enqueue work

```C
char buf[512];
read(fd, buf, sizeof(buf)); // Read HTTP request

// Offload disk task to worker thread pool
pthread_mutex_lock(&work_mutex);
work_queue[work_tail].client_fd = fd;
work_tail = (work_tail + 1) % QUEUE_SIZE;
pthread_cond_signal(&work_cond);
pthread_mutex_unlock(&work_mutex);
```

Since we are manipulating data that two threads are operating on, we must get the mutex lock to make sure any operations are thread safe, this is why we wrap the code interacting with the `work_queue` and signaling the worker that work is ready (`pthread_cond_signal`). Note that `work_queue` is populated with client connections, we will also need to populate it with the buffer filled by `read`, probably using snprintf.

Work is added to the tail of the queue; the worker will process from the head. A circular queue is implemeted by the pattern `(work_tail + 1) % QUEUE_SIZE`, this prevents both buffer overflows and easily enables the tasks in the queue to wrap around the buffer. (When do I have to worry about the tail lapping the head?).

Finally, the worker is signaled with `pthread_cond_signal`, which wakes up one worker.

### Worker loop processes job

```C
// Worker Thread Loop
void* worker_thread_func(void* arg) {
    while (1) {
        // 1. Fetch work task from queue
        pthread_mutex_lock(&work_mutex);
        while (work_head == work_tail) {
            pthread_cond_wait(&work_cond, &work_mutex);
        }
        Task task = work_queue[work_head];
        work_head = (work_head + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&work_mutex);

	// Implement handler/callback pattern here?

        // 3. Push completed task to completed queue
        pthread_mutex_lock(&done_mutex);
        done_queue[done_tail] = task;
        done_tail = (done_tail + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&done_mutex);

        // 4. Signal main thread via eventfd
        uint64_t signal_val = 1;
        eventfd_write(notify_fd, signal_val);
    }
    return NULL;
}
```

This hypothetical worker has been waiting, blocked, ever since `pthread_cond_wait` was called. It is important to remember that `pthread_cond_wait` releases the mutex when it blocks the thread, and then waits to get it back when the thread is unblocked by the OS. It goes back into action when the dispatch loop calls `pthread_cond_signal`. At this point, whatever blocking operation can take place, and it won't stop the server from accepting requests and enqueueing work for the other worker threads. Once the work is done, the lock for the `done_queue` is requested and the task is added to the queue's tail.

## callback/handler interface design

**Callback**: a function pointer passed as an argument into another piece of code so it can be executed later. Allows the main server loop to just pass data onto the function to be executed, instead of implementing that logic in the loop.

```C
// Define standard HTTP request and response structures
typedef struct { /* path, method, headers, body */ } http_request_t;
typedef struct { /* status_code, headers, body */ } http_response_t;

// Define a function pointer type for all route handlers
typedef void (*http_handler_t)(const http_request_t *req, http_response_t *res);
```

The first two typedefs are an example of how we structure variables that contain HTTP requests and responses. We define new types, which are structs, for these variables, to ensure those variables have a certain structure. For example, a request should have a path and method of type character array, headers of type array of character arrays, and a body that could be any number of types.

The third typedef is the type of the callback itself. We create a type of function pointer named `http_handler_t`. Function pointers of this type point to functions that always take two pointers as arguments, `req` and `res`, and these pointers always point to structsof type `http_request_t` and `http_response_t`, respectively.

**Handler**: the function associated with the callback, the function pointer. Here is where the backend logic is implemented.

```C
void handle_home(const http_request_t *req, http_response_t *res) {
    // Logic for GET /
}

void handle_not_found(const http_request_t *req, http_response_t *res) {
    // Logic for 404 Not Found
}
```

These are the functions that handle the server's response to a request. When the user accesses a certain path, like `/` or `/login`, a function exists for that route and handles it. If there is an error, another function handles surfacing that response to the user. 


**Route Table**: map URI paths to callback functions.

```C
typedef struct {
    const char *method;
    const char *path;
    http_handler_t handler;
} Route;

Route routes[] = {
    {"GET", "/", handle_home},
    {"GET", "/about", handle_about}
};
```

The route table is an array of `Route`s. Each `Route` tells how to process a kind of request sent to a certain path, by specifying what handler is to be called.

## Accepting client connections

`EPOLLONESHOT` is used to make sure a client connection only triggers an epoll event once, when it first receives data. Epoll operates at the byte level, so it doesn't know what an HTTP request is. If multiple requests come in, and two epoll events fire, then two different worker threads might read parts of the first request. With `EPOLLONESHOT`, only one event fires, so only one worker handles all of the incoming data (at least, effectively, as long as HTTP/1.1 is used).

## debug

Right now, epoll just puts client fds on the ready list when they have data that can be read. It should block everytime there is no data to be read from the server, client, or eventfd.
