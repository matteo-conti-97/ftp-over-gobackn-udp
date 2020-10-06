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
#include <math.h>

#define MAX_CHOICE_TIME 120
#define DEFAULT_TIMER 50
#define NORMAL 10
#define FIN 11
#define SYN 14
#define MAXLINE 497
#define PUT 1
#define GET 2
#define LIST 3

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

bool simulate_loss(float loss_rate);

void sig_alrm_handler(int signum);

void get(int sockfd, double timer, float loss_rate);

void list(int sockfd, double timer, float loss_rate);

void put(int sockfd, double timer, int window_size, float loss_rate);


int main(int argc, char *argv[]){
  int sockfd,n, serv_port, new_port, window_size, trial_counter=0;
  struct sockaddr_in servaddr, child_addr;
  struct sigaction sa;
  struct segment_packet data;
  struct ack_packet ack;
  long conn_req_no;
  float loss_rate;
  double timer, syn_timer;
  clock_t timer_sample; 
  bool dyn_timer_enable=false, timer_enable=false, SYN_sended=false;

  //Pulizia
  memset((void *)&data,0,sizeof(data));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&servaddr, 0, sizeof(servaddr));
  memset((void *)&child_addr, 0, sizeof(child_addr));

  //Controllo numero di argomenti
  if (argc < 6) { 
    fprintf(stderr, "utilizzo: client <indirizzo IPv4 server> <porta server> <dimensione finestra> <probabilita' perdita (float, -1 for 0)> <timeout (in ms double, -1 for dynamic timer)>\n");
    exit(EXIT_FAILURE);
  }

  //Controllo numero di porta
  if((serv_port=atoi(argv[2]))<1024){
    fprintf(stderr,"inserisci un numero di porta valido\n");
    exit(EXIT_FAILURE);
  }

  //Controllo dimensione finestra
  if((window_size=atoi(argv[3]))==0){
    fprintf(stderr,"inserisci dimensione finestra valida\n");
    exit(EXIT_FAILURE);
  }

  //Controllo probabilita' di perdita
  if((loss_rate=atof(argv[4]))==0){
      fprintf(stderr,"inserisci un loss rate valido\n");
      exit(EXIT_FAILURE);
  }

  if(loss_rate==-1)
    loss_rate=0;

  //Controllo timer
  if((timer=atof(argv[5]))==0){
    fprintf(stderr,"inserisci un timer valido\n");
    exit(EXIT_FAILURE);
  }

  if(timer<0){
    syn_timer=DEFAULT_TIMER;
    dyn_timer_enable=true;
  }
  else
    syn_timer=timer;

  //Seed per la perdita simulata
  srand48(2345);

  //Creazione socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
    perror("errore in socket");
    exit(EXIT_FAILURE);
  }

  //Assegnazione tipo di indirizzo
  servaddr.sin_family = AF_INET;
  //Assegnazione porta server
  servaddr.sin_port = htons(serv_port);
  //Assegnazione indirizzo IP del server
  if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
    perror("errore in inet_pton");
    exit(EXIT_FAILURE);
  }

  //Installazione gestore sigalarm
  sa.sa_handler = sig_alrm_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGALRM, &sa, NULL) < 0) {
    fprintf(stderr, "errore in sigaction");
    exit(EXIT_FAILURE);
  }

  //Per non usare sempre sendto e recvfrom
  if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    perror("errore in connect");
    exit(EXIT_FAILURE);
  }

  //Richiesta di connessione al server
  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il server e' morto
    if(trial_counter>10){
      printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      exit(EXIT_FAILURE);
    }
    

    if(!SYN_sended){
      //Invio SYN con numero di sequenza casuale come identificatore della connessione
      conn_req_no=lrand48();
      data.seq_no=htonl(conn_req_no);
      data.type=htons(SYN);
      if (send(sockfd, &data, sizeof(data), 0) < 0) {
        perror("errore in send richiesta connession syn");
        exit(EXIT_FAILURE);
      }

      SYN_sended=true;

      //Start timer
      timer_sample = clock();
      timer_enable = true;

      printf("SYN inviato\n");
    }

    //Timeout
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > syn_timer) && (timer_enable)){ 
      timer_sample = clock();

      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
      if(dyn_timer_enable)
        syn_timer=syn_timer*2;

      SYN_sended=false;
      trial_counter++;
      printf("Timeout SYN\n");
    }

    //Attendo SYNACK
    if(recv(sockfd, &data, sizeof(data), MSG_DONTWAIT)>0){
      if(!simulate_loss(loss_rate)){

         //Se l'identificatore non e' corretto il SYNACK non e' per me
        if(ntohl(data.seq_no)==conn_req_no){
          printf("Ricevuto SYNACK\n");
          timer_enable=false;
          break;
        }
      }
      else
        printf("PERDITA SYNACK SIMULATA\n");
    }
  }

  //Invio ACKSYNACK
  new_port=ntohs(atoi(data.data));

  ack.type=htons(SYN);
  ack.seq_no=htonl(conn_req_no);
  if (send(sockfd, &ack, sizeof(ack), 0) < 0) {
    perror("errore in send ack_syn_ack");
    exit(EXIT_FAILURE);
  }


  //Assegnazione tipo di indirizzo
  child_addr.sin_family = AF_INET;
  //Assegnazione porta del figlio dedicato
  child_addr.sin_port = new_port;
  //Assegnazione indirizzo IP del figlio
  if (inet_pton(AF_INET, argv[1], &child_addr.sin_addr) <= 0) {
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
    alarm(MAX_CHOICE_TIME);
    printf("Cosa posso fare per te? Hai due minuti per scegliere.\n");
    printf("1)PUT\n");
    printf("2)GET\n");
    printf("3)LIST\n");
    printf("Inserisci il numero dell'operazione da eseguire:\n");
    select_opt:
    if(scanf("%d",&n)!=1){
      perror("errore acquisizione operazione da eseguire");
      char c;
      while ((c = getchar()) != '\n' && c != EOF) { }
      goto select_opt;
    }
    switch(n){
      case PUT:
        //system("clear");
        alarm(0);
        put(sockfd, timer,window_size, loss_rate);
        //put(sockfd);
        break;
      case GET:
        //system("clear");
        alarm(0);
        get(sockfd, timer, loss_rate);
        break;
      case LIST:
        //system("clear");
        //list(sockfd);
        alarm(0);
        list(sockfd, timer, loss_rate);
        break;
      default:
        printf("Inserisci un numero valido\n");
        goto select_opt;
        break;
    }
  }
  close(sockfd);
  exit(EXIT_SUCCESS);
}

void list(int sockfd, double timer, float loss_rate){
  int trial_counter=0;
  struct segment_packet data;
  struct ack_packet ack;
  long expected_seq_no=0;
  clock_t timer_sample; 
  bool dyn_timer_enable=false, timer_enable=false, command_sended=false;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=htonl(-1);

  //Attivo timer dinamico
  if(timer<0){
    dyn_timer_enable=true;
    timer=DEFAULT_TIMER;
  }

  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il server e' morto
    if(trial_counter>10){
      printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      exit(EXIT_FAILURE);
    }

    if(!command_sended){
      
      //Invia al server il pacchetto di richiesta
      data.type=htons(LIST);
      if(send(sockfd, &data, sizeof(data), 0) < 0) {
        perror("errore in send comando");
        exit(EXIT_FAILURE);
      }
      printf("Inviata richiesta\n");
      command_sended=true;
      //Start timer
      timer_sample = clock();
      timer_enable = true;
    }

    //Timeout
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      timer_sample = clock();

      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;

      command_sended=false;
      trial_counter++;
      printf("Timeout comando\n");
    }


    //Attendo ACK richiesta
    if(recv(sockfd, &ack, sizeof(ack), MSG_DONTWAIT)>0){
      if(!simulate_loss(loss_rate)){
        printf("Ricevuto ack comando\n");
        timer_enable=false;
        break;
      }
      else
        printf("PERDITA ACK COMANDO SIMULATA\n");
    }
  }

  //Pulizia
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  trial_counter=0;
  ack.seq_no=htonl(-1);

  printf("Lista dei file su server:\n\n");

  //Ricevo dati
  while(1){

    //Se ci sono troppi errori di lettura lascio stare
    if(trial_counter>10){
      printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    //Attendo pacchetti dal server
    if(recv(sockfd, &data, sizeof(data), 0)<0){
      perror("Pacchetto corrotto errore recv\n");
      trial_counter++;
      continue;
    }
    if(!simulate_loss(loss_rate)){

      //Se arriva un pacchetto in ordine creo il relativo ack e aggiorno l'expected sequence number
      if(ntohl(data.seq_no)==expected_seq_no){

        //Se arriva il FIN esco
        if(ntohs(data.type)==FIN){
         if(ntohs(data.length)>0)
            printf("%s\n", data.data);
          else
            printf("Ho ricevuto FIN\n");
          ack.type=htons(FIN);
          ack.seq_no=data.seq_no;
          break;
        }

        //Se arriva un dato printo su stdout
        else{
          printf("%s\n",data.data);
          ack.type=htons(NORMAL);
          ack.seq_no=data.seq_no;
          expected_seq_no++;
        }
      }
      //Se arriva un pacchetto fuori ordine o corrotto non genero ack utilizzando quindi quello dell'iterazione precedente 

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

void put(int sockfd, double timer, int window_size, float loss_rate){
  int fd, trial_counter=0;
  struct segment_packet data;
  struct ack_packet ack;
  struct segment_packet *packet_buffer;
  long base=0,next_seq_no=0, file_size;
  clock_t start_sample_RTT, timer_sample; 
  double sample_RTT=0, estimated_RTT=0, dev_RTT=0;
  bool dyn_timer_enable=false, timer_enable=false, RTT_sample_enable=false, FIN_sended=false, command_sended=false;

  //Attivo timer dinamico
  if(timer<0){
    dyn_timer_enable=true;
    timer=DEFAULT_TIMER;
  }

  //Alloco il buffer della finestra
  if((packet_buffer=malloc(window_size*sizeof(struct segment_packet)))==NULL){
    perror("malloc fallita");
    data.length=htons(strlen("Putt fallita: errore interno del client"));
    strcpy(data.data,"Put fallita: errore interno del client");
    goto put_termination;
  }

  //Pulizia
  memset((void *)packet_buffer,0,sizeof(packet_buffer));
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=htonl(-1);

  //Scelta del file da caricare su server
  file_choice:
  printf("Inserire il nome del file da scaricare (con estensione):\n");
  if(scanf("%s",data.data)!=1){
    perror("inserire un nome valido");
    char c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    goto file_choice;
  }

  //Apro file
  if((fd = open(data.data, O_RDONLY, 0666))<0){
    perror("errore apertura/creazione file da ricevere controllare che il file sia presente sul server");
    data.length=htons(strlen("Putt fallita: errore interno del client"));
    strcpy(data.data,"Put fallita: errore interno del client");
    goto put_termination;
  }

  //Calcolo dimensione file
  lseek(fd, 0, SEEK_SET);
  file_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  //Invio richiesta
  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il server e' morto
    if(trial_counter>10){
      printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      close(fd);
      exit(EXIT_FAILURE);
    }

    if(!command_sended){
  
      //Invia al server il pacchetto di richiesta
      data.type=htons(PUT);
      if(send(sockfd, &data, sizeof(data), 0) <0) {
        perror("errore in send file name");
        close(fd);
        exit(EXIT_FAILURE);
      }
      printf("Inviata richiesta\n");
      command_sended=true;
      //Start timer
      timer_sample = clock();
      timer_enable = true;
    }

    //Timeout
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      timer_sample = clock();

      if(dyn_timer_enable)
        timer=timer*2;

      command_sended=false;
      trial_counter++;
      printf("Timeout comando\n");
    }


    //Attendo ACK richiesta
    if(recv(sockfd,&ack, sizeof(ack), MSG_DONTWAIT)>0){
      if(!simulate_loss(loss_rate)){
        printf("Ricevuto ack comando\n");
        timer_enable=false;
        break;
      }
      else
        printf("PERDITA ACK COMANDO SIMULATA\n");
    }
  }

  //Pulizia
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  trial_counter=0;
  ack.seq_no=htonl(-1);

  //Invio dati
  while((ntohl(ack.seq_no)+1)*497 < file_size){

    //Se ci sono troppe ritrasmissioni lascio stare
    if(trial_counter>10){
      printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    //Se la finestra non e' piena preparo ed invio il pacchetto
    if(next_seq_no < base+window_size){ 
      if((packet_buffer[next_seq_no%window_size].length = htons(read(fd, packet_buffer[next_seq_no%window_size].data, MAXLINE))) > 0){
        packet_buffer[next_seq_no%window_size].seq_no = htonl(next_seq_no);
        packet_buffer[next_seq_no%window_size].type = htons(NORMAL);
        send(sockfd, &packet_buffer[next_seq_no%window_size], sizeof(packet_buffer[next_seq_no%window_size]), 0);
        printf("Inviato pacchetto %ld e iniziato campionamento\n",packet_buffer[next_seq_no%window_size].seq_no);

        //Se e' attivato il timer dinamico campiono per calcolare l'rtt
        if((dyn_timer_enable)&&(!RTT_sample_enable)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Inviato pacchetto %d\n",ntohl(packet_buffer[next_seq_no%window_size].seq_no));

        //Se il next sequence number corrisponde con la base lancia il timer
        if(base == next_seq_no){
          timer_sample = clock();
          timer_enable = true;
        }

        next_seq_no++;
      }
    }

    //Scadenza del timer ritrasmetto tutto quello che era presente nella finestra
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      
      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;

      trial_counter++;
      timer_sample = clock();
      for(int i=0; i<window_size; i++){
        send(sockfd, &packet_buffer[i], sizeof(packet_buffer[i]), 0);

        if((dyn_timer_enable)&&(RTT_sample_enable==0)){
          start_sample_RTT = clock();
          RTT_sample_enable = true;
        }
        printf("Pacchetto %d ritrasmesso\n", ntohl(packet_buffer[i].seq_no));  
      }
    }

    //Controllo se ci sono ack
    if(recv(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT) > 0){ 
      if(!simulate_loss(loss_rate)){
        printf("ACK %d ricevuto, ricalcolo timer\n", ntohl(ack.seq_no));
        base = ntohl(ack.seq_no);

        //Azzero il contatore di tentativi di ritrasmissione in quanto se ricevo ACK il server e' vivo
        trial_counter=0;

        //Aggiorno timer dinamico
        if((dyn_timer_enable)&&(RTT_sample_enable==1)){
          RTT_sample_enable = false;
          sample_RTT=(double)(clock()-start_sample_RTT)*1000/CLOCKS_PER_SEC;
          printf("SAMPLE RTT %f\n", sample_RTT);
          estimated_RTT=(double)(0.875*estimated_RTT)+(0.125*sample_RTT);
          printf("ESTIMATED RTT %f\n", estimated_RTT);
          dev_RTT=(double)(0.75*dev_RTT)+(0.25*fabs(sample_RTT-estimated_RTT));
          printf("DEV RTT %f\n", dev_RTT);
          timer=(double)estimated_RTT+4*dev_RTT;
          printf("Nuovo timer %f\n",timer);
          //timer = (double)(clock()-sample_RTT)*1000/CLOCKS_PER_SEC; 
        } 
        //Stop del timer associato al pacchetto piu' vecchio della finestra 
        if(base == next_seq_no){
          timer_enable = false;
          printf("Ho fermato il timer\n");
        }
      }
      else
        printf("PERDITA ACK SIMULATA\n");
    }         
  }
  
  //Pulizia
  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));

  //Termine operazione
  put_termination:
  while(1){

    //Se faccio troppi tentativi lascio stare probabilmente il server e' morto
    if(trial_counter>10){
      if(ntohs(data.length)>0)
        printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      else
        printf("Il server e' morto oppure il canale e' molto disturbato tuttavia il file e' stato consegnato con successo\n");
      break;
    }

    //Invio il FIN solo se devo farlo per la prima volta o lo rinvio in caso di timeout per non inviarne inutilmente
    if(!FIN_sended){
      data.type=htons(FIN);
      data.seq_no=htonl(next_seq_no);
      send(sockfd, &data, sizeof(data), 0);
      FIN_sended=true;
      
      //Start timer
      timer_sample = clock();
      timer_enable = true;
      printf("Inviato FIN\n");
    } 

    //Timeout
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      timer_sample = clock();

      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;

      FIN_sended=false;
      trial_counter++;
      printf("Timeout FIN\n");
    }

    //Attendo FINACK
    if(recv(sockfd, &ack, sizeof(struct ack_packet), MSG_DONTWAIT)>0){
      if(!simulate_loss(loss_rate)){
        if(ntohs(ack.type)==FIN){
          printf("Ho ricevuto FIN ACK\n");
          break;
        }
      }
      else
        printf("PERDITA ACK FINALE SIMULATA\n");
    }
  }
  close(fd);
  printf("PUT terminata\n");
  exit(EXIT_SUCCESS);
}

void get(int sockfd, double timer, float loss_rate){
  int n, fd, trial_counter=0;
  struct segment_packet data;
  struct ack_packet ack;
  long expected_seq_no=0;
  char *rm_string;
  clock_t timer_sample; 
  bool dyn_timer_enable=false, timer_enable=false, command_sended=false;

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  ack.seq_no=htonl(-1);

  //Attivo timer dinamico
  if(timer<0){
    dyn_timer_enable=true;
    timer=DEFAULT_TIMER;
  }

  //Scelta del file da scaricare dal server
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

  //Apro file
  if((fd = open(data.data, O_RDWR | O_CREAT| O_TRUNC, 0666))<0){
    perror("errore apertura/creazione file da ricevere");
    exit(EXIT_FAILURE);
  }

  //Invio richiesta
  while(1){
    
    //Se faccio troppi tentativi lascio stare probabilmente il server e' morto
    if(trial_counter>10){
      printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      close(fd);
      system(rm_string);
      exit(EXIT_FAILURE);
    }

    if(!command_sended){

      //Invia al server il pacchetto di richiesta
      data.type=htons(GET);
      if(send(sockfd, &data, sizeof(data), 0) <0) {
        perror("errore in send file name");
        close(fd);
        system(rm_string);
        exit(EXIT_FAILURE);
      }
      printf("Inviata richiesta\n");
      command_sended=true;
      //Start timer
      timer_sample = clock();
      timer_enable = true;
    }

    //Timeout
    if(((double)(clock()-timer_sample)*1000/CLOCKS_PER_SEC > timer) && (timer_enable)){ 
      timer_sample = clock();

      //Se il timer scade probabilmente e' troppo breve, lo raddoppio
      if(dyn_timer_enable)
        timer=timer*2;
      command_sended=false;
      trial_counter++;
      printf("Timeout comando\n");
    }


    //Attendo ACK richiesta
    if(recv(sockfd,&ack, sizeof(ack), MSG_DONTWAIT)>0){
      if(!simulate_loss(loss_rate)){
        printf("Ricevuto ack comando\n");
        timer_enable=false;
        break;
      }
      else
        printf("PERDITA ACK COMANDO SIMULATA\n");
    }
  }

  memset((void *)&ack,0,sizeof(ack));
  memset((void *)&data,0,sizeof(data));
  trial_counter=0;
  ack.seq_no=htonl(-1);

  //Ricevo dati
  while(1){

    //Se ci sono troppi errori di lettura lascio stare
    if(trial_counter>10){
      printf("Il server e' morto oppure il canale e' molto disturbato ritenta piu' tardi\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    //Attendo pacchetti
    if(recv(sockfd, &data, sizeof(data), 0)<0){
      perror("Pacchetto corrotto errore recv\n");
      trial_counter++;
      continue;
    }

    if(!simulate_loss(loss_rate)){

      //Se arriva un pacchetto in ordine lo riscontro e aggiorno il numero di sequenza che mi aspetto
      if(ntohl(data.seq_no)==expected_seq_no){

        //Se e' un FIN esco
        if(ntohs(data.type)==FIN){

          //Se e' un FIN di errore printo l'errore, rimuovo il file sporco ed esco
          if(ntohs(data.length)>0){
            printf("%s\n", data.data);
            system(rm_string);
          }
          else
            printf("Ho ricevuto FIN\n");
          
          ack.type=htons(FIN);
          ack.seq_no=data.seq_no;
          break;
        }

        //Se non e' un FIN scrivo su file e continuo
        else{
          data.data[ntohs(data.length)]=0;
          printf("Ho ricevuto un dato di %d byte del pacchetto %d\n", ntohs(data.length), ntohl(data.seq_no));
          if((n=write(fd, data.data, ntohs(data.length)))!=ntohs(data.length)){
            perror("Non ho scritto tutto su file mi riposiziono\n");
            lseek(fd,0,SEEK_CUR-n);
            continue;
          }
          printf("Ho scritto %d byte sul file\n",n);
          ack.type=htons(NORMAL);
          ack.seq_no=data.seq_no;
          expected_seq_no++;
        }
      }
      //Se arriva un pacchetto fuori ordine o corrotto invio l'ack della precedente iterazione

      //Invio ack
      send(sockfd, &ack, sizeof(ack), 0);
      printf("ACK %d inviato\n", ntohl(ack.seq_no));

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
  printf("Tempo per la scelta terminato\n");
  exit(EXIT_FAILURE);
}
