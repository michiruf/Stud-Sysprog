/*
 * Systemprogrammierung
 * Multiplayer-Quiz
 *
 * Server
 *
 * score.c: Implementierung des Score-Agents
 *
 * Implementieren Sie in diesem Modul den Score-Agent. Vermeiden Sie
 * Code-Duplikation, indem Sie auch hier Funktionen aus den Modulen user
 * und rfc wiederverwenden.
 * Achten Sie in diesem Modul besonders darauf, den Semaphor zum Triggern
 * des Score-Agents sauber wegzukapseln. Der Semaphor darf nur modul- und
 * nicht programmglobal sein.
 */

#include <semaphore.h>
#include "score.h"
#include "user.h"
#include "../common/util.h"
#include <pthread.h>
#include "rfc.h"

static sem_t trigger;        // Zugriff nur über Funktionen dieses Moduls!
pthread_t scoreThreadID = 0;

int initSemaphore() {
    return sem_init(&trigger, 0, 0);
}

//increments (unlocks) Semaphore
int incrementSemaphore(){
    return sem_post(&trigger);
}

//starts a ScoreAgentThread
int startScoreAgentThread() {

    if (initSemaphore() >= 0) {
        int err;
        err = pthread_create(&scoreThreadID, NULL, (void *) &startScoreAgent, NULL);
        if (err == 0) {
            infoPrint("Score agent thread created successfully");
            return 1;
        } else {
            errorPrint("Error: Can't create Score agent thread");
            return err;
        }

    } else {
        errorPrint("Error: Semaphore could not be created/initialized");
        return -1;
    }
}

void startScoreAgent() {
    infoPrint("Starting ScoreAgent...");

    while (1) {

        //Waits until semaphor is incremented/unlocked and decrements (locks) it again
        sem_wait(&trigger);
        updateRanking();
        //SendPlayerListMSG
        //MESSAGE sendmessage = buildLoginResponseOk(message.body.loginRequest.rfcVersion, MAXUSERS,(__uint8_t) getClientIDforUser(username));
    /*
        MESSAGE sendmessage = buildPlayerList(, getUserAmount());
        if (sendMessage(client_sock, &sendmessage) >= 0) {

        }*/

    }



}


