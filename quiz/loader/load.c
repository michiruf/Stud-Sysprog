/**
 * \file	loader/load.c
 * \author	Stefan Gast
 *
 * \brief	Implementierung für das Laden eines Fragekataloges in den Shared Memory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "common/util.h"
#include "common/question.h"
#include "common/server_loader_protocol.h"
#include "load.h"
#include "util.h"
#include "parser.h"


/**
 * \brief	Ein Array durchmischen
 *
 * Mischt das übergebene Array
 */
static void mix(unsigned int *array,		/**< Das zu mischende Array */
		unsigned int size		/**< Die Größe des Arrays */
	       )
{
	unsigned int passesLeft = size*2U;
	unsigned int idx1, idx2;
	unsigned int tmp;

	while(passesLeft--)
	{
		idx1 = (unsigned int)rand() % size;
		idx2 = (unsigned int)rand() % size;
		if(idx1 != idx2)
		{
			tmp = array[idx1];
			array[idx1] = array[idx2];
			array[idx2] = tmp;
		}
	}
}


/**
 * \brief	Eine zufällig durchmischte Folge fortlaufender Zahlen erzeugen
 *
 * Erzeugt eine zufällig durchmischte Folge fortlaufender Zahlen auf dem Heap. Das Ergebnis muss
 * nach Verwendung mit free freigegeben werden.
 */
static unsigned int *createMixedSequence(unsigned int start,		/**< Der Startwert der Folge */
					 unsigned int end		/**< Der Endwert der Folge */
					)
{
	const unsigned int size = end-start+1;
	unsigned int *array = malloc(sizeof(unsigned int)*size);
	unsigned int i;

	if(array == NULL)
		return NULL;

	for(i=0; i<size; ++i)
		array[i] = i+start;

	mix(array, size);

	return array;
}


/**
 * \brief	Eine Frage in den Shared Memory schreiben
 *
 * Schreibt eine Frage in den Shared Memory und mischt dabei die Antwortmöglichkeiten.
 */
static void writeQuestionToShmem(Question *shmemPos,		/**< Die Position im Shared Memory, an die geschrieben werden soll */
		                 const Question *buffer		/**< Die zu schriebende Frage */
				)
{
	unsigned int answerOrder[NUM_ANSWERS];
	unsigned int i;

	/* Array mit durchmischten Indizes erzeugen */
	for(i=0; i<NUM_ANSWERS; ++i)
		answerOrder[i] = i;
	mix(answerOrder, (unsigned int)NUM_ANSWERS);

	/* Frage und Timeout kopieren */
	strcpy(shmemPos->question, buffer->question);
	shmemPos->timeout = buffer->timeout;

	/* Antworten in zufälliger Reihenfolge kopieren, dabei Bitmaske der
	 * richtigen Antworten anpassen */
	shmemPos->correct = (uint8_t)0U;
	for(i=0; i<NUM_ANSWERS; ++i)
	{
		strcpy(shmemPos->answers[answerOrder[i]], buffer->answers[i]);
		if((buffer->correct & (1U << i)) != 0)
			shmemPos->correct |= (1U << answerOrder[i]);
	}
}


/**
 * \brief	Die Fragen auf einem Stack in den Shared Memory verschieben
 *
 * Verschiebt Fragen von einem Stack in den von Server und Logger gemeinsam benutzten Speicherbereich
 * und mischt sie dabei durch. Auch die Antwortmöglichkeiten für die einzelnen Fragen werden dabei
 * durchgemischt.
 *
 * \return	0 im Fehlerfall, 1 bei Erfolg
 */
static int moveToShmem(Stack *stack	/**< Der Stack mit den Fragen */
		      )
{
	const size_t numQuestions = stackCount(stack);
	const size_t shmemSize = numQuestions * sizeof(Question);
	unsigned int *order = createMixedSequence(0, numQuestions-1);

	Question currentQuestion;

	Question *shmem;
	int shmHandle;
	size_t i;

	if(order == NULL)
		return 0;

	/* Shared Memory erzeugen, Größe setzen und in Adressraum einbinden */
	shmHandle = shm_open(SHMEM_NAME, O_RDWR | O_CREAT | O_EXCL, 0600);
	if(shmHandle == -1)
	{
		switch(errno)
		{
			case EEXIST:
				errorPrint("Shared Memory existiert bereits! Der Server muss ihn löschen, bevor er ein neues Quiz anfordert.");
				break;
			default:
				errorPrint("Kann Shared Memory nicht anlegen: %s", strerror(errno));
				break;
		}
		free(order);
		return 0;
	}
	if(ftruncate(shmHandle, (off_t)shmemSize) == -1)
	{
		errorPrint("Kann Größe des Shared Memory nicht setzen: %s", strerror(errno));
		free(order);
		close(shmHandle);
		shm_unlink(SHMEM_NAME);
		return 0;
	}
	shmem = mmap(NULL, shmemSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmHandle, 0);
	if(shmem == MAP_FAILED)
	{
		errorPrint("Kann Shared Memory nicht in Adressraum einbinden: %s", strerror(errno));
		free(order);
		close(shmHandle);
		shm_unlink(SHMEM_NAME);
		return 0;
	}

	/* Alle Fragen in Shared Memory verschieben */
	for(i=0; i<numQuestions; ++i)
	{
		if(!stackPop(stack, &currentQuestion))
		{
			errorPrint("Interner Fehler: Fragestack leer!");
			free(order);
			munmap(shmem, shmemSize);
			close(shmHandle);
			shm_unlink(SHMEM_NAME);
			return 0;
		}

		writeQuestionToShmem(shmem + order[i], &currentQuestion);
	}

	free(order);

	/* Shared Memory aus Adressraum entfernen und schließen */
	munmap(shmem, shmemSize);
	close(shmHandle);

	return 1;
}


/**
 * \brief	Shared Memory löschen
 *
 * Das ist die Cleanup-Funktion für den Fall, dass der Fragekatalog erfolgreich
 * geladen werden konnte, aber die Benachrichtigung dafür nicht auf die
 * Standardausgabe geschrieben werden konnte. In diesem Fall löschen wir das
 * Shared Memory Objekt. Der Fragestack muss nicht geleert werden, da bereits
 * alle Fragen in den Shared Memory verschoben wurden.
 */
static void shmCleanup(void)
{
	shm_unlink(SHMEM_NAME);
}


/**
 * \brief	Einen Fragekatalog in den Shared Memory laden
 *
 * Lädt den übergebenen Fragekatalog aus dem angegebenen Verzeichnis in den
 * Shared Memory.
 */
void load(const char *cataloges_dir,	/**< Verzeichnis mit den Fragekatalogen */
	  const char *catalog		/**< Dateiname des Fragekatalogs */
	 )
{
	static const char oomMsg[] = LOAD_ERROR_PREFIX LOAD_ERROR_OOM "\n";
	static const char cannotOpenMsg[] = LOAD_ERROR_PREFIX LOAD_ERROR_CANNOT_OPEN "\n";
	static const char cannotReadMsg[] = LOAD_ERROR_PREFIX LOAD_ERROR_CANNOT_READ "\n";
	static const char invalidMsg[] = LOAD_ERROR_PREFIX LOAD_ERROR_INVALID "\n";
	static const char shmemMsg[] = LOAD_ERROR_PREFIX LOAD_ERROR_SHMEM "\n";
	char successMsgBuffer[] = LOAD_SUCCESS_PREFIX "1234567890\n";

	Stack questionStack;
	size_t stackSize;

	ParserResult parserResult;
	char *path;
	size_t i;

	/* Leere Dateinamen abweisen */
	if(catalog[0] == '\0')
	{
		errorPrint("LOAD-Kommando ohne Dateiname!");
		write2stdout(cannotOpenMsg, sizeof(cannotOpenMsg)-1, NULL);
		return;
	}

	/* Dateien, deren Name mit einem Punkt beginnen, gelten als versteckt. Außerdem
	 * bezeichnet ".." das Elternverzeichnis, der Loader soll aber in sein Katalogverzeichnis
	 * eingesperrt sein. Würden wir Katalognamen der Form "../verzeichnis/datei" zulassen,
	 * so hätten wir möglicherweise eine Sicherheitslücke.
	 * Verbieten wir Dateinamen, die mit einem Punkt beginnen, sind wir auf der sicheren Seite. */
	if(catalog[0] == '.')
	{
		errorPrint("Dateinamen, die mit einem Punkt beginnen sind nicht erlaubt!");
		write2stdout(cannotOpenMsg, sizeof(cannotOpenMsg)-1, NULL);
		return;
	}

	/* Keine Schrägstriche im Dateinamen erlauben, Verzeichniswechsel ist verboten */
	for(i=0; catalog[i] != '\0'; ++i)
	{
		if(catalog[i] == '/')
		{
			errorPrint("Dateiname enthält einen Schrägstrich, Verzeichniswechsel sind aber verboten!");
			write2stdout(cannotOpenMsg, sizeof(cannotOpenMsg)-1, NULL);
			return;
		}
	}

	path = malloc(strlen(cataloges_dir) + strlen(catalog) + 2);
	if(path == NULL)
	{
		errorPrint("Nicht genug Speicher beim Zusammensetzen des Pfadnamens!");
		write2stdout(oomMsg, sizeof(oomMsg)-1, NULL);
		return;
	}

	/* Pfadname zusammensetzen */
	strcpy(path, cataloges_dir);
	strcat(path, "/");
	strcat(path, catalog);

	/* Stack initialisieren */
	stackInit(&questionStack);

	parserResult = parseCatalog(path, &questionStack);
	free(path);

	switch(parserResult.status)
	{
		case PARSE_OK:
			stackSize = stackCount(&questionStack);

			if(!moveToShmem(&questionStack))
			{
				write2stdout(shmemMsg, sizeof(shmemMsg)-1, NULL);
			}
			else
			{
				snprintf(successMsgBuffer, sizeof(successMsgBuffer),
						LOAD_SUCCESS_PREFIX "%lu\n", (unsigned long)stackSize);
				write2stdout(successMsgBuffer, strlen(successMsgBuffer), shmCleanup);

				debugPrint("Fragekatalog %s erfolgreich geladen.", catalog);
			}
			break;

		case PARSE_CANNOT_OPEN:
			errorPrint("Kann Fragekatalog %s nicht öffnen!", catalog);
			write2stdout(cannotOpenMsg, sizeof(cannotOpenMsg)-1, NULL);
			break;

		case PARSE_CANNOT_READ:
			errorPrint("Fehler beim Einlesen des Fragekatalogs %s!", catalog);
			write2stdout(cannotReadMsg, sizeof(cannotReadMsg)-1, NULL);
			break;

		case PARSE_LINE_TOO_LONG:
			errorPrint("Zeile %lu des Fragekatalogs %s ist zu lang!",
					parserResult.lineNumber, catalog);
			write2stdout(invalidMsg, sizeof(invalidMsg)-1, NULL);
			break;

		case PARSE_UNEXPECTED_EOF:
			errorPrint("Unerwartetes Dateiende in Fragekatalog %s bei Zeile %lu!",
					catalog, parserResult.lineNumber);
			write2stdout(invalidMsg, sizeof(invalidMsg)-1, NULL);
			break;

		case PARSE_INVALID_UTF8:
			errorPrint("Ungültige UTF-8-Zeichenkette in Zeile %lu von Fragekatalog %s!",
					parserResult.lineNumber, catalog);
			write2stdout(invalidMsg, sizeof(invalidMsg)-1, NULL);
			break;

		case PARSE_INVALID_TIMEOUT:
			errorPrint("Ungültige Timeout-Angabe in Zeile %lu von Fragekatalog %s!",
					parserResult.lineNumber, catalog);
			write2stdout(invalidMsg, sizeof(invalidMsg)-1, NULL);
			break;

		case PARSE_INVALID_ANSWER:
			errorPrint("Ungültige Kennzeichnung der Antwort in Zeile %lu von Fragekatalog %s!",
					parserResult.lineNumber, catalog);
			write2stdout(invalidMsg, sizeof(invalidMsg)-1, NULL);
			break;

		case PARSE_EMPTY:
			errorPrint("Die Datei %s enthält keine Fragen!", catalog);
			write2stdout(invalidMsg, sizeof(invalidMsg)-1, NULL);
			break;

		case PARSE_OOM:
			errorPrint("Zu wenig Speicher beim Parsen des Kataloges %s (Zeile %lu)!",
					catalog, parserResult.lineNumber);
			write2stdout(oomMsg, sizeof(oomMsg)-1, NULL);
			break;
	}

	/* Stack leeren, falls noch Fragen auf ihm liegen (im Fehlerfall) */
	stackClear(&questionStack);
}
