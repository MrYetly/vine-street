#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

#define PORT 1362
#define BUFFER_SIZE 4096
#define REQUEST_QUEUE_LEN 10
#define MAX_HEADERS 32
#define MAX_HEADER_KEY_LEN 64
#define MAX_HEADER_VAL_LEN 512
#define MAX_PATH_LEN 2048
#define MAX_METHOD_LEN 16
#define MAX_VERSION_LEN 16

int main(void) {
	//create unbound socket that we will later bind to
	//this has buffers (for data i/o), configuration (TCP vs UDP, IP4 vs IP6), and state (connection status, etc)
	int server_fd = socket(
			AF_INET, //IP_V4
			SOCK_STREAM, // TCP
			0 //choose most common configuration given prev. args
			);


	//create instance of sock_addr_in that will be used to bind to socket
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons(PORT)
	};
	char buffer[BUFFER_SIZE];
	ssize_t bytes_read;
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
		int status_code;
		http_header_t headers[MAX_HEADERS];
		char *body;
		size_t body_len;
	} http_response_t;
	typedef void (*http_handler_t)(const http_request_t *req, http_response_t *res);

	//bind address data to socker
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		return -1;
	}

	//open the connection, allow for 10 queued requests before auto-rejection
	listen(server_fd, REQUEST_QUEUE_LEN);

	while (1) {
		int client_fd = accept(server_fd, NULL, NULL); //the client connection is a file. this gets its file descriptor

		bytes_read = read(client_fd, buffer, sizeof(buffer)-1);
		buffer[bytes_read]='\0';
		printf("\nReceived %zd Bytes:\n%s", bytes_read, buffer);

		char *ptr = buffer;
		http_request_t req = {0};

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


		char response[]="HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
		write(client_fd, response, sizeof(response)-1);

		close(client_fd);
	}

	return 0;
}
