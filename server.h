#include <netinet/in.h>
#include <stddef.h>

#define PORT 1362
#define ADDRESS INADDR_LOOPBACK
#define BUFFER_SIZE 4096
#define MAX_EVENTS 10
#define MAX_HEADERS 32
#define MAX_HEADER_KEY_LEN 64
#define MAX_HEADER_VAL_LEN 512
#define MAX_PATH_LEN 2048
#define MAX_METHOD_LEN 16
#define MAX_VERSION_LEN 16
#define MAX_PHRASE_LEN 32
#define HTTP_VERSION "HTTP/1.1"
#define QUEUE_SIZE 128

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
	char reason_phrase[MAX_PHRASE_LEN];
	http_header_t headers[MAX_HEADERS];
	int next_header_idx;
	char *body;
	size_t body_size;
} http_response_t;

typedef void (*http_handler_t)(const http_request_t *req, http_response_t *res);

typedef struct {
	char *path;
	char *method;
	http_handler_t handler;
} route_t;

void app(const route_t *routes, size_t route_count);

//task structure passed between threads
typdef struct{
	int client_fd;
	char response_payload[BUFFER_SIZE];
} task_t;

