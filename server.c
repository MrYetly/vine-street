#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

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
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(9080)
	};
	char buffer[4096];
	ssize_t bytes_read;

	//bind address data to socker
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		return -1;
	}

	//open the connection, allow for 10 queued requests before auto-rejection
	listen(server_fd, 10);

	while (1) {
		int client_fd = accept(server_fd, NULL, NULL); //the client connection is a file. this gets its file descriptor

		bytes_read = read(client_fd, buffer, sizeof(buffer)-1);
		buffer[bytes_read]='\0';
		printf("Received %zd Bytes:\n%s\n", bytes_read, buffer);

		char response[]="HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
		write(client_fd, response, sizeof(response)-1);

		close(client_fd);
	}

	return 0;
}
