#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "server.h"

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

	int enable = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
	}

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
	if (listen(server_fd, REQUEST_QUEUE_LEN) < 0) {
		perror("Failed to set socket status to listen");
		exit(EXIT_FAILURE);
	}

	char buffer[BUFFER_SIZE];
	ssize_t bytes_read;
	while (1) {
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
