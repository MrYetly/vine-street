#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

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

char *load_html(const char filename*, size_t *file_size) {
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

	fread(buffer, *file_size, 1, f);
	buffer[size] = '\0';

	fclose(f);
	return buffer;

cleanup:
	perror("Error loading HTML file: ");
	if (f) fclose(f);
	return buffer;
}

void handle_home(const http_request_t req*, http_response_t res*) {
	char *body;
	size_t body_len;

	body = load_file(STATIC_DIR "index.html", &body_len);

	res.status_code = 200;
	res.headers[0].key = "Context-Length";
	snprintf(res.headers[0].val, sizeof(res.headers[0].val), "%zu", body_len);
	res.body = body;
	res.body_len = body_len;
	
}

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
		http_response_t res = {0};
		handle_home(req, res);

		size_t res_header_len =	sizeof(res.version) + 1
					3 + 1 //status code;
					+ 2; //CRLF
		for (int i = 0; res.headers[i].key && i < MAX_HEADERS; i++) {
			res_header_len += 	sizeof(res.headers[i].key)
						+2 //colon and space
						+ sizeof(res.headers[i].val)
						+ 2; //CRLF
		}
		res_header_len += 2; //double CRLF marking end of headers

		char *res_string = NULL;
		size_t res_string_len = res_header_len + res.body_len;
		res_string = malloc(res_string_len);

		//write version and status line
		size_t remaining = res_string_len;
		char fmt[]= "%s %d\r\n";
		int char_entered = snprintf(res_string, remaining, fmt, res.version, res.status_code);
		size_t offset = (size_t) char_entered;
		remaining -= (size_t) offset;

		//write headers
		char header_fmt[]="%s: %s\r\n";
		for (int = i 0; res.headers[i].key && i < MAX_HEADERS; ++i) {
			char_entered = snprintf(res_string + (size_t) offset, remaining, header_fmt, res.headers[i].key, res.headers[i].val);
			offset = char_entered;
		}

		//stopped here

		//typedef struct {
		//	char version[MAX_VERSION_LEN];
		//	int status_code;
		//	http_header_t headers[MAX_HEADERS];
		//	char *body;
		//	size_t body_len;
		//} http_response_t;

		write(client_fd, res_string, res_string_len);
		free(res_string);

		close(client_fd);
	}

	return 0;
}
