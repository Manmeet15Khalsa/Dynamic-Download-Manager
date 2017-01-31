
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <errno.h>
#include<stdlib.h>
#include<memory.h>
#include<string.h>
#define MY_PORT		9999
#define MAXBUF		1024
#define MIN_VALUE       3
int main(int Count, char *Strings[])
{   int sockfd;
	struct sockaddr_in self;
	char buffer[MAXBUF];

	
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("Socket");
		exit(errno);
	}

	
	bzero(&self, sizeof(self));
	self.sin_family = AF_INET;
	self.sin_port = htons(MY_PORT);
	self.sin_addr.s_addr = INADDR_ANY;

	
    if ( bind(sockfd, (struct sockaddr*)&self, sizeof(self)) != 0 )
	{
		perror("socket--bind");
		exit(errno);
	}

	
	if ( listen(sockfd, 20) != 0 )
	{
		perror("socket--listen");
		exit(errno);
	}
    
     int clientfd;
     printf("\n server listening on port 9999...\n");
	while (1)
	{	
		struct sockaddr_in client_addr;
		int addrlen=sizeof(client_addr);

		int bandwidth=4;
		clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
		printf("%s:%d connected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                while(1){

                   bandwidth=(((random())%20)+bandwidth)/2;
		   if((bandwidth==0)||(bandwidth==1)||(bandwidth==2))
			bandwidth=MIN_VALUE;
		   sleep(5);

                  printf("\n The allowed_conn is %d",bandwidth);
                  
                  write(clientfd, &bandwidth, sizeof(bandwidth));

                 
                 }

		

	}
     
	
	close(sockfd);
	return 0;
}

