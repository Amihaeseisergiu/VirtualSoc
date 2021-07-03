#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <termios.h>
#include <pthread.h>

extern int errno;

/* portul de conectare la server*/
int port, loggedIn, exited;
char username[1000];
pthread_mutex_t lock_exit = PTHREAD_MUTEX_INITIALIZER, lock_change_username = PTHREAD_MUTEX_INITIALIZER;

typedef struct thData
{
	int idThread; //id-ul thread-ului tinut in evidenta de acest program
	int cl; //descriptorul intors de accept
}thData;

void clean_stdin()
{
    int stdin_copy = dup(STDIN_FILENO);
    tcdrain(stdin_copy);
    tcflush(stdin_copy, TCIFLUSH);
    close(stdin_copy);
}

static void* reading(void* arg)
{
	struct thData tdL; 
	tdL= *((struct thData*)arg);
	while(1)
	{
		char msg[1000] = {0};
 
		if (read (tdL.cl, msg, sizeof(msg)) < 0)
    	{
     		perror ("[client]Eroare la read() de la server.\n");
      		return NULL;
    	}
		if(strcmp(msg, "/exit") == 0)
		{
			pthread_mutex_lock(&lock_exit);
			exited = 1;
			pthread_mutex_unlock(&lock_exit);
			break;
		}	
		if(strstr(msg, "Logged in!"))
		{
			pthread_mutex_lock(&lock_change_username);
			loggedIn = 1;
			strcpy(username, msg+26);
			pthread_mutex_unlock(&lock_change_username);
		}
		pthread_mutex_lock(&lock_change_username);
		if(loggedIn)printf("\033[2K\r%s\n%s: ",msg, username);
			else printf("\033[2K\r%s\nGuest: ",msg);
		pthread_mutex_unlock(&lock_change_username);
		fflush(stdout);
		clean_stdin();
	}	

	close ((intptr_t)arg);
	return(NULL);
}

static void* writing(void* arg)
{
	struct thData tdL; 
	tdL= *((struct thData*)arg);
	while(1)
  	{
		char msg[1000] = {0};
		/* citirea mesajului */
		
		pthread_mutex_lock(&lock_change_username);
		if(loggedIn)printf("\033[2K\r%s: ",username);
			else printf("\033[2K\rGuest: ");
		pthread_mutex_unlock(&lock_change_username);
		fflush(stdout);

		read(0, msg, sizeof(msg));
		msg[strlen(msg)-1] = '\0';

  		/* trimiterea mesajului la server */
  		if (write (tdL.cl, msg, sizeof(msg)) <= 0)
  		{	
  			perror ("[client]Eroare la write() spre server.\n");
  			return NULL;
  		}
	}

	close ((intptr_t)arg);
	return(NULL);
}

int main (int argc, char *argv[])
{
  	int sd;			// descriptorul de socket
  	struct sockaddr_in server;	// structura folosita pentru conectare 

  	/* exista toate argumentele in linia de comanda? */
  	if (argc != 3)
    {
    	printf ("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
    	return -1;
    }

  	/* stabilim portul */
  	port = atoi (argv[2]);

  	/* cream socketul */
  	if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
    	perror ("Eroare la socket().\n");
    	return errno;
    }

  	/* umplem structura folosita pentru realizarea conexiunii cu serverul */
  	/* familia socket-ului */
  	server.sin_family = AF_INET;
  	/* adresa IP a serverului */
  	server.sin_addr.s_addr = inet_addr(argv[1]);
  	/* portul de conectare */
  	server.sin_port = htons (port);
  
  	/* ne conectam la server */
 	if (connect (sd, (struct sockaddr *) &server,sizeof (struct sockaddr)) == -1)
    {
    	perror ("[client]Eroare la connect().\n");
    	return errno;
    }

	pthread_t th[2];
	for(int i = 0; i <= 1; i++)
	{
		thData *td;
		td=(struct thData*)malloc(sizeof(struct thData));	
		td->idThread = i;
		td->cl = sd;

		if(i==0)pthread_create(&th[i], NULL, &writing, td);	      
			else pthread_create(&th[i], NULL, &reading, td);	
		//pthread_detach(th[i]);   
	}
	while(exited == 0) ;
	printf("\033[2K\r");
	fflush(stdout);
}

