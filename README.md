Descrizione sintetica:
Sistema client-server che consente ai client di giocare a tris contro altri giocatori.

Descrizione dettagliata:
Quando un nuovo client si collega al server, viene messo in attesa finché non è disponibile un altro client per
iniziare una partita. A quel punto, i due client iniziano la partita, comunicando le loro mosse al server.
Il server è in grado di gestire un numero arbitrario di partite simultanee (ogni partita coinvolge
due client e ogni client può essere coinvolto in una sola partita alla volta). Al termine di una partita, i due
client coinvolti tornano disponibili e possono venire accoppiati con altri client disponibili, se ce ne sono in quel
momento, per poi cominciare una nuova partita.

Regole generali: 
Il server è realizzato in linguaggio C su piattaforma UNIX/Linux. Utilizza le system call UNIX,
è di tipo concorrente, ed è in grado di gestire un numero arbitrario di client contemporaneamente. 
Il server effettua il log delle principali operazioni (nuove connessioni, sconnessioni, richieste da parte dei client) su standard output.
Client e server comunicano tramite socket TCP.
