/*
 * Systemprogrammierung
 * Multiplayer-Quiz
 *
 * Server
 *
 * login.c: Implementierung des Logins
 *
 * Dieses Modul beinhaltet die Funktionalität des Login-Threads. Das heißt,
 * hier wird die Schleife zum Entgegennehmen von Verbindungen mittels accept(2)
 * implementiert.
 * Benutzen Sie für die Verwaltung der bereits angemeldeten Clients und zum
 * Eintragen neuer Clients die von Ihnen entwickelten Funktionen aus dem Modul
 * user.
 */

#include "login.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "vardefine.h"
#include "user.h"
#include "../common/util.h"
#include <pthread.h>

pthread_t loginThreadID = 0;

//starts a loginThread
int startLoginThread(int *port){
    int err;
    err=pthread_create(&loginThreadID,NULL,(void * ) &startLoginListener,(void * ) port);
    if(err == 0){
        infoPrint("Login thread created successfully");
    }else{
        errorPrint("Can't create Login thread");
    }
    return err;
}

//TODO gameMode implementieren
//return -1 => VorbereitungsPhase
//return 1 => Spiel läuft
//wenn StartGame (STG) c=>s dann auf Spiel läuft wechseln
int getGameMode(){
    return -1;
}

//Rückgabewert -1 bei Fehler
int startLoginListener(int *port){

    infoPrint("Starting login listener...");

    const size_t buf_size = MAXDATASIZE;
    int client_sock;

    //Socket create type AF_INET IPv4, TCP
    const int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sock < 0){
        errorPrint("Could not ceate socket");
        return -1;
    }

    //specify IP-address and listening port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(*port);

    //Socket bind to local IP and port
    if(bind(listen_sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0 ){
        errorPrint("Could not bind socket to address");
        return -1;
    }

    //Listen to Connections, MAXCONNECTIONS 4
    if(listen(listen_sock, MAXCONNECTIONS) < 0){
        errorPrint("Could not accept client connection");
        return -1;
    }else{
        infoPrint("Bind socket to local IP on Port: %d, and listening...",*port);
    }

    while(1){
        //waits for client connection
        //accept() - blockiert und wartet bis eine Verbindung vom Client aufgebaut wird
        //client_sock beinhaltet den Socket-Deskriptor des Clients
        client_sock = accept(listen_sock, NULL, NULL);
        if(client_sock < 0){
            errorPrint("Could not accept client connection");
            return -1;
        }

        if(getUserAmount()<MAXUSERS && getGameMode() < 0){

            debugPrint("here we are");

            //TODO daten buffer uint8 empfangen und auf LRQ-Nachricht auswerten.
            //falls LRQ Username extrahieren ggf. nullbyte hizufugen und addUser(username, client-socketID)
            //Client-thread erzeugen
            //LOK-Nachricht senden


        }else{
            errorPrint("Error: Game running or maximum user amount reached, please try again later...");
        }

        //2-Do auf RFC-Meldung LoginRequest auswerten, und User in Tabelle Speichern
        //now receive data and send it back, until the connection closes
        /*uint8_t bytes_read;
        char buf[buf_size];
        while((bytes_read = read(client_sock, buf, buf_size)) > 0){

            write(STDOUT_FILENO,buf, (size_t)bytes_read);
            write(client_sock, buf, (size_t)bytes_read);
        }

        if(bytes_read < 0){
            perror("Error receiving data");
            return 1;
        }*/
    }

    return 0;
}