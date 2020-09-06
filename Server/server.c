#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h> 
#define SERV_PORT 5192
#define MAXLINE 1024
#define PUT 1
#define GET 2
#define LIST 3

void get(int sockfd, struct sockaddr_in addr){
  int fd,n;
  char buff[MAXLINE];
  memset(buff,0,sizeof(buff));
  fd=open("imgPiccola.jpg",O_RDONLY);
  while((n = read(fd, buff, MAXLINE))>0)
  {
    //usleep(1000);
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

void put(int sockfd, struct sockaddr_in addr){
  int n, len, fd, command=2;
  char buff[MAXLINE];
  len=sizeof(addr);
  //apro file
  fd = open("files/prova1.jpg", O_RDWR | O_CREAT| O_TRUNC, 0666);

   while((n = recvfrom(sockfd, buff, MAXLINE, 0, (struct sockaddr *) &addr, &len))) {
        buff[n] = 0;
        if (!(strcmp(buff, "End"))) {
            break;
        }
        write(fd, buff, n);
    }
  printf("Ho finito di ricevere\n");
  close(fd);
}

void list(int sockfd, struct sockaddr_in addr){
  DIR *d;
  struct dirent *dir;
  int n;
  d = opendir("./files");
  while ((dir = readdir(d)) != NULL) {
    if((strcmp(dir->d_name,".")==0)||(strcmp(dir->d_name,"..")==0)) continue;
    if (sendto(sockfd, &(dir->d_name), sizeof(dir->d_name), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      perror("errore in sendto");
      exit(-1);
    }
  }
  printf("Ho finito di inviare\n");
  sendto(sockfd, "End", strlen("End"), 0, (struct sockaddr *) &addr, sizeof(addr));
  closedir(d);
}

int main(int argc, char *argv[ ]){
  int sockfd, len, command;
  struct sockaddr_in addr;
  unsigned char buff[MAXLINE];

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(-1);
  }
  //Buffer extension
  /*int a = 65535;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &a, sizeof(int)) == -1) {
    perror("errore resize buffer ricezione");
  }
  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &a, sizeof(int)) == -1) {
    perror("errore resize buffer ricezione");
  }*/
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
    if ((recvfrom(sockfd, &command, sizeof(int), 0, (struct sockaddr *)&addr, &len)) < 0) {
      perror("errore in recvfrom");
      exit(-1);
    }
  
  switch(command){
    case PUT:
      put(sockfd, addr);
      break;
    case GET:
      get(sockfd, addr);
      break;
    case LIST:
      list(sockfd, addr);
      break;
    default:
      break;
  }
 
  }
   exit(0);
}


    