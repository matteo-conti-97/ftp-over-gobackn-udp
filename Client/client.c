#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#define DEFAULT_TIMER 50000
#define NORMAL 10
#define FIN 11
#define SYN 14
#define MAXLINE 497
#define PUT 1
#define GET 2
#define LIST 3

bool timeout_event=false;

struct segment_packet {
    int type;
    bool is_ack;
    long seq_no;
    int length;
    char data[MAXLINE];
};

struct ack_packet {
    int type;
    long seq_no;
};

void set_timer(long timer);

bool simulate_loss(float loss_rate);

void sig_alrm_handler(int signum);

void get(int sockfd, long timer, float loss_rate);

void list(int sockfd, long timer, float loss_rate);

void put(int sockfd, long timer, int window_size, float loss_rate);


int main(int argc, char *argv[]){
  int sockfd,n, serv_port, new_port, window_size;
  struct sockaddr_in servaddr, child_addr;
  struct sigaction sa;
  struct segment_packet data;
  struct ack_packet ack;
  long conn_req_no, timer;
  float loss_rate;

  memset((void *)&data,0,sizeof(data));
  memset((void *)&ack,0,sizeof(ack));

  if (argc < 6) { /* controlla numero degli argomenti */
    fprintf(stderr, "utilizzo: client <indirizzo IPv4 server> <porta server> <dimensione finestra> <probabilita' perdita (float, -1 for 0)> <timeout (in us it is advisable not to go below milliseconds, -1 for dynamic timer)>\n");
    exit(EXIT_FAILURE);
  }

  if((serv_port=atoi(argv[2]))<1024){
    fprintf(stderr,"inserisci un numero di porta valido\n");
    exit(EXIT_FAILURE);
  }

  if((window_size=atoi(argv[3]))==0){
    fprintf(stderr,"inserisci dimensione finestra valida\n");
    exit(EXIT_FAILURE);
  }

  if((loss_rate=atof(argv[4]))==0){
      fprintf(stderr,"inserisci un loss rate valido\n");
      exit(EXIT_FAILURE);
  }

  if(timer==0){
    printf("Hai messo un timer nullo, il gobackn non e' realizzabile");
    exit(EXIT_FAILURE);
  }

  if(loss_rate==-1)
    loss_rate=0;

  if((timer=atol(argv[5]))==0){
    fprintf(stderr,"inserisci un timer valido\n");
    exit(EXIT_FAILURE);
  }

  //random seed per la perdita simulata
  srand48(2345);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(EXIT_FAILURE);
  }
  //Buffer extension
  /*int a = 65535;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &a, sizeof(int)) == -1) {
    perror("errore resize buffer ricezione");
  }
  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &a, sizeof(int)) == -1) {
    perror("errore resize buffer ricezione");
  }*/

  memset((void *)&servaddr, 0, sizeof(servaddr));
  memset((void *)&child_addr, 0, sizeof(child_addr));
  /* azzera servaddr */
  servaddr.sin_family = AF_INET;
  /* assegna il tipo di indirizzo */
  servaddr.sin_port = htons(serv_port); /* assegna la porta del server */
  /* assegna l'indirizzo del server prendendolo dalla riga di comando. L'indirizzo è
  una stringa da convertire in intero secondo network byte order. */
  if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
  /* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
    perror("errore in inet_pton");
    exit(EXIT_FAILURE);
  }

  sa.sa_handler = sig_alrm_handler;
  /* installa il gestore del segnale */
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGALRM, &sa, NULL) < 0) {
    fprintf(stderr, "errore in sigaction");
    exit(EXIT_FAILURE);
  }

  // Per non usare sempre sendto e recvfrom
  if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    perror("errore in connect");
    exit(EXIT_FAILURE);
  }

  conn_req_no=lrand48();
  data.seq_no=conn_req_no;
  data.type=SYN;
  if (send(sockfd, &data, sizeof(data), 0) < 0) {
    perror("errore in send richiesta connession syn");
    exit(EXIT_FAILURE);
  }
  
  while(1){
    memset((void *)&data,0,sizeof(data));

    if(timer<0)
      set_timer(DEFAULT_TIMER);
    else
      set_timer(timer);

    if(recv(sockfd, &data, sizeof(data), 0)<0){
      perror("errore in recv porta dedicata syn_ack");
      exit(EXIT_FAILURE);
    }
    set_timer(0);
    if(data.seq_no==conn_req_no)
      break;
  }
  new_port=atoi(data.data);
  ack.type=SYN;
  ack.seq_no=conn_req_no;
  if (send(sockfd, &ack, sizeof(ack), 0) < 0) {
    perror("errore in send ack_syn_ack");
    exit(EXIT_FAILURE);
  }


  /* azzera child_addr */
  child_addr.sin_family = AF_INET;
  /* assegna il tipo di indirizzo */
  child_addr.sin_port = new_port; /* assegna la porta del figlio */
  if (inet_pton(AF_INET, argv[1], &child_addr.sin_addr) <= 0) {
  /* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
    perror("errore in inet_pton");
    exit(EXIT_FAILURE);
  }

  // Per non usare sempre sendto e recvfrom
  if (connect(sockfd, (struct sockaddr *) &child_addr, sizeof(child_addr)) < 0) {
    perror("errore in connect");
    exit(EXIT_FAILURE);
  }
  printf("Ti sei connesso con successo\n");

  while(1){
    printf("Cosa posso fare per te? Hai un minuto per scegliere\n");
    printf("1)PUT\n");
    printf("2)GET\n");
    printf("3)LIST\n");
    printf("Inserisci il numero dell'operazione da eseguire\n");
    selectOpt:
    if(scanf("%d",&n)!=1){
      perror("errore acquisizione operazione da eseguire");
      exit(1);
    }
    switch(n){
      case PUT:
        //system("clear");
        put(sockfd, timer,window_size, loss_rate);
        //put(sockfd);
        break;
      case GET:
        //system("clear");
        get(sockfd, timer, loss_rate);
        break;
      case LIST:
        //system("clear");
        //list(sockfd);
        list(sockfd, timer, loss_rate);
        break;
      default:
        printf("Inserisci un numero valido\n");
        goto selectOpt;
        break;
    }
  }
  close(sockfd);
  exit(EXIT_SUCCESS);
}

void list(int sockfd, long timer, float loss_rate){
  int trial_counter=0;
  struct segment_packet data;
  struct ack_packet ack;
  long expected_seq_no=0;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));

  data.type=LIST;
  while(1){
    if(trial_counter>10){
      printf("Il server e' morto");
      exit(EXIT_FAILURE);
    }
    /* Invia al server il pacchetto di richiesta*/
    if(send(sockfd, &data, sizeof(data), 0) != sizeof(data)) {
      perror("errore in send comando");
      exit(EXIT_FAILURE);
    }

    if(timer<0)
      set_timer(DEFAULT_TIMER);
    else
      set_timer(timer);

    printf("Inviato comando\n");
    if(recv(sockfd,&ack, sizeof(ack),0) == sizeof(ack)){
    printf("Ricevuto ack comando\n");
    set_timer(0);
    trial_counter=0;
      break;
    }
    perror("errore recv comando");
    trial_counter++;
  }
  trial_counter=0;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  printf("Lista dei file su server:\n\n");

  //Ricevo dati
  while(1){
    if(trial_counter>10){
      printf("Il server e' morto o e' irraggiungibile\n");
      exit(EXIT_FAILURE);
    }
    if(timeout_event){
      trial_counter++;
      timeout_event=false;
    }

    if(timer<0)
      set_timer(DEFAULT_TIMER);
    else
      set_timer(timer);

    if(recv(sockfd, &data, sizeof(data), 0)!=sizeof(data)){
      perror("Pacchetto corrotto errore recv\n");
      continue;
    }
    if(!simulate_loss(loss_rate)){
      set_timer(0);
      trial_counter=0;
      //Se arriva un pacchetto in ordine lo riscontro e aggiorno il numero di sequenza che mi aspetto
      if(data.seq_no==expected_seq_no){
        if(data.type==FIN){
          printf("Ho ricevuto FIN\n");
          ack.type=FIN;
          ack.seq_no=data.seq_no;
          break;
        }
        else{
          printf("%s\n",data.data);
          ack.type=NORMAL;
          ack.seq_no=data.seq_no;
          expected_seq_no++;
        }
      }
      //Se arriva un pacchetto fuori ordine o corrotto invio l'ack della precedente iterazione

      //Invio ack
      send(sockfd, &ack, sizeof(ack), 0);
      //printf("ACK %ld inviato\n",ack.seq_no);

   }
    else
      printf("PERDITA PACCHETTO SIMULATA\n");
  } 

  //Invio ack finale
  send(sockfd, &ack, sizeof(ack), 0);
  printf("FIN ACK inviato\n");
  printf("\nLIST terminata\n\n");
  exit(EXIT_SUCCESS);
}

void put(int sockfd, long timer, int window_size, float loss_rate){
  int fd, trial_counter=0;
  struct segment_packet data;
  struct ack_packet ack;
  struct segment_packet *packet_buffer;
  long base=0,next_seq_no=0, file_size;
  long sample_RTT=0, estimated_RTT=0, dev_RTT=0, dyn_timer_value=DEFAULT_TIMER; //microsecondi
  struct timeval start_sample, end_sample;

  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    exit(EXIT_FAILURE);
  }

  memset((void *)&start_sample,0,sizeof(start_sample));
  memset((void *)&end_sample,0,sizeof(end_sample));
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  file_choice:
  printf("Inserire il nome del file da scaricare (con estensione):\n");
  if(scanf("%s",data.data)!=1){
    perror("inserire un nome valido");
    char c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    goto file_choice;
  }

  //apro file
  if((fd = open(data.data, O_RDONLY, 0666))<0){
    perror("errore apertura/creazione file da ricevere controllare che il file sia presente sul server");
    exit(EXIT_FAILURE);
  }

  //Calcolo dimensione file
  lseek(fd, 0, SEEK_SET);
  file_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  //Invio richiesta
  data.type=PUT;
  while(1){
    if(trial_counter>10){
      printf("Il server e' morto");
      close(fd);
      exit(EXIT_FAILURE);
    }
    /* Invia al server il pacchetto di richiesta*/
    if(send(sockfd, &data, sizeof(data), 0) != sizeof(data)) {
      perror("errore in send file name");
      close(fd);
      exit(EXIT_FAILURE);
    }
    set_timer(timer);
    printf("Inviato nome file da aprire\n");
    if(recv(sockfd,&ack, sizeof(ack),0) == sizeof(ack)){
    printf("Ricevuto ack comando\n");
    set_timer(0);
    trial_counter=0;
      break;
    }
    perror("errore recv comando");
    trial_counter++;
  }
  trial_counter=0;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  //Invio dati
  while((ack.seq_no+1)*497 < file_size){
    if(trial_counter>10){
      printf("Abort il server e' morto o e' irraggiungibile\n");
      close(fd);
      exit(EXIT_FAILURE);
    }
    //Se c'e' stato un timeout ritrasmetti tutti i pacchetti in finestra
    if(timeout_event){
      trial_counter++;
      if(timer<0)
        set_timer(dyn_timer_value);
      else
        set_timer(timer);

      gettimeofday(&start_sample,NULL);
      for(int i=0; i<window_size; i++){
          send(sockfd, &packet_buffer[i],sizeof(packet_buffer[i]), 0);
      }
      printf("Rinviati pacchetti e iniziato campionamento\n");
      timeout_event=false;
    }    

    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((packet_buffer[next_seq_no%window_size].length = read(fd, packet_buffer[next_seq_no%window_size].data, MAXLINE)) > 0){
        packet_buffer[next_seq_no%window_size].seq_no = next_seq_no;
        packet_buffer[next_seq_no%window_size].type = NORMAL;
        send(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0);
        gettimeofday(&start_sample,NULL);
        printf("Inviato pacchetto %ld e iniziato campionamento\n",packet_buffer[next_seq_no%window_size].seq_no);

        //Se il next sequence number corrisponde con la base lancia il timer
        if(base == next_seq_no){
          if(timer<0){
            set_timer(dyn_timer_value);
            printf("Ho lanciato il timer dinamico di %ld us\n",dyn_timer_value);
          }
          else{
            set_timer(timer);
            printf("Ho lanciato il timer statico di %ld us\n",timer);
          } 
        }

        next_seq_no++;
      }
    }

    //Controllo se ci sono ack
    if(recv(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT) > 0){ 
      if(!simulate_loss(loss_rate)){
        trial_counter=0; //Vuoldire che il client e' ancora vivo
        printf("ACK %ld ricevuto, ricalcolo timer\n",ack.seq_no);
        base = ack.seq_no+1;
        gettimeofday(&end_sample,NULL);
        sample_RTT=((end_sample.tv_sec - start_sample.tv_sec) * 1000000) + (end_sample.tv_usec - start_sample.tv_usec);
        //printf("sample_RTT %ld\n",sample_RTT);
        estimated_RTT=(estimated_RTT*0.875)+(sample_RTT*0.125);
        //printf("estimated_RTT %ld\n",estimated_RTT);
        dev_RTT=(dev_RTT*0.75)+((abs(sample_RTT-estimated_RTT))*0.25);
        //printf("dev_RTT %ld\n",dev_RTT);
        dyn_timer_value=estimated_RTT+(dev_RTT*4);
        //printf("Il nuovo timer e' %ld\n", dyn_timer_value);
        //Stop del timer associato al pacchetto piu' vecchio della finestra 
        if(base == next_seq_no){
          set_timer(0);
          printf("Ho fermato il timer\n");
        }
        else{
          if(timer<0){
            set_timer(dyn_timer_value);
            printf("Ho rilanciato il timer dinamico di %ld us\n",dyn_timer_value);
          }
          else{
            set_timer(timer);
            printf("Ho rilanciato il timer statico di %ld us\n",timer);
          }
        }
      }
      else
        printf("PERDITA ACK SIMULATA\n");
      
    }         
  }
  
  trial_counter=-1;
  while(1){
    timeout_event=false;
    trial_counter++;
    if(trial_counter>10){
      printf("Ho consegnato il pacchetto con successo ma il server e' morto o e' irraggiungibile\n");
      exit(EXIT_SUCCESS);
    }
    data.type=FIN;
    data.seq_no=next_seq_no;
    send(sockfd, &data, sizeof(data), 0);
    printf("Inviato FIN\n");
    if(timer<0){
        set_timer(dyn_timer_value);
        printf("Ho lanciato il timer dinamico finale di %ld us\n",dyn_timer_value);
      }
      else{
        set_timer(timer);
        printf("Ho lanciato il timer statico finale di %ld us\n",timer);
      }
    recv(sockfd, &ack, sizeof(struct ack_packet),0);
    if(!simulate_loss(loss_rate)){
      if(ack.type==FIN){
        printf("Ho ricevuto FIN ACK\n");
        set_timer(0);
        break;
      }
   }
   else
    printf("PERDITA ACK FINALE SIMULATA\n");
  }
  close(fd);
  printf("Put terminata\n");
  exit(EXIT_SUCCESS);
}

void get(int sockfd, long timer, float loss_rate){
  int n, fd, trial_counter=0;
  struct segment_packet data;
  struct ack_packet ack;
  long expected_seq_no=0;
  char *rm_string;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  file_choice:
  printf("Inserire il nome del file da scaricare (con estensione):\n");
  if(scanf("%s",data.data)!=1){
    perror("inserire un nome valido");
    char c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    goto file_choice;
  }

  printf("E' stato scelto %s \n", data.data);
  //Utile solo per la pulizia della directory in caso di errori
  rm_string=malloc(strlen(data.data)+3);
  sprintf(rm_string,"rm %s",data.data);

  //apro file
  if((fd = open(data.data, O_RDWR | O_CREAT| O_TRUNC, 0666))<0){
    perror("errore apertura/creazione file da ricevere controllare che il file sia presente sul server");
    exit(EXIT_FAILURE);
  }

  //Invio richiesta
  data.type=GET;
  while(1){
    if(trial_counter>10){
      printf("Il server e' morto");
      close(fd);
      system(rm_string);
      exit(EXIT_FAILURE);
    }
    /* Invia al server il pacchetto di richiesta*/
    if(send(sockfd, &data, sizeof(data), 0) != sizeof(data)) {
      perror("errore in send file name");
      close(fd);
      system(rm_string);
      exit(EXIT_FAILURE);
    }

    /*if(timer<0)
      set_timer(DEFAULT_TIMER);
    else
      set_timer(timer);*/

    printf("Inviato nome file da aprire\n");
    if(recv(sockfd,&ack, sizeof(ack),0) == sizeof(ack)){
    printf("Ricevuto ack comando\n");
    //set_timer(0);
    trial_counter=0;
      break;
    }
    perror("errore recv comando");
    trial_counter++;
  }
  trial_counter=0;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=-1;

  //Ricevo dati
  while(1){
    if(trial_counter>10){
      printf("Il server e' morto o e' irraggiungibile\n");
      close(fd);
      system(rm_string);
      exit(EXIT_FAILURE);
    }
    if(timeout_event){
      trial_counter++;
      timeout_event=false;
    }

    /*if(timer<0)
      set_timer(DEFAULT_TIMER);
    else
      set_timer(timer);*/

    if(recv(sockfd, &data, sizeof(data), 0)!=sizeof(data)){
      perror("Pacchetto corrotto errore recv\n");
      continue;
    }
    if(!simulate_loss(loss_rate)){
      //set_timer(0);
      trial_counter=0;
      //Se arriva un pacchetto in ordine lo riscontro e aggiorno il numero di sequenza che mi aspetto
      if(data.seq_no==expected_seq_no){
        if(data.type==FIN){
          printf("Ho ricevuto FIN\n");
          ack.type=FIN;
          ack.seq_no=data.seq_no;
          break;
        }
        else{
          data.data[data.length]=0;
          printf("Ho ricevuto un dato di %d byte del pacchetto %ld\n", data.length, data.seq_no);
          if((n=write(fd, data.data, data.length))!=data.length){
            perror("Non ho scritto tutto su file mi riposiziono\n");
            lseek(fd,0,SEEK_CUR-n);
            continue;
          }
          printf("Ho scritto %d byte sul file\n",n);
          ack.type=NORMAL;
          ack.seq_no=data.seq_no;
          expected_seq_no++;
        }
      }
      //Se arriva un pacchetto fuori ordine o corrotto invio l'ack della precedente iterazione

      //Invio ack
      send(sockfd, &ack, sizeof(ack), 0);
      printf("ACK %ld inviato\n",ack.seq_no);

   }
    else
      printf("PERDITA PACCHETTO SIMULATA\n");
  } 

  //Invio ack finale
  send(sockfd, &ack, sizeof(ack), 0);
  printf("FIN ACK inviato\n");
  close(fd);
  printf("\nGET terminata\n\n");
  exit(EXIT_SUCCESS);
}

bool simulate_loss(float loss_rate){
    double rv;
    rv = drand48();
    if (rv < loss_rate)
    {
        return true;
    } else {
        return false;
    }
}

void sig_alrm_handler(int signum){
  printf("SIGALRM\n");
  timeout_event=true;
}

void set_timer(long timer){
  struct itimerval timer_info;
  struct timeval timer_value;

  memset((void *)&timer_info,0,sizeof(timer_info));
  memset((void *)&timer_value,0,sizeof(timer_value));

  timer_value.tv_usec=timer;
  timer_info.it_value=timer_value;

  if (setitimer (ITIMER_REAL, &timer_info, NULL) < 0)
    perror("Errore set timer\n");

}