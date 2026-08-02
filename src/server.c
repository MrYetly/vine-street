#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define PORT 1362
#define BUFFER_SIZE 4096
#define REQUEST_QUEUE_LEN 10
#define MAX_HEADERS 32
#define MAX_HEADER_KEY_LEN 64
#define MAX_HEADER_VAL_LEN 512
#define MAX_PATH_LEN 2048
#define MAX_METHOD_LEN 16
#define MAX_VERSION_LEN 16
#define STATIC_DIR "/home/deploy/dev/ipa-website/static/"
#define HTTP_VERSION "HTTP/1.1"

typedef struct {
	char key[MAX_HEADER_KEY_LEN];
	char val[MAX_HEADER_VAL_LEN];
} http_header_t;

typedef struct {
	char method[MAX_METHOD_LEN];
	char path[MAX_PATH_LEN];
	char version[MAX_VERSION_LEN];
	//need to parse headers eventually
} http_request_t;

typedef struct {
	char version[MAX_VERSION_LEN];
	int status_code;
	http_header_t headers[MAX_HEADERS];
	char *body;
	size_t body_len;
} http_response_t;

typedef void (*http_handler_t)(const http_request_t *req, http_response_t *res);

char *load_html(const char *filename, size_t *file_size) {
	FILE *f = NULL;
	char *buffer = NULL;
	long size;

	f = fopen(filename, "rb");
	if (!f) goto cleanup;

	//get file length
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	*file_size = (size_t) size;
	buffer = malloc(size + 1);
	if (!buffer) goto cleanup;

	long bytes_read = fread(buffer, 1, *file_size, f);
	if (bytes_read < (long) *file_size) {
		if (feof(f)) {
			printf("End of file unexpectedly found.\n");
		} else if (ferror) {
			goto cleanup;
		}
	}
	buffer[size] = '\0';

	fclose(f);
	return buffer;

cleanup:
	perror("Error loading HTML file");
	if (f) fclose(f);
	if (buffer) {
		free(buffer);
		buffer = NULL;
	}
	return buffer;
}

void handle_home(const http_request_t *req, http_response_t *res) {
	char *body = NULL;
	size_t body_len = 0;

	body = load_html(STATIC_DIR "index.html", &body_len);

	if (body) {
		res->status_code = 200;
		snprintf(res->headers[0].key, MAX_HEADER_KEY_LEN, "Context-Length");
		snprintf(res->headers[0].val, MAX_HEADER_VAL_LEN, "%zu", body_len);
		res->body = body;
		res->body_len = body_len;
	}
}

int main(void) {
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
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons(PORT)
	};

	//bind address data to socket
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	//open the connection, allow for 10 queued requests before auto-rejection
	if (listen(server_fd, REQUEST_QUEUE_LEN) < 0) {
		perror("Failed to set socker status to listen");
		exit(EXIT_FAILURE);
	}

	char buffer[BUFFER_SIZE];
	ssize_t bytes_read;
	while (1) {
		int client_fd = 0;
		//errors handled internally: if accept or read fail, the socket is broken and no reponse can be sent
		client_fd = accept(server_fd, NULL, NULL); //the client connection is a file. this gets its file descriptor
		if (client_fd < 0) {
			goto socket_error_cleanup;
		}

		bytes_read = read(client_fd, buffer, sizeof(buffer)-1);
		if (bytes_read < 0) {
			goto socket_error_cleanup;
		}
		buffer[bytes_read]='\0';

		//400 error handling: if error, there's something wrong with the user's request
		char *ptr = buffer;
		http_request_t req = {0};
		char *parsed_res = NULL;
		http_response_t res = { .version = HTTP_VERSION};

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

		//create response
		//500 error handling: from here on, if error then we fucked up.
		//need to implement route table eventually, w/ error handling
		handle_home(&req, &res);
		if (!res.body) goto server_error_cleanup;

		//malloc for parsed response
		size_t res_header_len =	strlen(res.version) + 1
					+ 3 + 1 //status code and required extra space
					+ 2; //CRLF
		for (int i = 0; i < MAX_HEADERS && res.headers[i].key[0] != '\0'; i++) {
			res_header_len += 	strlen(res.headers[i].key)
						+ 2 //colon and space
						+ strlen(res.headers[i].val)
						+ 2; //CRLF
		}
		res_header_len += 2; //second CRLF in a row, marking end of headers
		size_t parsed_res_len = res_header_len + res.body_len;
		parsed_res = malloc(parsed_res_len);
		if (!parsed_res) goto server_error_cleanup;

		//parse response
		//write version and status line
		int char_entered = snprintf(parsed_res, parsed_res_len,  "%s %d \r\n", res.version, res.status_code);
		if (char_entered < 0) goto server_error_cleanup;
		size_t offset = (size_t) char_entered;

		//write headers
		for (int i = 0; i < MAX_HEADERS && res.headers[i].key[0] != '\0'; ++i) {
			char_entered = snprintf(
					parsed_res + offset,
					parsed_res_len - offset,
					"%s: %s\r\n",
					res.headers[i].key,
					res.headers[i].val);
			if (char_entered <0) goto server_error_cleanup;
			offset += (size_t) char_entered;
		}
		char_entered = snprintf(parsed_res + offset, parsed_res_len - offset,  "\r\n");
		if (char_entered < 0) goto server_error_cleanup;
		offset += (size_t) char_entered;
		assert(strlen(parsed_res) == res_header_len);

		//write body
		memcpy(parsed_res + offset, res.body, res.body_len);

		//send response
		write(client_fd, parsed_res, parsed_res_len);

		//cleanup
		free(parsed_res);
		close(client_fd);
		continue;

		socket_error_cleanup:
			perror("Socket error");
			if (client_fd) close(client_fd);
			continue;
		server_error_cleanup:
			perror("Server error");
			const char *server_error_response = 
				"HTTP/1.1 500 But why male models?\r\n"
				"Content-Length: 0\r\n"
				"Connection: close\r\n"
				"\r\n";
			write(client_fd, server_error_response, strlen(server_error_response));
			if (client_fd) close(client_fd);
			if (parsed_res) free(parsed_res);
			continue;

	}

	return 0;
}
