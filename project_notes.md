Maybe there is some way to benchmark aio performance?

# The Vine Street HTTP Web Server

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

**Dispatch Loop**: the while loop that monitors the socket for client connections and requests, and brings responses from a route to the client connection.

## Async via epoll
Things aren't as simple as simply switching to epoll and getting the same asynchronous behavior as FastAPI.

**Mutex**: short for mutual exclusion lock, as in, if one thread has the lock, no other threads can. It is an object in memory, a struct, that tracks whether a lock is owned, which thread has ownership of the lock, and which threads are waiting for the lock. The programmer is responsible for implementing this lock properly, such that a thread must ask for the lock (`pthread_mutex_lock()`) before executing a critical section of code, and releasing the lock when it is done (`pthread_mutex_unlock()`).

**Condition Variable**: allows a thread to manage the state of an application, and to signal that state to other threads waiting for that condition to become true. It is a struct that carries within it a linked list of handles for threads waiting for the state to become true, and a sequence counter that tracks signals.

Not the queues are managed by the OS kernel and pthread library, and have no max limit besides what is constrained by memory. A mutex is created for work to be done and finished work, a condition variable is created for work to be done only (why?).

```C
//create work queue
task_t work_queue[WORK_QUEUE_SIZE];
int work_head = 0, work_tail = 0;
pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;

//create done queue
task_t done_queue[QUEUE_SIZE];
int done_head = 0, done_tail = 0;
pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
```

**eventfd**: essentially a 64-bit counter. Every time write() is called with an 8 byte buffer, that buffer is added to the counter. Every time read() is called, the counter's value returned and it is reset to 0 (unless `EFD_SEMAPHORE` is set). Somehow this is used as a wait/notify mechanism.


1. Create Worker to run on Separate Thread

```C
pthread_t worker;
pthread_create(&worker, NULL, worker_thread_func, NULL);
```

`pthread_create()` will create a new thread in the same process that `app` is running in. This new thread runs the worker loop that will do blocking work, freeing up our server to continue to process requests. Since threads in a process share memory, both the app thread and the worker thread can access `work_queue` and `done_queue`. It is important to understand that the threads created by `pthread_create` are software abstractions, the number you create is not limited by hardware: they are not the same as the number of simultaneous instructions sets that can be executed by your machine, these are hardware threads. As a software abstraction, a `pthreads` thread is handled by the OS scheduler, and so if the scheduler notices that the thread is waiting, it will let another thread proceed until all of your hardware threads are executing a software thread or all remaining software threads are blocked.

In a webserver, worker threads are often loading a lot of html, css, or javascript files to serve. Each one of these read operations blocks the thread that called it. So, if we create multiple worker threads, the OS scheduler can go on dispatching reads across those workers, and our server can serve the read data once its ready.

1. Read Client request and enqueue work

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

1. Worker loop processes job

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

        // 2. Simulate heavy/blocking file I/O (e.g., reading disk)
        usleep(100000); // 100ms artificial delay
        snprintf(task.response_payload, sizeof(task.response_payload),
                 "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!");

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

(how does eventfd come into play?)
Why do we need both server and client connections to be non-blocking?
what does EPOLLONESHOT do exactly?
How should I pass routes into app now that I have worker threads?
