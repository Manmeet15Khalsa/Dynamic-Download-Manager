#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include<strings.h>
#include<stdlib.h>
int main(int argc, char**argv)
{
   int sockfd,n;
   struct sockaddr_in servaddr,cliaddr;
   char sendline[1000];
   char recvline[1000];

   int num_conns=0;
  
   sockfd=socket(AF_INET,SOCK_STREAM,0);

   bzero(&servaddr,sizeof(servaddr));
   servaddr.sin_family = AF_INET;
   servaddr.sin_addr.s_addr=inet_addr("172.17.20.142");
   servaddr.sin_port=htons(9999);

   if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))==0)
	printf("\n connection successfully established\n");
   else
	printf("connection couldn't be established");
   while(read(sockfd,&num_conns,sizeof(num_conns))!=0)
	{
	  printf("number of connections : %d\n",num_conns);
	}
	
  /* while (fgets(sendline, 10000,stdin) != NULL)
   {
      sendto(sockfd,sendline,strlen(sendline),0,
             (struct sockaddr *)&servaddr,sizeof(servaddr));
      n=recvfrom(sockfd,recvline,10000,0,NULL,NULL);
      recvline[n]=0;
      fputs(recvline,stdout);
   }*/
}

