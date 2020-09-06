**NB: Run prima il server ed inserire il nome del file che si creerà in ricezione (e. ricevuto.txt, ricevuto.jpg). 
Poi Run client**

gcc invioImgServer.c -o server
./server 8888

gcc invioImgClient.c -o client
./client 127.0.0.1 8888

