#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

/* ************************************************************************** */

#define MAXPENDING 10

/* Variabile globale che tiene traccia del numero degli utenti che stanno giocando. */
int COUNTPLAYER = 0;
/* Condition variable associata a COUNTPLAYER. */
pthread_cond_t COUNTPLAYER_FLAG = PTHREAD_COND_INITIALIZER;

/* Condition variable associata a waitingList. */
pthread_cond_t LIST_FLAG = PTHREAD_COND_INITIALIZER;

/* Mutex associato alla variabile globale COUNTPLAYER. */
pthread_mutex_t MUTEX_COUNT = PTHREAD_MUTEX_INITIALIZER;

/* Mutex associato alla struttura globale playerList. */
pthread_mutex_t MUTEX_LIST = PTHREAD_MUTEX_INITIALIZER;

/* Strutttura che viene allocata per ogni thread (quindi ogni partita) per tenere traccia delle informazioni dei due client che giocano. */
typedef struct Arg {
   int  clientSd [2];          /* Clients sockets.          */
   char * name1;               /* Nome del primo client.    */
   char * name2;               /* Nome del secondo client.  */
}arg;

/* Strutttura che viene allocata per ogni client che decide di giocare, per tenere traccia delle proprie informazioni. */
typedef struct PlayerList {
   int clientSd;              /* Clients sockets.   */
   ushort recycled;           /* Utente che ha già effettuato almeno una partita da quando è connesso */
   char * name;               /* Nome del client.   */
   struct PlayerList * next;
}playerList;

playerList * waitingList = NULL;


/* ************************************************************************** */

/*
* handler.
*/

/*Nel caso si inserisce nel server la parola "close" il processo figlio muore e invia il segnale SIGCHLD al padre, che esegue questo handler. */
void abrt () {

  if (wait (NULL) < 0) {
      perror ("Error wait\n");
      exit (-1);
  }
  printf("\nIl server è stato arrestato...\n");
  exit (0);
}


/*
* Funzioni di lettura dalla socket.
*/

/* Legge e restituisce un int dalla socket del client. */
int readClientInt (int clientSd) {

    int msg = 0;

    if (read (clientSd, &msg, sizeof(int)) != sizeof(int)) {
        return -1;
    }

    return msg;
}


/* ************************************************************************** */


/*
* Funzioni di scrittura dalla socket.
*/

/* Scrive un messaggio (un char) alla socket del client. */
void writeClientMsg (int clientSd, char msg) {

     if (write (clientSd, &msg, sizeof(char)) != sizeof(char)) {
          perror("ERROR. Scrittura del messaggio sulla socket del client");
          exit (-1);
      }
}


/* Scrive un intero alla socket del client. */
void writeClientInt (int clientSd, int msg) {

    if (write (clientSd, &msg, sizeof(int)) != sizeof(int)) {
        perror("ERROR. Scrittura di un intero sulla socket del client");
        exit (-1);
    }
}


/* ************************************************************************** */


/*
* Funzioni di lista.
*/

/* Inserimento in coda (i primi inseriti saranno i primi a poter giocare) alla lista di attesta. */
void insertWaitingList (char * name, int clientSd, ushort recycled) {

  playerList * temp = waitingList;
  /* Primo inserimento. */
  if (temp == NULL) {
      temp = (playerList *) malloc (sizeof (playerList));
      temp->name = (char *) malloc (20 * sizeof (char));
      temp->next = waitingList;
      temp->name = name;
      temp->recycled = recycled;
      temp->clientSd = clientSd;
      waitingList = temp;
  }
  /* Dal secondo inserimento in poi. */
  else {
      while (temp->next != NULL) {
        temp = temp->next;
      }
      temp->next = (playerList *) malloc (sizeof (playerList));
      temp = temp->next;
      temp->name = (char *) malloc (20 * sizeof (char));
      temp->name = name;
      temp->recycled = recycled;
      temp->clientSd = clientSd;
      temp->next = NULL;
  }
}


/* Eliminazione dalla testa della lista di attesta. */
void deleteWaitingList () {

  playerList * temp = waitingList;
  waitingList = waitingList->next;
  free (temp->name);
  free (temp);

}


/* ************************************************************************** */


/*
* Funzione per stabilire la connessione.
*/

/* Imposta la socket del server. */
int setupSd (unsigned short serverPort) {

  struct sockaddr_in ServerAddr;
  int socketDes;                               /* Socket descriptor per il server. */

  /* Creazione della socket */
  if ( (socketDes = socket (PF_INET, SOCK_STREAM, 0)) < 0) {
      perror ("Socket error");
      exit (-1);
  }

  /* Costruzione della struttura. */
  ServerAddr.sin_family = AF_INET;
  ServerAddr.sin_port = htons (serverPort);
  ServerAddr.sin_addr.s_addr = htonl (INADDR_ANY);
  memset (&(ServerAddr.sin_zero), '\0', sizeof (ServerAddr.sin_zero));   /* Riempimento del campo sin_zero della struttura. */

  /* Permette a bind di assegnare un indirizzo che risulta ancora occupato. */
  if (setsockopt (socketDes, SOL_SOCKET, SO_REUSEADDR, (void *) &socketDes, sizeof(int)) < 0) {
      perror("setsockopt failed\n");
      exit (-1);
  }

  /* Associazione dell'indirizzo alla socket. */
  if ( (bind (socketDes, (struct sockaddr *) &ServerAddr, sizeof(ServerAddr))) != 0) {
      perror ("Bind error");
      exit (-1);
  }

  /* Ritorna il socket descriptor. */
  return socketDes;
}



void * acceptNewConnections (void * servSd) {

  int socketDes = * (int *) servSd;
  char * name;
  int clientSd;
  socklen_t clilen;
  struct sockaddr_in ClientAddr;

  clilen = sizeof (ClientAddr);

  pid_t pid;
  char * closeServer = (char *) malloc (10 * sizeof(char));


  if ( (pid = fork()) < 0) {
      perror ("Error. fork");
      exit (-1);
  }
  else if (pid == 0) {

      do {
          scanf("%s", closeServer);
      } while (strcmp(closeServer, "close") != 0);

      if (close (socketDes) != 0) {
          perror ("ERROR. Chiusura socket");
          exit (-1);
      }

      free (closeServer);

      exit (0);

  }
  else {

      /* Quando il figlio muore, manda il segnale SIGCHLD al padre. */
      signal (SIGCHLD, abrt);

      while (1) {

        /* Accetta nuove connessioni evitando l'attesa attiva nel caso si è raggiunto il numero massimo di partite. */
        pthread_mutex_lock (&MUTEX_COUNT);
          while (COUNTPLAYER >= 30) {
              printf("\nRaggiunto il numero massimo di partite.\n");
              if (pthread_cond_wait (&COUNTPLAYER_FLAG, &MUTEX_COUNT) != 0) {
                  perror ("Error pthread_cond_wait");
                  exit (-1);
              }
          }
        pthread_mutex_unlock (&MUTEX_COUNT);

          /* Accetta la connessione del client. */
          if ((clientSd = accept (socketDes, (struct sockaddr *) &ClientAddr, &clilen)) < 0) {
              perror ("Accept error");
              exit (-1);
          }

          name = (char *) malloc (20 * sizeof(char));
          if (name == NULL) {
              perror("\nMemoria esaurita\n");
              exit(-1);
          }

          memset (name, '\0', 20 * (sizeof  (char)));

          /* Riceve il nome del client appena connesso.*/
          if (read (clientSd, name, (20 * sizeof (char))) < 0) {
              perror("ERROR. Lettura del messaggio sulla socket del client");
              exit (-1);
          }

          printf ("\nNuova connessione \n");
          printf ("Indirizzo del client %s:   %s \n" , name, inet_ntoa (ClientAddr.sin_addr) );


          pthread_mutex_lock (&MUTEX_LIST);
            insertWaitingList (name, clientSd, 0);
            if (pthread_cond_broadcast (&LIST_FLAG) != 0) {
                perror ("Error pthread_cond_broadcast");
                exit (-1);
            }
          pthread_mutex_unlock (&MUTEX_LIST);

          /* Incrementa il count dei giocatori. */
          pthread_mutex_lock (&MUTEX_COUNT);
            COUNTPLAYER ++;
            printf("\nIl numero di utenti attivi in questo momento è: %d.\n", COUNTPLAYER);
          pthread_mutex_unlock (&MUTEX_COUNT);
      }
  }

}




/* Imposta le socket e le connessioni dei client. */
void ConnectTwoClients (int socketDes, arg * argument) {

  int playerTurn = 0;
  int recycled;

  while (playerTurn < 2) {

      pthread_mutex_lock (&MUTEX_LIST);
        while (waitingList == NULL) {

            if (pthread_cond_wait (&LIST_FLAG, &MUTEX_LIST) != 0) {
                perror ("Error pthread_cond_wait");
                exit (-1);
            }

        }
        argument->clientSd[playerTurn] = waitingList->clientSd;
        if (playerTurn) {
            strcpy (argument->name2, waitingList->name);
        }
        else {
            strcpy (argument->name1, waitingList->name);
        }

        recycled = waitingList->recycled;
        deleteWaitingList ();

      pthread_mutex_unlock (&MUTEX_LIST);



      /* Invia al client il suo id se non gli è stato già associato. */
      if (! recycled) {
          writeClientInt (argument->clientSd[playerTurn], playerTurn);
      }
      else {
          if (playerTurn) {
              writeClientMsg (argument->clientSd[1], 'P');
          }
      }

      if (playerTurn == 0) {
          /* Invia un messaggio al primo client per far sapere che il server è in attesa di un secondo client. */
          writeClientMsg (argument->clientSd[0], 'A');
      }

      playerTurn++;
  }
}


/* ************************************************************************** */


/*
* Funzioni di gioco.
*/

/* Riceve una mossa dal client. */
int getPlayerMove (int clientSd) {

    /* Chiedi al giocatore di effettuare una mossa. */
    writeClientMsg (clientSd, 'T');

    /* Restituisce la mossa del giocatore. */
    return readClientInt (clientSd);
}


/* Controlla se la cella dove il client ha effettuato la mossa è vuota. */
int checkMove (char playboard[][3], int move, int playerTurn) {

    move = move -1;
    if (playboard[move/3][move%3] == ' ') { /* Mossa valida. */
        return 1;
   }
   else { /* Mossa invalida. */
       return 0;
   }
}


/* Disegna la tabella da gioco in output. */
void drawPlayboard (char playBoard[][3], int numPartita) {

  printf(" \nTabella partita %d \n", numPartita);
  printf(" _________________\n");
  printf("|     |     |     | \n");
  printf("|  %c  |  %c  |  %c  |\n",playBoard[0][0],playBoard[0][1],playBoard[0][2]);
  printf("|_____|_____|_____|\n");
  printf("|     |     |     |\n");
  printf("|  %c  |  %c  |  %c  |\n",playBoard[1][0],playBoard[1][1],playBoard[1][2]);
  printf("|_____|_____|_____|\n");
  printf("|     |     |     |\n");
  printf("|  %c  |  %c  |  %c  |\n",playBoard[2][0],playBoard[2][1],playBoard[2][2]);
  printf("|_____|_____|_____|\n");

}


/* Invia la tabella da gioco aggiornata ad entrambi i clients. */
void sendUpdate (int * clientSd, int move, int playerTurn) {

    /* Segnala un aggiornamento ad entrambi i clients */
    writeClientMsg (clientSd[0], 'U');
    writeClientMsg (clientSd[1], 'U');

    /* Invia l' ID del giocatore che ha effettuato la mossa. */
    writeClientInt (clientSd[0], playerTurn);
    writeClientInt (clientSd[1], playerTurn);

    /* Invia la mossa del giocatore. */
    writeClientInt (clientSd[0], move);
    writeClientInt (clientSd[1], move);

}


/* Controlla la tabella da gioco per determinare se ci sono vincitori. */
int checkPlayboard (char playboard[][3], int lastMove) {

    lastMove = lastMove -1;
    int row = lastMove/3;
    int col = lastMove%3;

    /* Contollo delle righe per vedere se c'è un eventuale vincitore. */
    if ( playboard[row][0] == playboard[row][1] && playboard[row][1] == playboard[row][2] ) {
        return 1;
    }
    /* Contollo delle colonne per vedere se c'è un eventuale vincitore. */
    else if ( playboard[0][col] == playboard[1][col] && playboard[1][col] == playboard[2][col] ) {
        return 1;
    }
    /* Se la mossa effettuata coincide con un numero pari, controlliamo le diagonali per vedere se c'è un eventuale vincitore */
    else if (lastMove % 2 == 0) {
        /* Controllo diagonale da sx verso dx */
        if ( (lastMove == 0 || lastMove == 4 || lastMove == 8) && (playboard[0][0] == playboard[1][1] && playboard[1][1] == playboard[2][2]) ) {
            return 1;
        }
        /* Controllo diagonale da dx verso sx */
        if ( (lastMove == 2 || lastMove == 4 || lastMove == 6) && (playboard[1][1] == playboard[0][2] && playboard[1][1] == playboard[2][0]) ) {
            return 1;
        }
    }

    /* Non c'è ancora nessun vincitore. */
    return 0;
}


/* Avvia una partita tra i due client. */
void * runGame (void * structArg) {

    arg * argument = (arg *) structArg;
    int * clientSd = (int *) argument->clientSd;           /* Client sockets. */
    char * name1 = (char *) argument->name1;               /* Nome giocatore 1*/
    char * name2 = (char *) argument->name2;               /* Nome giocatore 2*/

    char playboard[3][3] = { {' ', ' ', ' '},      /* Tabella da gioco. */
                             {' ', ' ', ' '},
                             {' ', ' ', ' '}
                           };

    pthread_mutex_lock (&MUTEX_COUNT);
      int numPartita = COUNTPLAYER/2 ;
    pthread_mutex_unlock (&MUTEX_COUNT);

    printf ("\nIl numero delle partite attive è: %d \n", numPartita);

    /* Invia il messaggio di inizio ad entrambi i clients.*/
    writeClientMsg (clientSd[0], 'S');
    writeClientMsg (clientSd[1], 'S');

    drawPlayboard (playboard, numPartita);

    int playerTurn = 0;
    int gameOver = 0;
    int countTurn = 0;

    int valid;
    int move;
    int choiceForTimeOut;

    while (!gameOver) {

        valid = 0;

        /* Chiede all'avversario di aspettare la mossa del client che sta giocando. */
        writeClientMsg (clientSd[(playerTurn + 1) % 2], 'W');

        /*Abbiamo bisogno di continuare a chiedere una mossa finchè il giocatore non inserisce una mossa valida. */
        while (!valid) {

            move = getPlayerMove (clientSd[playerTurn]);

            /* Errore di lettura della socket del client (-1), oppure client non risponde(-3). */
            if (move == -1 || move == -3) {
                break;
            }

            printf ("Il giocatore %s ha scelto la posizione %d\n", playerTurn ? name2 : name1 , move);

            /* se il client sceglie una cella già occupata la funzione torna 0. */
            valid = checkMove (playboard, move, playerTurn);

            /* Mossa invalida. */
            if (!valid) {
                printf("%s ha effettuato una mossa non valida.\n", playerTurn ? name2 : name1);
                writeClientMsg (clientSd[playerTurn], 'I');
            }
        }

        /* Errore di lettura del client, si avverte dunque l'altro client che il suo avversario si è disconnesso. */
        if (move == -1) {
            printf("\n%s si è disconnesso.\n", playerTurn ? name2 : name1);
            if (playerTurn) {
                writeClientMsg (clientSd[0], 'E');
            }
            else {
                writeClientMsg (clientSd[1], 'E');
            }

            /* Si esce dal while perchè questa partita è terminata a causa della disconnessione di un client. */
            break;
        }
        else if (move == -3) {
            printf("\n%s non risponde da 90 secondi, verrà dunque disconnesso..\n", playerTurn ? name2 : name1);

            /* choiceForTimeOut tiene traccia della volontà dell'altro client di giocare un'altra partita o uscire. */
            choiceForTimeOut = readClientInt (clientSd[(playerTurn + 1) % 2]);
            if (choiceForTimeOut) {
                printf("\n%s Ha deciso di giocare una nuova partita perchè l'avversario non risponde.\n", playerTurn ? name1 : name2);
            }
            break;
        }
        else {
              /* Aggiorna la tabella da gioco e invia l'aggiornamento. */
              move = move -1;
              playboard[move/3][move%3] = playerTurn ? 'X' : 'O';
              sendUpdate (clientSd, move + 1, playerTurn);

              /* Ridisegna la tabella da gioco. */
              drawPlayboard (playboard, numPartita);


              /* Controlla chi ha vinto/perso. il controllo avviene solo dalla 5 mossa in poi che sarebbe il numero minimo di giocate per poter vincere.*/
              if (countTurn >= 4) {
                  gameOver = checkPlayboard (playboard, move + 1);
              }

              /* Abbiamo un vincitore. */
              if (gameOver == 1) {
                  writeClientMsg (clientSd[playerTurn], 'V');
                  writeClientMsg (clientSd[(playerTurn + 1) % 2], 'L');
                  printf ("\nLa partita numero %d è stata vinta dal giocatore %s \n\n", numPartita, playerTurn ? name2 : name1);
              }
              /* Ci sono state nove mosse valide e nessun vincitore, la partita è terminata in pareggio. */
              else if (countTurn == 8) {
                  printf ("\nLa partita numero %d  tra %s e %s è terminata con un pareggio.\n\n", numPartita, name2, name1);
                  writeClientMsg (clientSd[0], 'D');
                  writeClientMsg (clientSd[1], 'D');
                  gameOver = 1;
              }

              playerTurn = (playerTurn + 1) % 2;
              countTurn++;
          }
      }

      int choiceForReplay0;
      int choiceForReplay1;

      if (move != -3) {
          /* In choiceForReplay0 ci sarà 1 se il client vuole giocare ancora, 0 altrimenti (-1 in caso di disconnessione anomala). */
          choiceForReplay0 = readClientInt (clientSd[0]);
      }
      else {
          if(!playerTurn) {
            choiceForReplay0 = 0;
            choiceForReplay1 = choiceForTimeOut;
          }
          else {
            choiceForReplay0 = choiceForTimeOut;
            choiceForReplay1 = 0;
          }
      }

      /* Se il client vuole giocare un'altra partita bisogna salavre il suo Socketdes altrimenti si chiude la socket. */
      if (choiceForReplay0 == 0 || choiceForReplay0 == -1) {

          /* Chiude la socket del client e decrementa il numero dei giocatori. */
          if (close (clientSd[0]) != 0) {
              perror ("\nERROR. Chiusura socket\n");
              exit (-1);
          }

          free (name1);

          pthread_mutex_lock (&MUTEX_COUNT);
            COUNTPLAYER --;
            if (pthread_cond_broadcast (&COUNTPLAYER_FLAG) != 0) {
                perror ("Error pthread_cond_broadcast");
                exit (-1);
            }
          pthread_mutex_unlock (&MUTEX_COUNT);

      }
      else {
        pthread_mutex_lock (&MUTEX_LIST);
          insertWaitingList (name1, clientSd[0], 1);
          if (pthread_cond_broadcast (&LIST_FLAG) != 0) {
              perror ("Error pthread_cond_broadcast");
              exit (-1);
          }
        pthread_mutex_unlock (&MUTEX_LIST);
      }

      if (move != -3) {
          choiceForReplay1 = readClientInt (clientSd[1]);
      }

      if (choiceForReplay1 == 0 || choiceForReplay1 == -1) {

          if (close (clientSd[1]) != 0) {
              perror ("ERROR. Chiusura socket");
              exit (-1);
          }

          free (name2);

          pthread_mutex_lock (&MUTEX_COUNT);
            COUNTPLAYER --;
            if (pthread_cond_broadcast (&COUNTPLAYER_FLAG) != 0) {
                perror ("Error pthread_cond_broadcast");
                exit (-1);
            }
          pthread_mutex_unlock (&MUTEX_COUNT);

      }
      else {
          pthread_mutex_lock (&MUTEX_LIST);
            insertWaitingList (name2, clientSd[1], 1);
            if (pthread_cond_broadcast (&LIST_FLAG) != 0) {
                perror ("Error pthread_cond_broadcast");
                exit (-1);
            }
          pthread_mutex_unlock (&MUTEX_LIST);
      }

      free (argument);

      pthread_exit (NULL);
  }


/* ************************************************************************** */


/*
*  Main.
*/

int main (int argc, char *argv[]) {

    char * p = NULL;                /* Variabile usata per controllare se la funzione strtol viene eseguita correttamente  */
    unsigned short serverPort;
    int servSd;

    /* Test per la correttezza sul numero di argomenti passati in input al programma */
    if (argc != 2) {
        printf ("Usage:  %s <Server Port> \n", argv[0]);
        exit(-1);
    }

    /* In caso di errore salva l'ultimo carattere dove è fallita la converisone. */
    serverPort = (unsigned short) strtol (argv[1], &p, 10);
    /* Converisone fallita: o non ha fatto niente o non è arrivato all'ultimo carattere. */
    if (p == NULL || * p != '\0') {
        perror ("strtol error. Numero di porta errato.");
        exit (-1);
    }

    servSd = setupSd (serverPort);

    printf("\nIl server è stato avviato con successo.\n");

    /* Impostazione del socket in modalità di ascolto. */
    if ((listen (servSd, MAXPENDING)) != 0) {
        perror ("Listen error");
        exit (-1);
    }

    pthread_attr_t attr;
    if (pthread_attr_init (&attr) != 0 ) {
        perror ("pthread_attr_init error");
        exit (-1);
    }

    if (pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED) != 0) {
        perror ("pthread_attr_setdetachstate error");
        exit (-1);
    }

    pthread_t threadForConn;
    /* Avvia un nuovo thread per accettare nuove connessioni. */
    if (pthread_create (&threadForConn, &attr, acceptNewConnections, (void *)&servSd)  != 0) {
        perror ("pthread create\n");
        exit (-1);
    }

    while (1) {

        /* Allocazione della struttura che tiene traccia delle informazioni dei due client che giocano. */
        arg * argument = (arg *) malloc (sizeof (arg));
        if (argument == NULL) {
            perror("\nMemoria esaurita\n");
            exit(-1);
        }

        argument->name1 = (char *) malloc (20 * sizeof(char));
        if (argument->name1 == NULL) {
            perror("\nMemoria esaurita\n");
            exit(-1);
        }

        argument->name2 = (char *) malloc (20 * sizeof(char));
        if (argument->name1 == NULL) {
            perror("\nMemoria esaurita\n");
            exit(-1);
        }


        ConnectTwoClients (servSd, argument);

        pthread_t thread;
        /* Avvia un nuovo thread per questa partita. */
        if (pthread_create (&thread, &attr, runGame, (void *) argument) != 0) {
            perror ("pthread create\n");
            exit (-1);
        }
    }

    if (pthread_attr_destroy (&attr) != 0) {
        perror ("pthread attr_destroy\n");
        exit (-1);
    }

    if (close (servSd) != 0) {
        perror ("ERROR. Chiusura socket");
        exit (-1);
    }

    return 0;
}
