/*
 * Systemprogrammierung
 * Multiplayer-Quiz
 *
 * Server
 *
 * login.h: Header für das Login
 */
#ifndef LOGIN_H
#define LOGIN_H

int serverSocketFileDescriptor;

int startLoginThread(int *port);

void enableLogin();

void disableLogin();

#endif
