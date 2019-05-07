#include "mftp.h"

//	Author:	Spencer Warneke
//	Class:	CS 360
//	Assn:	Final

#define LINE_LEN 1024
#define PIPE_READ 0
#define PIPE_WRITE 1

//Converts string from a potentially messy string with a lot of whitespace to a more reasonable string
//that competent human might type.
char *convertToGoodString(char* string);

//Pipes an ls -l into a more -20 and prints the results to the screen.
void pipels();

//Grabs the last token from a absolute or relative path, and returns it.
char *getFilenameFromPath(char *path);

//Sends a request to the server to perform an ls -l, and 'pipes' the data into a local more -20.
void remotels(int socketfd);

//Attempts to establish a connection to the host specified by hostString on portNum.
//On success, socketfd will contain the value of the socket fd, and establishConnection will return true.
bool establishConnection(int *socketfd, int portNum, char *hostString);

//Grabs a line from sockfd, and checks if it is a valid acknowledgement.
//Returns true with no print on a valid ack, returns false with an error print on an invalid ack.
bool getAcknowledgement(char *buf);

//Sends a request to the server to open a data connection, and returns the port number given by the server's acknowledgement.
int getDataConnection();

//Sends a request to get a serverside file (referred to by pathname) and save it in this process' working directory.
void getFile(char *pathname);

//Sends a request to show a serverside file (referred to by pathname) and 'pipes' the data into a `more -20` exec.
void showFile(char* pathname);

//Sends a request to put a clientside file (referred to by pathname) and save it in the server's working directory.
void putFile(char *pathname);

//Attempts to chdir to pathname.
void cd(char *pathname);

//Sends a request to the server to chdir to pathname.
void remotecd(char *pathname);

//hostname of server to connect to
//given by argv[1] on command line; used throughout program for establishConnection()
char *hostName;
//fd of the control connection to the server
int socketfd;

int main(int argc, char *argv[])
{
	//need two arguments exactly
	if(argc != 2)
	{
		fprintf(stderr, "Error: Invalid number of arguments to %s\n", argv[0]);	
		exit(1);	
	}
	
	//save hostname
	hostName = argv[1];
	
	//attempt to connect to the server
	if(!establishConnection(&socketfd, PORT_NUM, hostName))
	{
		fprintf(stderr, "Error: Unable to establish connection\n");
		exit(1);
	}
	
	//buffer for raw user commands
	char commandBuf[BUF_SIZE];
	//buffer for formatted command
	char *command;
	//buffers for each token of command
	char *firstToken = malloc(512 * sizeof(char));
	char *secondToken= malloc(512 * sizeof(char));
	//buffer for use in getcwd()
	char cwdBuf[LINE_LEN];
	
	printf("Connection made to %s\n> ", argv[1]);
	
	//command loop
	while( fgets(commandBuf, BUF_SIZE - 1, stdin) != NULL )
	{
		//get the tokens from the line
		command = convertToGoodString(commandBuf);
		
		int numTokens = sscanf(command, "%s %s", firstToken, secondToken);
		
		//we now have our tokens ready
		
		//which command are we running?
		if(numTokens == 1)
		{
			//possible commands: exit, ls, rls, pwd
			
			if(strcmp(firstToken, "exit") == 0)
			{
				writeStringToSock(socketfd, "Q\n", 2);
				
				printf("Exiting...\n");
				exit(0);
			}
			else if(strcmp(firstToken, "ls") == 0)	
			{
				pipels();
			}
			else if(strcmp(firstToken, "rls") == 0)	
			{
				remotels(socketfd);
			}
			else if(strcmp(firstToken, "pwd") == 0)		//have one for remote too?
			{
				printf("%s\n", (getcwd(cwdBuf, sizeof(cwdBuf)) == NULL)?"error":cwdBuf);
			}
			else
			{
				printf("%s is an invalid command. Please try again.\n", command);
			}
		}
		else
		{
			//possible commands: cd, rcd, get, put, show
			if(strcmp(firstToken, "cd") == 0)
			{
				cd(secondToken);
			}
			else if(strcmp(firstToken, "rcd") == 0)
			{
				remotecd(secondToken);
			}
			else if(strcmp(firstToken, "get") == 0)
			{
				getFile(secondToken);
			}
			else if(strcmp(firstToken, "put") == 0)
			{
				putFile(secondToken);
			}
			else if(strcmp(firstToken, "show") == 0)
			{
				showFile(secondToken);
			}
			else
			{
				printf("%s is an invalid command. Please try again.\n", command);	
			}
			
		}
		
		//string clearing (!! KEEP AT END OF LOOP !!)
		memset(firstToken, 0, BUF_SIZE);
		memset(secondToken, 0, BUF_SIZE);
		
		free(command);
		
		printf("> ");
	}
}

void cd(char *pathname)
{
	//attempt dir change
	int check = chdir(pathname);
				
	//check success
	if(check == -1)
	{
		fprintf(stderr, "Error: unable to change directory to %s\n", pathname);	
	}
	else
	{
		printf("Success.\n");	
	}
}

void remotecd(char *pathname)
{
	//our message to the server is going to be C<pathname>\n\0, so we need 3 more character slots than pathname
	char message[strlen(pathname) + 3];

	message[0] = 'C';
	message[1] = '\0';

	strcat(message, pathname);

	//message is now C<pathname>\0

	strcat(message, "\n");

	//message is now C<pathname>\n\

	//write the message to the server
	int len = writeStringToSock(socketfd, message, strlen(message));

	//if unable to write the full message, it may mean that the server is no longer alive
	if(len != strlen(message))
	{
		fprintf(stderr, "Error: unable to write string \"%s\" to socket; server termination expected\n", message);
		exit(1);
	}

	//grab an acknowledgement from the server
	char acknowledgeBuf[BUF_SIZE];

	if(getAcknowledgement(acknowledgeBuf))
	{
		printf("Success.\n");	
	}
}

char *convertToGoodString(char* string)
{
	char *newString = malloc(BUF_SIZE * sizeof(char));
	
	int fromIndex = 0, toIndex = 0;
	
	//skip leading whitespace
	while( string[fromIndex] != '\n' && isspace(string[fromIndex]) )
	{
		fromIndex++;	
	}
	
	//copy all nonspace characters
	while( string[fromIndex] != '\n' && !isspace(string[fromIndex]) )
	{
		newString[toIndex++] = string[fromIndex++];
	}
	
	//at this point, either we've hit the end of the string or fromIndex is pointing to a space in string
	
	if(string[fromIndex] == '\n')
	{
		newString[toIndex] = '\0';
		return newString;
	}
	
	newString[toIndex++] = ' ';
	
	//skip more whitespace
	while( string[fromIndex] != '\n' && isspace(string[fromIndex]) )
	{
		fromIndex++;	
	}
	
	if(string[fromIndex] == '\n')
	{
		newString[toIndex] = '\0';
		return newString;
	}
	
	//copy all nonspace characters
	while( string[fromIndex] != '\n' && !isspace(string[fromIndex]) )
	{
		newString[toIndex++] = string[fromIndex++];
	}

	newString[toIndex] = '\0';
	return newString;
}

//routine to pipe an ls -l into more -p -20
void pipels()
{
	printf("--- Begin ls ---\n");
	int pipefd[2];	
	int check = pipe(pipefd);
	
	if(check == -1)									//if something went wrong while opening the pipe, don't continue
	{
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}	
	
	int pid1, pid2;
	
	if(pid1 = fork())	//parent
	{
		if(pid2 = fork())	//parent
		{
			//close both ends of the pipe, we have no need for them
			close(pipefd[PIPE_READ]);
			close(pipefd[PIPE_WRITE]);
			
			//wait for both children
			waitpid(pid1, NULL, 0);
			waitpid(pid2, NULL, 0);
		}
		else				//child
		{
			//do a more
			
			close(socketfd);
			
			//close the write end; we have no use for it
			close(pipefd[PIPE_WRITE]);
			
			//dupe the read end over stdin
			dup2(pipefd[PIPE_READ], STDIN_FILENO);
			
			//close the original read end
			close(pipefd[PIPE_READ]);
			
			//exec a more -20
			execlp("more", "more", "-20", (char *)NULL);
		}
	}
	else	//child
	{
		//do an ls
		
		close(socketfd);
		
		//close the read end, we have no use for it
		close(pipefd[PIPE_READ]);
		
		//duplicate the write end of the pipe over stdout
		dup2(pipefd[PIPE_WRITE], STDOUT_FILENO);
		
		//close the original pipe write fd
		close(pipefd[PIPE_WRITE]);
		
		//exec an `ls -l`
		execlp("ls", "ls", "-l", (char *)NULL);
	}
	
	printf("--- End ls ---\n");
}

char *getFilenameFromPath(char *path)
{
	//grab length of the path
	int pathLength = strlen(path);
	
	//set up indexes
	int scanIndex = 0;
	//why -1? because strcpy is going to copy from the character *after* this index to the end.
	//this covers both the cases of there being a slash, and there not being one.
	int indexOfLastSlash = -1;
	
	//scan for last slash in path
	while(scanIndex < pathLength)
	{
		if(path[scanIndex] == '/')
		{
			indexOfLastSlash = scanIndex;	
		}
		
		scanIndex++;
	}
	
	//allocate only as much space as we need
	char *filename = malloc( (pathLength - indexOfLastSlash) * sizeof(char) );
	
	//grab filename from path[indexOfLastSlash + 1] to path[pathLength]
	strcpy(filename, &path[indexOfLastSlash + 1]);
	
	return filename;
}

//Attempts to write len bytes of cbuf to the socket specified by fd
//Returns the number of bytes successfully written to the socket
int writeStringToSock(int fd, char *cbuf, int len)
{
	int currentWrite;
	int bytesLeft = len;
	
	//while we still have data to write
	while(bytesLeft > 0)
	{
		//grab the number of bytes actually written to the socket
		currentWrite = write(fd, cbuf + (len - bytesLeft), bytesLeft);
		
		//subtract that from the total number of bytes to write
		bytesLeft -= currentWrite;
	}
	
	return len - bytesLeft;
}

//Reads up to bufLen bytes into buf from the given fd.
//Returns the number of bytes read into buf.
//If read returns a 0, readBytes will return the number of bytes read as a negative integer to indicate EOF.
int readBytesFromSocket(int fd, char *buf, int bufLen)
{
	//clear the buffer
	memset(buf, 0, bufLen);
	
	int bytesRead = 0;
	int currentRead;
	
	//while we still have data in the buffer
	while(bytesRead < bufLen -1)
	{
		//attempt a read for the remaining space in the buf
		currentRead = read(fd, buf+bytesRead, bufLen - bytesRead);
		
		//if the return was less than 1, return a negative integer
		if(currentRead < 1)
		{
			return -1*bytesRead;
		}
		
		bytesRead += currentRead;
		
		//if the last character was a newline, terminate the string and return
		if(buf[bytesRead - 1] == '\n')
		{
			buf[bytesRead] = '\0';
			return bytesRead;	
		}
	}
	
	return bytesRead;
}

void remotels(int socketfd)
{
	int datafd;
	int dataPort;
	char responseBuf[BUF_SIZE];
	
	dataPort = getDataConnection();
	
	//if data connection failed, return from this subroutine
	if(dataPort == -1)
	{
		return;
	}
	
	//if establishing a connection failed, return from this subroutine
	if(!establishConnection(&datafd, dataPort, hostName))
	{
		fprintf(stderr, "Error: Unable to establish data connection for remote ls\n");
		return;
	}
	
	//write a list command to the socket
	writeStringToSock(socketfd, "L\n", 2);
	
	printf("--- Begin ls ---\n");
	
	int pid;
	
	if(pid = fork())	//parent
	{
		//close the data fd and wait for the child
		close(datafd);
		waitpid(pid, NULL, 0);
	}
	else				//child
	{
		//close the socket fd, and dup2 the datafd over stdin
		close(socketfd);
		dup2(datafd, STDIN_FILENO);
		close(datafd);
		
		//then exec to more -20
		execlp("more", "more", "-20", NULL);
	}
	
	printf("--- End ls ---\n");
	
	if(getAcknowledgement(responseBuf))
	{
		printf("Success.\n");
	}
}

//takes an integer buffer and a port number and attempts to establish a connection to the port
//returns a bool describing connection success
bool establishConnection(int *socketfd, int portNum, char *hostString)
{
	(*socketfd) = socket(AF_INET, SOCK_STREAM, 0);
	
	//set up server address struct
	struct sockaddr_in servAddr;
	struct hostent* hostEntry;
	struct in_addr **pptr;

	memset( &servAddr, 0, sizeof(servAddr) );
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(portNum);

	//resolve the hostname
	hostEntry = gethostbyname(hostString);
	
	//if resolution is unsuccessful, don't continue
	if(hostEntry == NULL)
	{
		herror("Error: Unable to resolve hostname");
		return false;
	}

	pptr = (struct in_addr **) hostEntry->h_addr_list;
	memcpy(&servAddr.sin_addr, *pptr, sizeof(struct in_addr));
	
	//connect to the host on the specified port
	int connectReturn = connect( (*socketfd), (struct sockaddr *) &servAddr, sizeof(servAddr));
	
	//if connection unsuccessful, don't continue
	if(connectReturn < 0)
	{
		perror("Error: unable to open connection to host");
		return false;
	}
	
	return true;
}

bool getAcknowledgement(char *buf)
{
	readBytesFromSocket(socketfd, buf, BUF_SIZE);

	if(buf[0] != 'A')
	{
		fprintf(stderr, "Error recieved from server:\n%s\n", &buf[1]);	
		return false;
	}
	else
	{
		return true;
	}	
	
}

int getDataConnection()
{
	//request a data connection
	writeStringToSock(socketfd, "D\n", 2);
	
	char responseBuf[BUF_SIZE];
	
	//grab the acknowledgement; getAck handles err printing
	if(!getAcknowledgement(responseBuf))
	{
		return -1;
	}
	
	//grab the port number from the acknowledgement
	return atoi(&responseBuf[1]);	
}

void getFile(char *pathname)
{
	//data connection setup
	int datafd;
	int dataPort;

	dataPort = getDataConnection();

	if(dataPort == -1)
	{
		return;
	}

	//if establishing a data connection failed, return from this subroutine
	if(!establishConnection(&datafd, dataPort, hostName))
	{
		fprintf(stderr, "Error: Unable to establish data connection for get\n");
		return;
	}

	//message will be G<pathname>\n\0, so we need 3 extra character slots
	char message[strlen(pathname) + 3];

	message[0] = 'G';
	message[1] = '\0';

	strcat(message, pathname);

	//message is now G<pathname>\0

	strcat(message, "\n");

	//message is now G<pathname>\n\0

	//attempt to write message to server
	int len = writeStringToSock(socketfd, message, strlen(message));

	//check if the write worked
	if(len != strlen(message))
	{
		fprintf(stderr, "Error: unable to write string \"%s\" to socket; server termination expected\n", message);
		close(datafd);
		exit(1);
	}	
	
	//grab an ackowledgement from the server
	char ackBuf[BUF_SIZE];
	
	if(!getAcknowledgement(ackBuf))
	{
		return;	
	}
	
	//buffer for file bytes
	char fileBuffer[BUF_SIZE];
	
	char *filename = getFilenameFromPath(pathname);
	
	//open file in local dir for writing; create if non-existant, truncate if existant.
	int localfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
	
	//check if the fd is valid
	if(localfd < 0)
	{
		fprintf(stderr, "Error: unable to open %s in local directory\n", filename);
		close(datafd);
		return;
	}
	
	int readLen;
	
	//while we still have data to read from the data connection
	while( (readLen = read(datafd, fileBuffer, BUF_SIZE)) > 0 )
	{
		//write out whatever we just read in
		write(localfd, fileBuffer, readLen);
	}
	
	//close the data connection and the local file
	close(datafd);
	close(localfd);
	
	//print a success message
	printf("Success.\n");
}

void showFile(char *pathname)
{
	//stream data like get
	//but display like rls
	
	//data connection setup
	int datafd;
	int dataPort;

	dataPort = getDataConnection();

	if(dataPort == -1)
	{
		return;
	}

	//if establishing a connection failed, return from this subroutine
	if(!establishConnection(&datafd, dataPort, hostName))
	{
		fprintf(stderr, "Error: Unable to establish data connection for get\n");
		return;
	}

	
	char message[strlen(pathname) + 3];

	message[0] = 'G';
	message[1] = '\0';

	strcat(message, pathname);

	//message is now G<pathname>\0

	strcat(message, "\n");

	//message is now G<pathname>\n\0

	//attempt to write message to server
	int len = writeStringToSock(socketfd, message, strlen(message));

	//check if write was successful
	if(len != strlen(message))
	{
		fprintf(stderr, "Error: unable to write string \"%s\" to socket; server termination expected\n", message);
		exit(1);
	}	
	
	//grab acknowledgement from server
	char ackBuf[BUF_SIZE];
	
	if(!getAcknowledgement(ackBuf))
	{
		return;	
	}
	
	printf("--- Begin show ---\n");
	
	int pid;
	
	if(pid = fork())	//parent
	{
		//close the data fd and wait for the child
		close(datafd);
		waitpid(pid, NULL, 0);
	}
	else				//child
	{
		//close the socket fd, and dup2 the datafd over stdin
		close(socketfd);
		dup2(datafd, STDIN_FILENO);
		close(datafd);
		
		//then exec to more -20
		execlp("more", "more", "-20", NULL);
	}
	
	printf("--- End show ---\n");
}

void putFile(char *pathname)
{
	//data connection setup
	int datafd;
	int dataPort;

	dataPort = getDataConnection();

	if(dataPort == -1)
	{
		return;
	}

	//if establishing a connection failed, return from this subroutine
	if(!establishConnection(&datafd, dataPort, hostName))
	{
		fprintf(stderr, "Error: Unable to establish data connection for get\n");
		return;
	}
	
	char *filename = getFilenameFromPath(pathname);

	char message[strlen(filename) + 3];

	message[0] = 'P';
	message[1] = '\0';

	strcat(message, filename);

	//message is now P<filename>\0

	strcat(message, "\n");

	//message is now P<filename>\n\0

	//attempt to write message to server
	int len = writeStringToSock(socketfd, message, strlen(message));

	//check if write was successful
	if(len != strlen(message))
	{
		fprintf(stderr, "Error: unable to write string \"%s\" to socket; server termination expected\n", message);
		exit(1);
	}	
	
	//get acknowledgement from server
	char ackBuf[BUF_SIZE];
	
	if(!getAcknowledgement(ackBuf))
	{
		return;	
	}
	
	//open up local file for reading
	int localfd = open(pathname, O_RDONLY);
	
	//check if the fd is valid
	if(localfd < 0)
	{
		fprintf(stderr, "Error: unable to open %s in local directory\n", filename);
		close(datafd);
		return;
	}
	
	char fileBuf[BUF_SIZE];
	int readLen;
	
	//read/write loop
	do
	{
		readLen = read(localfd, fileBuf, BUF_SIZE);
		write(datafd, fileBuf, readLen);
	}while(readLen >= BUF_SIZE);
	
	//close data connection and local file
	close(datafd);
	close(localfd);
	
	printf("Success.\n");
}