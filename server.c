//epoll + worker thread pool + eventfd
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <unistd.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <pthread.h>
//#include <sys/epoll.h>
//#include <sys/eventfd.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
//
//#define PORT 8080
//#define MAX_EVENTS 10
//#define QUEUE_SIZE 100
//
//// Task structure passed between threads
//typedef struct {
//    int client_fd;
//    char response_payload[128];
//} Task;
//
//// Work queue (Main Thread -> Worker Pool)
//Task work_queue[QUEUE_SIZE];
//int work_head = 0, work_tail = 0;
//pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;
//
//// Completed queue (Worker Pool -> Main Thread)
//Task done_queue[QUEUE_SIZE];
//int done_head = 0, done_tail = 0;
//pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
//
//int notify_fd; // eventfd handle
//
//// Helper to make sockets non-blocking
//void set_nonblocking(int fd) {
//    int flags = fcntl(fd, F_GETFL, 0);
//    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
//}
//
//// Worker Thread Loop
//void* worker_thread_func(void* arg) {
//    while (1) {
//        // 1. Fetch work task from queue
//        pthread_mutex_lock(&work_mutex);
//        while (work_head == work_tail) {
//            pthread_cond_wait(&work_cond, &work_mutex);
//        }
//        Task task = work_queue[work_head];
//        work_head = (work_head + 1) % QUEUE_SIZE;
//        pthread_mutex_unlock(&work_mutex);
//
//        // 2. Simulate heavy/blocking file I/O (e.g., reading disk)
//        usleep(100000); // 100ms artificial delay
//        snprintf(task.response_payload, sizeof(task.response_payload),
//                 "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!");
//
//        // 3. Push completed task to completed queue
//        pthread_mutex_lock(&done_mutex);
//        done_queue[done_tail] = task;
//        done_tail = (done_tail + 1) % QUEUE_SIZE;
//        pthread_mutex_unlock(&done_mutex);
//
//        // 4. Signal main thread via eventfd
//        uint64_t signal_val = 1;
//        eventfd_write(notify_fd, signal_val);
//    }
//    return NULL;
//}
//
//int main() {
//    int listen_fd, epfd;
//    struct sockaddr_in addr;
//    struct epoll_event ev, events[MAX_EVENTS];
//
//    // --- STEP 1: Set up eventfd ---
//    notify_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
//
//    // --- STEP 2: Create worker thread pool ---
//    pthread_t worker;
//    pthread_create(&worker, NULL, worker_thread_func, NULL);
//
//    // --- STEP 3: Create non-blocking server socket ---
//    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
//    int opt = 1;
//    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
//    set_nonblocking(listen_fd);
//
//    addr.sin_family = AF_INET;
//    addr.sin_addr.s_addr = INADDR_ANY;
//    addr.sin_port = htons(PORT);
//    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
//    listen(listen_fd, SOMAXCONN);
//
//    // --- STEP 4: Initialize epoll and add descriptors ---
//    epfd = epoll_create1(EPOLL_CLOEXEC);
//
//    // Add server socket
//    ev.events = EPOLLIN;
//    ev.data.fd = listen_fd;
//    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
//
//    // Add eventfd descriptor
//    ev.events = EPOLLIN;
//    ev.data.fd = notify_fd;
//    epoll_ctl(epfd, EPOLL_CTL_ADD, notify_fd, &ev);
//
//    printf("Server listening on port %d...\n", PORT);
//
//    // --- STEP 5: Main Event Loop ---
//    while (1) {
//        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
//
//        for (int i = 0; i < nfds; i++) {
//            int fd = events[i].data.fd;
//
//            // CASE A: New client connection
//            if (fd == listen_fd) {
//                int client_fd = accept(listen_fd, NULL, NULL);
//                if (client_fd > 0) {
//                    set_nonblocking(client_fd);
//                    ev.events = EPOLLIN | EPOLLONESHOT; // ONESHOT to prevent multi-thread race
//                    ev.data.fd = client_fd;
//                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
//                }
//            }
//            // CASE B: Worker thread signaled completed I/O via eventfd
//            else if (fd == notify_fd) {
//                eventfd_t val;
//                eventfd_read(notify_fd, &val); // Clear eventfd state
//
//                // Drain completed tasks
//                pthread_mutex_lock(&done_mutex);
//                while (done_head != done_tail) {
//                    Task completed_task = done_queue[done_head];
//                    done_head = (done_head + 1) % QUEUE_SIZE;
//
//                    // Main thread writes final HTTP response to client socket
//                    write(completed_task.client_fd, completed_task.response_payload,
//                          strlen(completed_task.response_payload));
//
//                    // Close client socket
//                    epoll_ctl(epfd, EPOLL_CTL_DEL, completed_task.client_fd, NULL);
//                    close(completed_task.client_fd);
//                }
//                pthread_mutex_unlock(&done_mutex);
//            }
//            // CASE C: Client socket ready to read
//            else {
//                char buf[512];
//                read(fd, buf, sizeof(buf)); // Read HTTP request
//
//                // Offload disk task to worker thread pool
//                pthread_mutex_lock(&work_mutex);
//                work_queue[work_tail].client_fd = fd;
//                work_tail = (work_tail + 1) % QUEUE_SIZE;
//                pthread_cond_signal(&work_cond);
//                pthread_mutex_unlock(&work_mutex);
//            }
//        }
//    }
//    return 0;
//}
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>

#include "server.h"

//create work queue
task_t work_queue[WORK_QUEUE_SIZE];
int work_head = 0, work_tail = 0;
pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;

//create done queue
task_t done_queue[QUEUE_SIZE];
int done_head = 0, done_tail = 0;
pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;

int notify_fd; //eventfd handle

//worker thread loop
void* worker_thread_func(void* arg) {
	while (1) {
		//fetch a task to be done
		pthread_mutex_lock(&work_mutex);
		while (work_head == work_tail) {
			pthread_cond_wait(&work_cond, &work_mutex); //immediate gives up the lock? And also repeatedly says to wait? Isn't that redundant?
		}

	}
}

static int set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1) return -1;
	printf("flags: %b", flags);
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void app(const route_t *routes, size_t route_count) {
	//create unbound socket that we will later bind to
	//this has buffers (for data i/o), configuration (TCP vs UDP, IP4 vs IP6), and state (connection status, etc)
	int server_fd = socket(
			AF_INET, //IP_V4
			SOCK_STREAM, // TCP
			0 //choose most common configuration given prev. args
			);
	if (server_fd < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

	//set socket to instantly reuse address
	int enable = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
		exit(EXIT_FAILURE);
	}
	//set socket to nonblocking
	if (set_nonblocking(server_fd) < 0) {
		perror("Failed to set socket as nonblocking");
		exit(EXIT_FAILURE);

	//create instance of sock_addr_in that will be used to bind to socket
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(ADDRESS),
		.sin_port = htons(PORT)
	};

	//bind address data to socket
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	//open the connection, allow for 10 queued requests before auto-rejection
	if (listen(server_fd, SOMAXCONN) < 0) {
		perror("Failed to set socket status to listen");
		exit(EXIT_FAILURE);
	}

	//set close-on-exec, so multi-threaded execution doesn't share file descriptor of epoll instance, causing potential race condition when manipulating epoll instance.
	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.fd = server_df, //for my convenience, allows me to differentiate between socket and client events later?
	};
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

	char buffer[BUFFER_SIZE];
	ssize_t bytes_read;
	struct epoll_event events[MAX_EVENTS];
	while (1) {
		int nfds = epoll_wait(epoll, events, MAX_EVENTS, -1); //-1 sets no timeout, blocks indefinitely until connections on interest list enter some events/data on the ready list


		int client_fd = 0;
		//errors handled internally: if accept or read fail, the socket is broken and no reponse can be sent
		client_fd = accept(server_fd, NULL, NULL); //the client connection is a file. this gets its file descriptor
		if (client_fd < 0) {
			goto socket_error;
		}

		bytes_read = read(client_fd, buffer, sizeof(buffer)-1);
		if (bytes_read < 0) {
			goto socket_error;
		}
		buffer[bytes_read]='\0';

		//400 error handling: if error, there's something wrong with the user's request
		char *ptr = buffer;
		http_request_t req = {0};
		char *parsed_res = NULL;
		http_response_t res = {
			.version = HTTP_VERSION,
			.headers[0].key = "Connection",
			.headers[0].val = "close",
			.next_header_idx = 1,
		};

		//parse request line
		for (int i = 0; *ptr != ' ' && i < MAX_METHOD_LEN - 1; ++i) {
			req.method[i] = *ptr;
			++ptr;
		}
		++ptr;
		for (int i = 0; *ptr != ' ' && i < MAX_PATH_LEN -1; ++i) {
			req.path[i] = *ptr;
			++ptr;
		}
		++ptr;
		for (int i = 0; *ptr != '\n' && i < MAX_VERSION_LEN -1; ++i) {
			if (*ptr != '\r') {
				req.version[i] = *ptr;
			}
			++ptr;
		}
		++ptr;

		//parse headers

		//parse body


		//very naive matching process, may need to improve later
		http_handler_t handler;
		for (int i = 0; i < route_count; ++i) {
			if (strcmp(req.path, (routes + i)->path) == 0) {
				goto path_matched;
			}
		}
		res.status_code = 404;
		snprintf(res.reason_phrase, MAX_PHRASE_LEN, "Not Found");
		snprintf(res.headers[res.next_header_idx].key, MAX_HEADER_KEY_LEN, "Content-Length");
		snprintf(res.headers[res.next_header_idx].val, MAX_HEADER_VAL_LEN, "0");
		++res.next_header_idx;
		goto skip_handler_because_of_error;

		path_matched:
		for (int i=0; i< route_count; ++i) {
			if (strcmp(req.path, (routes + i)->path) == 0) {
				if (strcmp(req.method, (routes + i)->method) == 0) {
					handler = (routes + i)->handler;
					goto matching_done;
				}
			}
		}
		res.status_code = 405;
		snprintf(res.reason_phrase, MAX_PHRASE_LEN, "Method Not Allowed");
		snprintf(res.headers[res.next_header_idx].key, MAX_HEADER_KEY_LEN, "Content-Length");
		snprintf(res.headers[res.next_header_idx].val, MAX_HEADER_VAL_LEN, "0");
		++res.next_header_idx;
		goto skip_handler_because_of_error;

		matching_done:
		//create response
		//500 error handling: from here on, if error then we fucked up.
		handler(&req, &res);

		skip_handler_because_of_error:
		//malloc for parsed response
		size_t res_header_strlen =	strlen(res.version) + 1
					+ 3 + 1 //status code
					+ strlen(res.reason_phrase)
					+ 2; //CRLF
		for (int i = 0; i < MAX_HEADERS && res.headers[i].key[0] != '\0'; i++) {
			res_header_strlen += 	strlen(res.headers[i].key)
						+ 2 //colon and space
						+ strlen(res.headers[i].val)
						+ 2; //CRLF
		}
		res_header_strlen += 2; //second CRLF in a row, marking end of headers
		size_t res_header_size = res_header_strlen + 1;
		size_t parsed_res_size = res_header_strlen + res.body_size;
		parsed_res = malloc(parsed_res_size + 1); //leave room for null terminator incase body is text and we need to print
		if (!parsed_res) goto response_parse_error;
		parsed_res[parsed_res_size] = '\0';

		//parse response
		//write version, status, and reason phrase line
		int char_entered = snprintf(
				parsed_res,
				res_header_size,
				"%s %d %s\r\n",
				res.version,
				res.status_code,
				res.reason_phrase);
		if (char_entered < 0) goto response_parse_error;
		size_t offset = (size_t) char_entered;

		//write headers
		for (int i = 0; i < MAX_HEADERS && res.headers[i].key[0] != '\0'; ++i) {
			char_entered = snprintf(
					parsed_res + offset,
					res_header_size - offset,
					"%s: %s\r\n",
					res.headers[i].key,
					res.headers[i].val);
			if (char_entered <0) goto response_parse_error;
			offset += (size_t) char_entered;
		}
		char_entered = snprintf(parsed_res + offset, res_header_size - offset,  "\r\n");
		if (char_entered < 0) goto response_parse_error;
		offset += (size_t) char_entered;
		assert(strlen(parsed_res) == res_header_strlen);

		//write body
		if (res.body) memcpy(parsed_res + offset, res.body, res.body_size);

		//send response
		write(client_fd, parsed_res, parsed_res_size);
		goto cleanup;

		response_parse_error:
			perror("Response parse error");
			const char *response_parse_error_response = 
				"HTTP/1.1 500 But why male models?\r\n"
				"Context-Length: 0\r\n"
				"Connection: close\r\n"
				"\r\n";
			write(client_fd, response_parse_error_response, strlen(response_parse_error_response));
			goto cleanup;
		socket_error:
			perror("Socket error");
			goto cleanup;
		cleanup:
			if (parsed_res) free(parsed_res);
			if (client_fd) close(client_fd);
			continue;
	}
}
