#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>

#include "server.h"

//create work queue
task_t work_queue[QUEUE_SIZE];
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
	//struct timespec	start = {0}, end = {0}, handler_start, handler_end;
	//double elapsed, handler_elapsed;
	while (1) {

		//fetch a task to be done
		pthread_mutex_lock(&work_mutex);
		//if (start.tv_nsec || start.tv_sec) {
		//	clock_gettime(CLOCK_MONOTONIC, &end);
		//	elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec)/1e9;
		//	printf("Elapsed worker time: %0.6f seconds\n", elapsed);
		//}
		while (work_head == work_tail) { //while loop to prevent spurious wakes
			pthread_cond_wait(&work_cond, &work_mutex);
		}
		//clock_gettime(CLOCK_MONOTONIC, &start);
		task_t task = work_queue[work_head];
		work_head = (work_head + 1) % QUEUE_SIZE;
		pthread_mutex_unlock(&work_mutex);

		//400 error handling: if error, there's something wrong with the user's request
		route_t *routes = ((app_init_t *)*(intptr_t *)arg)->routes;
		size_t route_count = ((app_init_t *)*(intptr_t *)arg)->route_count;
		char *ptr = task.req_buffer;
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
		bool end_of_path_found = false;
		for (int i = 0; *ptr != ' ' && i < MAX_PATH_LEN -1; ++i) {
			if (*ptr == '?' || *ptr == '#') end_of_path_found = true;
			if (!end_of_path_found) req.path[i] = *ptr;
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
		//clock_gettime(CLOCK_MONOTONIC, &handler_start);
		handler(&req, &res);
		//clock_gettime(CLOCK_MONOTONIC, &handler_end);
		//handler_elapsed = (handler_end.tv_sec - handler_start.tv_sec) + (handler_end.tv_nsec - handler_start.tv_nsec)/1e9;
		//printf("Elapsed handler time: %0.6f seconds\n", handler_elapsed);

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

		//write to task
		task.parsed_res = parsed_res;
		task.parsed_res_size = parsed_res_size;
		goto done;

		response_parse_error:
			perror("Response parse error");
			char *response_parse_error_response = 
				"HTTP/1.1 500 But why male models?\r\n"
				"Context-Length: 0\r\n"
				"Connection: close\r\n"
				"\r\n";
			task.parsed_res = response_parse_error_response;
			task.parsed_res_size = strlen(response_parse_error_response);
			goto done;

		done:
			pthread_mutex_lock(&done_mutex);
			done_queue[done_tail] = task;
			done_tail = (done_tail +1) % QUEUE_SIZE;
			pthread_mutex_unlock(&done_mutex);
			uint64_t signal_val = 1;
			if (eventfd_write(notify_fd, signal_val) < 0) {
				perror("eventfd_write");
			}
	}
	return NULL;
}

static int set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1) return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void app(const app_init_t *app_init) {
	//start eventfd for epoll to track
	notify_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); //using CLOEXEC is defensive, incase other libraries use fork() + exec()

	//start worker thread(s)
	pthread_t workers[NUM_WORKER_THREADS];
	for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
		pthread_create(&workers[i], NULL, worker_thread_func, &app_init);
	}

	//create socket
	int server_fd = socket(
			AF_INET, //IP_V4
			SOCK_STREAM, // TCP
			0 //choose most common configuration given prev. args
			);
	if (server_fd < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}
	int enable = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
		exit(EXIT_FAILURE);
	}
	if (set_nonblocking(server_fd) < 0) {
		perror("Failed to set socket as nonblocking");
		exit(EXIT_FAILURE);
	}
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(ADDRESS),
		.sin_port = htons(PORT)
	};
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	if (listen(server_fd, SOMAXCONN) < 0) {
		perror("Failed to set socket status to listen");
		exit(EXIT_FAILURE);
	}

	//start epoll
	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);//using CLOEXEC is defensive, incase other libraries use fork() + exec()

	//register server_fd with epoll
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.fd = server_fd,
	};
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

	//register notify_fd with epoll
	ev = (struct epoll_event){
		.events = EPOLLIN,
		.data.fd = notify_fd,
	};
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notify_fd, &ev);

	char buffer[BUFFER_SIZE];
	ssize_t bytes_read;
	struct epoll_event events[MAX_EVENTS];
	while (1) {
		int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); //-1 sets no timeout, blocks until events occur

		for (int i = 0; i < nfds; i++) {
			int fd = events[i].data.fd;

			if (fd == server_fd) {
				int client_fd = accept(server_fd, NULL, NULL);
				if (client_fd < 0) {
					perror("Socket error, accept");
					continue;
				} else {
					set_nonblocking(client_fd);
					ev.events = EPOLLIN | EPOLLONESHOT;
					ev.data.fd = client_fd;
					epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
				}
			} else if (fd == notify_fd) {
				eventfd_t val;
				eventfd_read(notify_fd, &val);

				//drain completed tasks
				pthread_mutex_lock(&done_mutex);
				while (done_head != done_tail) {
					task_t task = done_queue[done_head];
					done_head = (done_head + 1) % QUEUE_SIZE;

					ssize_t bytes_written = write(task.client_fd, task.parsed_res, task.parsed_res_size);
					if (bytes_written < 0) {
						perror("Socket error, write");
					}

					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, task.client_fd, NULL);
					close(task.client_fd);
				}
				pthread_mutex_unlock(&done_mutex);
			} else {
				bytes_read = read(fd, buffer, BUFFER_SIZE-1);
				if (bytes_read < 0) {
					perror("Socket error, read");
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
					close(fd);
					continue;
				}
				buffer[bytes_read] = '\0';

				pthread_mutex_lock(&work_mutex);

				work_queue[work_tail].client_fd = fd;
				snprintf(work_queue[work_tail].req_buffer, BUFFER_SIZE, "%s", buffer);

				work_tail = (work_tail +1) % QUEUE_SIZE;

				pthread_cond_signal(&work_cond);

				pthread_mutex_unlock(&work_mutex);
			}
		}
	}
}
