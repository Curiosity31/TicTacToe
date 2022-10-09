#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

/* ************************************************************************** */

/* Variabile globale usata per salvare il socket descriptor nell'eventualità che un client non immette una mossa in 90 secondi. */
int management = -1;

/* Strutture usate per rendere le system call bloccanti solo per un certo periodo di tempo. */
struct timeval timeout1 = {3, 0,};
struct timeval timeout2 = {90, 0,};

/* ************************************************************************** */


/*
* Funzione di scrittura dalla socket.
*/

/* Scrive un int nella socket del server. */
void writeToServer (int serverSd, int msg) {

    if (write (serverSd, &msg, sizeof(int)) != sizeof(int)) {
        perror("ERROR. Scrittura di un intero sulla socket del server");
        printf("\nIl server è stato arrestato o il tuo avversario si è disconnesso.\n");
        exit (-1);
    }
}


/* ************************************************************************** */


/*
* Handler.
*/

/* Gestisce il segnale alarm, impostato per far in modo che un utente non impieghi
* troppo tempo per decidere se giocare un'altra partita dopo che ne ha terminata una. */
void hand1 () {
  printf("Tempo scaduto! Arrivederci.\n" );
  exit (-1);
}


/* Gestisce il segnale alarm, impostato per far in modo che un utente non impieghi
* troppo tempo per inserire una mossa. */
void hand2 () {
  printf("\nHai sforato il limite di tempo per effettuare una mossa. La partita è termita!\n");
  writeToServer (management, -3);
  exit(-1);
}


/* ************************************************************************** */


/* Finita una partita l'utente può scegliere di giocarne un'altra senza disconnettersi. */
int anotherGame (int serverSd, ushort twoPlayer) {

  int choice;
  signal (SIGALRM, hand1);

  /* In caso di fine partita ci sono due player che devono decidere se rigiocare
  * quindi gli viene assegnato un tempo limitato per scegliere, qualora invece
  * un player si disconnetta non c'è bisogno di mettere fretta all'altro per decidere. */
  if (twoPlayer) {
      printf("\n\nHai 15 secondi di tempo per effettuare una scelta");
      alarm(15);
  }

  while (1) {
      printf("\n-Premere 1 per avviare una nuova partita");
      printf("\n-Premere 0 per terminare:");
      printf("\n-Scelta:   ");
      if (scanf("%d", &choice) != 1) {
          while (getchar() != '\n'); /* "pulisce" il buffer nel caso sia stata inserita una stringa. */
      }

      if (choice != 1 && choice != 0) {
          printf("\nScelta errata, riprova");
      }
      else {
          if (choice == 1) {
              if (twoPlayer) {
                  alarm(0);
              }
              printf ("\nIn attesa di un secondo giocatore...\n\n");
          }
          else{
              printf ("\nArrivederci.\n\n");
          }

          writeToServer (serverSd, choice);

          return choice;
      }
  }
}


/* ************************************************************************** */


/*
* Funziondi di lettura dalla socket.
*/

/* Legge un messaggio dalla socket del server. */
char readServerMsg (int serverSd, ushort flag, ushort firstTimeRecycled) {

    char msg = ' ';
    int ret;

    while (1) {

        if (read (serverSd, &msg, sizeof(char)) != sizeof(char)) {
            if (errno == EWOULDBLOCK) {
                /* flag = 1 quando si entra in questa funzione a partita inoltrata, firstTimeRecycled per gestire i "riciclati"  se = 0, non è la prima volta (della partita) che chiama questa funzione. */
                if (flag == 1 && firstTimeRecycled == 0) { 
                    printf("\nIl tuo avversario non risponde da 90 secondi\n");
                    ret = anotherGame (serverSd, 0);
                    if (ret == 0) {
                        exit (-1);
                    }
                }
                /* flag = 0 quando si entra in questa funzione per la prima volta. */
                else {
                    printf("\nAttendere pochi istanti affinchè un altro player si colleghi.\n");
                }
            }

            /* Se il segnale di interruzione del timeout arriva mentre il processo effettua la read
            *  la chiamata di sistema viene interrotta restituendo un errore (errno viene impostato su EINTR). */
            else if (errno == EINTR) {
                if (setsockopt (serverSd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout2, sizeof(timeout2)) < 0) {
                    perror("setsockopt failed\n");
                    exit (-1);
                }
            }
            else {
                perror("ERROR. Lettura dalla socket del server.");
                printf("\nIl server è stato arrestato.\n");
                exit (-1);
            }
        }
        else {
            /* Se legge correttamente bisogna uscire dal while. */
            break;
        }
    }

    return msg;
}


/* Legge un int (mossa) dalla socket del server. */
int readServerMove (int serverSd) {

    int msg = 0;

    if (read (serverSd, &msg, sizeof(int)) != sizeof(int)) {
          perror("ERROR. Lettura dalla socket del server.");
          printf("\nIl server è stato arrestato.\n");
          exit (-1);
    }

    return msg;
}


/* Legge l'id assegnato al client dalla socket del server. */
int readServerID (int serverSd) {

    int msg = 0;
    int flag = 1;

    while (1) {
        if (read (serverSd, &msg, sizeof(int)) != sizeof(int)) {
            if (errno == EWOULDBLOCK) {

                printf("\nAttualmente sono avviate il numero massimo di partite\n");
                printf("Attendere pochi istanti affinchè un'altra partita termini.\n");

                /* Imposta un nuovo timeout che servirà per le successive read. */
                if (setsockopt (serverSd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout2, sizeof(timeout2)) < 0) {
                    perror("setsockopt failed\n");
                    exit (-1);
                }
                flag = 0;
            }
            else {
                perror("ERROR. Lettura dalla socket del server.");
                printf("\nIl server è stato arrestato.\n");
                exit (-1);
            }
        }
        else {
            /* Se legge correttamente bisogna uscire dal while. */
            break;
        }
    }

    if (flag) {
        if (setsockopt (serverSd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout2, sizeof(timeout2)) < 0) {
            perror("setsockopt failed\n");
            exit (-1);
        }
    }

    return msg;
}


/* ************************************************************************** */

/*
* Funzione per stabilire la connessione.
*/

int connectToServer (char * serverIP, unsigned short serverPort) {

    struct sockaddr_in ServerAddr;
    int socketDes;

    /* Creazione della socket */
    if ((socketDes = socket (PF_INET, SOCK_STREAM, 0)) < 0) {
        perror ("Socket error");
        exit (-1);
    }

    /* Costruzione della struttura */
    ServerAddr.sin_family = AF_INET;
    ServerAddr.sin_port = htons (serverPort);
    if (inet_aton (serverIP , &ServerAddr.sin_addr) == 0) {
        perror ("Inet_aton");
        exit (-1);
    }
    /* Riempimento del campo sin_zero della struttura. */
    memset (&(ServerAddr.sin_zero), '\0', sizeof (ServerAddr.sin_zero));


    /* Inizialmente il timeout è impostato a tre secondi in modo tale da capire se il server
    * sta gestendo il numero massimo di connessioni (in secondo momento verrà impostato a 90 secondi). */
    if (setsockopt (socketDes, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout1, sizeof(timeout1)) < 0) {
        perror("setsockopt failed\n");
        exit (-1);
    }


    /* Connessione al server */
    if ((connect (socketDes, (struct sockaddr *) &ServerAddr, sizeof (ServerAddr))) < 0) {
        perror ("Connect error");
        exit (-1);
    }

    return socketDes;
}


/* ************************************************************************** */

/*
* Funzioni di gioco.
*/

/* Disegna la tabella di gioco sullo stdout. */
void drawPlayBoard (char playBoard[][3]) {

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


/* Assegna il turno del giocatore e lo invia al server. */
void takeTurn (int serverSd, char * name) {

    int move;
    signal (SIGALRM, hand2);

    /* Viene salvato in via precauzionale la socket per comunicare con il server
    * nel caso in cui il client non risponde in tempo, verrà inviato un messaggio al server per disconnetterlo. */
    management = serverSd;

    while (1) { /*Chiedere fintanto che non si riceve una mossa valida */

        printf ("\n %s effettua la tua mossa scegliendo la posizione come indicato nella tabella di riferimento:   ", name);
        /* Il client ha 90 secondi di tempo per effettuare una mossa. */
        alarm(90);

        if (scanf ("%d", &move) != 1) {
            while (getchar() != '\n'); /* pulisce il buffer nel caso sia stata inserita una stringa. */
        }

        /* Se la mossa è stata effettuta in tempo, il segnale viene disattivato. */
        alarm(0);

        if (move <= 9 && move >= 1) {
            /* Invia la mossa del giocatore al server. */
            writeToServer (serverSd, move);
            break;
        }
        else {
            printf("\n%s hai inserito una mossa non valida. Riprova.\n", name);
        }
    }
}


/* Ottieni l'aggiornamento dal server. */
void updatePlayBoard (int serverSd, char playBoard[][3]) {

    int playerId = readServerID (serverSd);
    int move = readServerMove (serverSd);

    /* Aggiorna la tabella di gioco. */
    move = move - 1;
    playBoard[move/3][move%3] = playerId ? 'X' : 'O';
}


/* Funzione principale di gioco. */
void play (char * serverIP, char * port) {

    unsigned short serverPort;
    char * p = NULL;                   /* Variabile per controllare se la strtol viene eseguita correttamente  */

    /* In caso di errore salva l'ultimo carattere dove è fallita la converisone. */
    serverPort = (unsigned short) strtol (port, &p, 10);
    /* Converisone fallita: o non ha fatto niente o non è arrivato all'ultimo carattere. */
    if (p == NULL || *p != '\0') {
        perror ("strtol error, hai inserito un numero di porta errato.");
        exit (-1);
    }

    char * name = (char *) malloc (20 * sizeof(char));
    printf("Inserisci il tuo nome per giocare: \t");
    scanf("%s", name);

    /* Connessione al server. */
    int serverSd = connectToServer (serverIP, serverPort);

    /* Invio del nome del client al server. */
    if (write (serverSd, name, strlen(name))!= strlen(name)) {
        perror("ERROR. Scrittura di una stringa sulla socket del server");
        printf("\nIl server è stato arrestato o il tuo avversario si è disconnesso.\n");
        exit (-1);
    }

    /* L' ID è la prima cosa che si riceve dopo la connessione. */
    int playerId = readServerID(serverSd);

    char msg;
    char playBoard[3][3] = { {' ', ' ', ' '},
                             {' ', ' ', ' '},
                             {' ', ' ', ' '}
                           };

/*
* A -> Attesa secondo giocare //Nel caso di utente "riciclato" invia playerId=0 .
* T -> Turno del giocatore.
* S -> Inizio partita. //Nel caso di utente "riciclato" pulisce tabella e inizializza la partita.
* I -> Mossa invalida.
* U -> Aggiornamento tabella di giocatore.
* W -> Attesa della mossa dell'avversario.
* V -> Vincitore.
* L -> Perdente.
* D -> Pareggio.
* E -> EXIT (un utente ha deciso di disconnettersi o si disconnette in modo anomalo).
* P -> Invia playerID = 1 nel caso sia un utente "riciclato" .
*/

    /* In attesa che il gioco abbia inizio. */
    do {
        msg = readServerMsg (serverSd, 0, 0);

        if (msg == 'A') {
            printf ("\nIn attesa di un secondo giocatore...\n\n");
        }
    } while ( msg != 'S' );


    /* Il gioco è iniziato. */
    printf ("\nBenvenuto %s\n", name);
    printf ("Che la partita abbia inizio!\n\n");
    printf ("%s tu sei %c\n", name, playerId ? 'X' : 'O');

    /* Tabella di riferimento. */
    char referencePlayBoard [3][3] = {
                                {'1','2','3'},
                                {'4','5','6'},
                                {'7','8','9'}
                              };

    printf("\nTabella di riferimento\n");
    drawPlayBoard (referencePlayBoard);
    drawPlayBoard (playBoard);

    ushort flag = 1, firstTimeRecycled = 0;
    while (flag == 1) {

        msg = readServerMsg (serverSd, 1, firstTimeRecycled);
        firstTimeRecycled = 0;

        switch (msg) {
            case 'T' :  {
                takeTurn (serverSd, name);
                break;
            }
            case 'P' :  { /* Qui ci si va solo per un utente "riciclato". */
                playerId = 1;
                break;
            }
            case 'A' :  { /* Qui ci si va solo per un utente "riciclato". */
                firstTimeRecycled ++ ;
                playerId = 0;
                break;
            }
            case 'S' :  { /* Qui ci si va solo per un utente "riciclato". */
                printf ("\nBenvenuto %s\n", name);
                printf ("Che la partita abbia inizio!\n\n");
                printf ("%s tu sei %c\n", name, playerId ? 'X' : 'O');
                printf("\n\nTabella di riferimento\n");
                memset (playBoard, ' ', sizeof(playBoard));
                drawPlayBoard (referencePlayBoard);
                drawPlayBoard (playBoard);
                break;
            }
            case 'E' :  {
                printf("\nIl tuo avversario ha deciso di abbandonare la partita\n");
                flag = anotherGame(serverSd, 0);
                break;
            }
            case 'I': {
                printf("\n%s la cella scelta è gia occupata. Inserire una cella libera.\n", name);
                break;
            }
            case 'U' : {
                updatePlayBoard (serverSd, playBoard);
                printf("\nTabella di riferimento\n");
                drawPlayBoard (referencePlayBoard);
                drawPlayBoard (playBoard);
                break;
            }
            case 'W' : {
                printf("\nIn attesa della mossa dell'avversario...\n");
                break;
            }
            case 'V' : {
                printf("\nCongratulazioni %s, hai vinto!\n", name);
                printf ("La partita è terminata.\n");
                flag = anotherGame (serverSd, 1);
                break;
            }
            case 'L' : {
                printf("\n%s hai perso.\n", name);
                printf ("La partita è terminata.\n");
                flag = anotherGame (serverSd, 1);
                break;
            }
            case 'D' : {
                printf("\n%s la partita è terminata con un pareggio.\n", name);
                flag = anotherGame (serverSd, 1);
                break;
            }
            default : {
                perror ("ERROR. Ricevuto un messaggio inaspettato");
                exit (-1);
            }
        }
    }

    /* Chiude la socket e termina*/
    if (close (serverSd) != 0) {
      perror ("ERROR. Chiusura socket");
      exit (-1);
    }

    free (name);
}


/* ************************************************************************** */

int menu(void) {

	int choice;
  printf("__________________________________________________________________\n");
	printf("| ////// //////  //////             //////    //\\\\     //////     |\n");
	printf("|   ||     ||    ||                   ||     //  \\\\    ||         |\n");
  printf("|   ||     ||    ||                   ||    //xxxx\\\\   ||         |\n");
	printf("|   ||     ||    ||                   ||   //      \\\\  ||         |\n");
	printf("|   ||   //////  //////               ||  //        \\\\ //////     |\n");
  printf("|                                                                 |\n");
  printf("|                                                                 |\n");
	printf("|                   ////// /////// //////                         |\n");
	printf("|                     ||   ||   || ||                             |\n");
  printf("|                     ||   ||   || /////                          |\n");
	printf("|                     ||   ||   || ||                             |\n");
	printf("|                     ||   /////// //////                         |\n");
	printf("|____________________________________ ____________________________|\n");
	printf("\n			    Menu' 				  \n ");

	printf("Inserire: \n");
	printf("  1 per giocare.\n");
  printf("  2 per avere informazioni sul gioco.\n");
	printf("  3 per uscire dal gioco.\n");
  printf("  Scelta: \t");

	if (scanf("%d", &choice) != 1) {
      while (getchar() != '\n'); /* pulisce il buffer nel caso sia stata inserita una stringa. */
  }

	return choice;
}

void info() {
  printf ("\nChe cos’è il 'Tic Tac Toe' ?\n");
  printf ("Un popolarissimo gioco di strategia meglio conosciuto in Italia con il nome di 'Tris'.\n");
  printf ("Si gioca utilizzando come campo di gioco una matrice quadrata 3 x 3 .\n");
  printf ("Come funziona ?\n");
  printf ("A turno, due giocatori si sfidano inserendo in una cella vuota il proprio simbolo (di solito una “X” o un “O”). \n");
  printf ("Vince chi dei due riesce per primo a disporre tre dei propri simboli in linea retta orizzontale , verticale o diagonale.\n");
  printf  ("Se la griglia viene riempita senza che nessuno dei giocatori sia riuscito a completare una linea retta di tre simboli, il gioco finisce in parità.\n");
}


/* ************************************************************************** */

/*
* Funzione principale
*/

int main (int argc, char *argv[]) {

  if (argc != 3) {    /* Test per la correttezza del numero di  argomenti */
      printf ("Usage:  %s <IP> <Server Port>\n", argv[0]);
      exit(1);
  }

  while(1) {
      switch (menu()) {
  		    case 1: {
  		        printf ("\nHai scelto di giocare.\n\n");
  		        play (argv[1], argv[2]);
  		        return 0;
  		    }
  		    case 2: {
  		        printf("\nHai scelto di avere informazioni sul gioco.\n\n");
  		        info();
  		        break;
  		    }
      		case 3: {
          		printf ("\nHai scelto di uscire.\n\n");
          		exit(0);
      		}
      		default: {
          		printf ("\n\nScelta errata, riprova.\n\n");
          		break;
      		}
      }
	}
  return 0;
}
