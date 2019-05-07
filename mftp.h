#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <wait.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>

#define BUF_SIZE 512
#define PORT_NUM 49999


//Attempts to write len bytes from cbuf through the socket specified by sockfd.
//Returns the number of bytes successfully written.
int writeStringToSock(int sockfd, char *cbuf, int len);

//Attempts to read len bytes from teh socket specified by fd and place them into buf.
//Returns the number of bytes read as a positive integer if EOF was not reached, or a negative integer if EOF was reached.
int readBytesFromSocket(int fd, char *buf, int len);