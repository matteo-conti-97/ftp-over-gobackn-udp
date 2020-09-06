#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#define SERV_PORT 5192
#define MAXLINE 1024

int main(int argc, char *argv[ ]){
  int sockfd, len, n, fd;
  struct sockaddr_in addr;
  unsigned char buff[MAXLINE];

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(-1);
  }

  memset((void *)&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); /* il server accetta pacchetti su
  una qualunque delle sue interfacce di rete */
  addr.sin_port = htons(SERV_PORT); /* numero di porta del server */
  
  /* assegna l'indirizzo al socket */
  if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("errore in bind");
    exit(-1);
  }

  while (1) {
    len = sizeof(addr);
    if ((recvfrom(sockfd, buff, MAXLINE, 0, (struct sockaddr *)&addr, &len)) < 0) {
      perror("errore in recvfrom");
      exit(-1);
    }
    
    fd=open("download.jpg",O_RDONLY);
    while((n = read(fd, buff, MAXLINE))>0)
    {
      usleep(1000);
      if (sendto(sockfd, buff, n, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0)
      {
        perror("errore in sendto");
        exit(-1);
      }
      memset(buff,0,sizeof(buff));
    }
    printf("Ho finito di inviare\n");
    sendto(sockfd, "End", strlen("End"), 0, (struct sockaddr *) &addr, sizeof(addr));
    close(fd);
  }

  exit(0);
}


    