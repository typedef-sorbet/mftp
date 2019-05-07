#include "mftp.h"

//	Author:	Spencer Warneke
//	Class:	CS 360
//	Assn:	Final

int servicePID;
int socketfd;

//Attempts to resolve the hostname of the connected client; returns the hostname of the string if successful, NULL otherwise.
char *getClientHostname(struct sockaddr_in *clientAddr);

//Creates a new socket on some available ephemeral port, and sends an acknowledgement to the client along with said port number
//openDataConenction returns the fd created by accept() on this socket connection to the client
int openDataConnection();

//Behavior of the server child process: read in lines from the control connection, perform actions, rinse and repeat.
void mftpService();

//Opens the file referred to in command and sends it over the data connection specified by datafd.
void sendFileToClient(int datafd, char *command);

//Creates the file referred to in command and fills it in with data from the data connection specified by datafd.
void getFileFromClient(int datafd, char *command);

//Attempts to chdir to the directory referred to in command.
void changeDirectory(char *command);

//Sends the output from an `ls -l` through the data connection referred to by datafd.
void listDirectory(int datafd);

int main()
{
	//socket setup
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	
	//if unable to open socket, don't continue
	if(listenfd < 0)
	{
		perror("Error: unable to open socket");
		exit(1);
	}
	
	//server address setup
	struct sockaddr_in servAddr;
	
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(PORT_NUM);
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);	

	//attempt to bind socket to port; if unsuccessful, don't continue
	if ( bind( listenfd, 
			(struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
			perror("Error: unable to bind the opened socket to given port number");
			exit(1);
	}
	
	//begin listening on the port (queue length of 4)
	listen(listenfd, 4);
	
	//client address struct setup
	int length = sizeof(struct sockaddr_in);
	struct sockaddr_in clientAddr;
	
	//print out success message
	printf("Server successfully established on port %d\n", PORT_NUM);
	
	while(1)
	{
		//attempt to accept a connection; if unsuccessful, don't continue
		if( ( (socketfd = accept(listenfd, (struct sockaddr *) &clientAddr, &length)) < 0 ) )
		{
			perror("Error: unable to establish connection or connection is invalid");
			exit(1);
		}
		
		//at this point, socketfd is now a valid file descriptor tied to the socket
		
		
		//Parent process
		if( (servicePID = fork()) ) 
		{
			//close the connection fd (not the listening fd!)
			close(socketfd);
			
			//attempt to resolve the hostname of the client
			char* clientName = getClientHostname(&clientAddr);
			
			//print out the client's hostname, or "unknown" if unable to resolve
			printf("Client has connected; hostname %s\n", (clientName == NULL)?"unknown":clientName);
			
			//wait until the child process has finished
			waitpid(servicePID, NULL, 0);
			
			//print out a disconnection notif
			printf("Client has disconnected\n");
		}
		else	//Child process
		{
			//close listening fd (not the connection fd!)
			close(listenfd);
			
			mftpService();
			
			//close the connection
			close(socketfd);
			
			//and exit
			exit(0);
		}
	}
}

void mftpService()
{
	//string buffers and relevant data
	char commandBuf[BUF_SIZE];		//buffer for reads from the client
	char messageBuf[BUF_SIZE];		//buffer for messages to the client
	int commandLen;
	
	//data connection stuff
	int datafd;
	bool hasDataConnection = false;

	//what do you mean I'm copying the example executable?
	int pid = getpid();
	
	//while we haven't hit EOF from the control connection
	while( (commandLen = readBytesFromSocket(socketfd, commandBuf, BUF_SIZE)) > 0 )
	{
		//it's been a while since i've had an excuse to use a switch block
		switch(commandBuf[0])
		{
			//Quit command
			case 'Q':
				printf("%5d: Client exiting...\n", pid);
				return;
			//Open data connection command
			case 'D':
				datafd = openDataConnection(pid);
				hasDataConnection = true;
				printf("%5d: Data connection established with fd %d\n", pid, datafd);
				break;
			//Get file command
			case 'G':
				printf("%5d: Client issued \"P\" command with file %s\n", pid, &commandBuf[1]);
				if(!hasDataConnection)
				{
					writeStringToSock(socketfd, "ENo Data Connection\n", strlen("ENo Data Connection\n"));
					fprintf(stderr, "%5d: Error: Client issued \"G\" command w/o data conn.\n", pid);
					break;
				}
				sendFileToClient(datafd, commandBuf);
				close(datafd);
				hasDataConnection = false;
				break;
			//Put file command
			case 'P':
				printf("%5d: Client issued \"P\" command with file %s\n", pid, &commandBuf[1]);
				if(!hasDataConnection)
				{
					writeStringToSock(socketfd, "ENo Data Connection\n", strlen("ENo Data Connection\n"));
					fprintf(stderr, "%5d: Error: Client issued \"P\" command w/o data conn.\n", pid);
					break;
				}
				getFileFromClient(datafd, commandBuf);
				close(datafd);
				hasDataConnection = false;
				break;
			//Change dir command
			case 'C':
				printf("%5d: Client issued \"C\" command to dir %s\n", pid, &commandBuf[1]);
				changeDirectory(commandBuf);
				break;
			//List dir command
			case 'L':
				printf("%5d: Client issued \"L\" command\n", pid);
				if(!hasDataConnection)
				{
					writeStringToSock(socketfd, "ENo Data Connection\n", strlen("ENo Data Connection\n"));
					fprintf(stderr, "%5d: Error: Client issued \"L\" command w/o data conn.\n", pid);
					break;
				}
				listDirectory(datafd);
				close(datafd);
				hasDataConnection = false;
				break;
			//this won't ever happen unless this program is used with a different executable
			default:
				writeStringToSock(socketfd, "EInvalid command\n", strlen("EInvalid command\n"));
				break;
		}
		
		//wipe the buffers, just in case
		memset(commandBuf, 0, BUF_SIZE);
		memset(messageBuf, 0, BUF_SIZE);
	}
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

//Atempts to resolve the hostname of the connected client
//Returns NULL if unsuccessful, or the string hostname if successful
char *getClientHostname(struct sockaddr_in *clientAddr)
{
	struct hostent* hostEntry;

	hostEntry = gethostbyaddr(&(clientAddr->sin_addr), 
			   sizeof(struct in_addr), AF_INET);

	if(hostEntry == NULL)
		return NULL;
	else
		return hostEntry->h_name;
}

int openDataConnection()
{
	char messageBuf[BUF_SIZE];
	
	int dataPort;
	
	//socket setup
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	
	//if unable to open socket, don't continue
	if(listenfd < 0)
	{
		perror("Error: unable to open socket");
		writeStringToSock(socketfd, "EUnable to open socket for data connection\n", strlen("EUnable to open socket for data connection\n"));
		exit(1);
	}
	
	//server address setup
	struct sockaddr_in servAddr;
	
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(0);					//any port wildcard
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);	

	//attempt to bind socket to port; if unsuccessful, don't continue
	if ( bind( listenfd, 
			(struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
			perror("Error: unable to bind");
			writeStringToSock(socketfd, "EUnable to bind\n", strlen("EUnable to bind\n"));
			exit(1);
	}
	
	//empty struct that we'll use to get the port number that bind gave us
	struct sockaddr_in addrWithData;
	
	memset(&addrWithData, 0, sizeof(struct sockaddr_in));
	
	int length = sizeof(addrWithData);
	
	//grab the data for listenfd and put it in addrWithData
	if ( getsockname(listenfd, (struct sockaddr *)&addrWithData, &length) != 0)
	{
		perror("Error: unable to get port from getsockname()");
		writeStringToSock(socketfd, "Egetsockname failed\n", strlen("Egetsockname failed\n"));
		exit(1);
	}
	
	//grab the port num; make sure byte order is correct
	dataPort = ntohs(addrWithData.sin_port);
	
	//print out success message
	printf("%5d: Data connection successfully established on port %d\n", getpid(), dataPort);
	
	//write acknowledgement message to client
	sprintf(messageBuf, "A%d\n", dataPort);
	writeStringToSock(socketfd, messageBuf, strlen(messageBuf));
	
	//begin listening on the port
	listen(listenfd, 1);
	
	//return the fd that accept gives us
	return accept(listenfd, NULL, NULL);
}

void sendFileToClient(int datafd, char *command)
{
	//if this line looks weird and convoluted, that's because it is
	//this replaces the first occurence of a newline char in command with a null byte
	//because chdir and open *really* don't like newline chars
	command[index(command, '\n') - command] = '\0';
	
	//filename starts at second character
	char *filename = &command[1];
	
	//open the file for reading
	int localfd = open(filename, O_RDONLY);
	
	//check if the fd is valid
	if(localfd < 0)
	{
		fprintf(stderr, "Error: unable to open %s in local directory\n", filename);
		writeStringToSock(socketfd, "EUnable to open file\n", strlen("EUnable to open file\n"));
		return;
	}
	
	//file buffers
	char fileBuf[BUF_SIZE];
	int readLen;
	
	do
	{
		readLen = read(localfd, fileBuf, BUF_SIZE);
		write(datafd, fileBuf, readLen);
	}while(readLen >= BUF_SIZE);
	
	//close the file
	close(localfd);
	
	//write an acknowledgement
	writeStringToSock(socketfd, "A\n", 2);
	printf("%5d: File successfully sent.\n", getpid());
}

void getFileFromClient(int datafd, char *command)
{
	//if this line looks weird and convoluted, that's because it is
	//this replaces the first occurence of a newline char in command with a null byte
	//because chdir really doesn't like newline chars
	command[index(command, '\n') - command] = '\0';
	
	//buffer for file bytes
	char fileBuffer[BUF_SIZE];
	
	//filename starts at second character
	char *filename = &command[1];
	
	//open file in local dir for writing; create if non-existant, truncate if existant.
	int localfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
	
	//check if the fd is valid
	if(localfd < 0)
	{
		fprintf(stderr, "Error: unable to open %s in local directory\n", filename);
		writeStringToSock(socketfd, "EUnable to open file\n", strlen("EUnable to open file\n"));
		return;
	}
	
	//write acknowledgement
	writeStringToSock(socketfd, "A\n", 2);
	
	int readLen;
	
	//while we still have data to read from the data connection
	while( (readLen = read(datafd, fileBuffer, BUF_SIZE)) > 0 )
	{
		//write out whatever we just read in
		write(localfd, fileBuffer, readLen);
	}
	
	//close the data connection and the local file
	close(localfd);
	
	printf("%5d: File successfully recieved.\n", getpid());
}

void changeDirectory(char *command)
{
	//if this line looks weird and convoluted, that's because it is
	//this replaces the first occurence of a newline char in command with a null byte
	//because chdir really doesn't like newline chars
	command[index(command, '\n') - command] = '\0';
	
	//filename starts at second character
	int check = chdir(&command[1]);
	
	//check if the chdir actually worked
	if(check == -1)
	{
		fprintf(stderr, "Error: unable to change directory; errno: %s\n", strerror(errno));
		writeStringToSock(socketfd, "EUnable to change dir\n", strlen("EUnable to change dir\n"));
	}
	else
	{
		writeStringToSock(socketfd, "A\n", 2);
		printf("%5d: Directory successfully changed to %s\n", getpid(), &command[1]);
	}
}

void listDirectory(int datafd)
{
	int pid;
	
	if(pid = fork())	//parent
	{
		//wait for the exec to finish
		waitpid(pid, NULL, 0);
	}
	else				//child
	{
		//duplicate datafd over stdout
		dup2(datafd, STDOUT_FILENO);
		//close original datafd
		close(datafd);
		
		//exec an ls -l
		execlp("ls", "ls", "-l", (char *)NULL);
	}
	
	//send an acknowledgement
	writeStringToSock(socketfd, "A\n", 2);
	printf("%5d: Directory successfully listed.\n", getpid());
}