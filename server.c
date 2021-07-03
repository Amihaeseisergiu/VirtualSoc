
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <dirent.h>

/* portul folosit */
#define PORT 2908
#define MAXONLINE 100
#define MAXCHATROOMS 100
#define MAXROOMCAPACITY 100
/* codul de eroare returnat de anumite apeluri */
extern int errno;
pthread_mutex_t lock_update_online = PTHREAD_MUTEX_INITIALIZER, lock_update_chatrooms = PTHREAD_MUTEX_INITIALIZER, lock_update_file = PTHREAD_MUTEX_INITIALIZER;

struct 
{
	int isonline;
	int fd;
	int id;
	int busy;
	char username[64];
}online[MAXONLINE];

struct
{
	int free;
	int inside[MAXROOMCAPACITY];
	int nrinvited;
	int nrfiles;
	char invited[MAXROOMCAPACITY][100];
	char files[MAXROOMCAPACITY][1000];
	char name[100];
}chatrooms[MAXCHATROOMS];

typedef struct thData
{
	int idThread; //id-ul thread-ului tinut in evidenta de acest program
	int cl; //descriptorul intors de accept
}thData;

typedef struct messageData
{
	int nr;
	int size;
	char lines[MAXROOMCAPACITY][100];
}messageData;

int split(char line[1000], char lines[1000][1000], char* delim)
{
	int i = 0;
	char* p = strtok(line, delim);
	while(p)
	{
		strcpy(lines[i++], p);
		p = strtok(NULL, delim);
	}
	return i-1;
}

void delete_directory(const char path[])
{
    char full_path[1000];
    DIR *dir;
    struct stat stat_path, stat_entry;
    struct dirent *entry;

    stat(path, &stat_path);

    if (S_ISDIR(stat_path.st_mode) == 0)
	{
        fprintf(stderr, "%s: %s\n", "Is not directory", path);
        exit(-1);
    }

    if ((dir = opendir(path)) == NULL)
	{
        fprintf(stderr, "%s: %s\n", "Cant open directory", path);
        exit(-1);
    }

    while ((entry = readdir(dir)) != NULL)
	{
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        strcpy(full_path, path);
        strcat(full_path, "/");
        strcat(full_path, entry->d_name);

        stat(full_path, &stat_entry);

        if (S_ISDIR(stat_entry.st_mode) != 0)
		{
            delete_directory(full_path);
            continue;
        }

        unlink(full_path);
    }

   	rmdir(path);
    closedir(dir);
}

void send_notification(char usr[1000], char msg[1000])
{
	int foundonline = 0;
	pthread_mutex_lock(&lock_update_online);
	for(int j = 0; j < MAXONLINE; j++)
		if(online[j].busy == 0 && strcmp(online[j].username, usr) == 0)
		{
			write (online[j].fd, msg, strlen(msg));
			foundonline = 1;
			break;
		}
		else if(online[j].busy == 1 && strcmp(online[j].username, usr) == 0)
		{
			char tmp[1000] = "serverdata/users/";
			strcat(tmp, online[j].username);
			strcat(tmp, "/notifications.txt");
			FILE *f = fopen(tmp, "a");
		    fprintf(f, "%s\n", msg);
			fclose(f);
			foundonline = 1;
		    break;
		}
	pthread_mutex_unlock(&lock_update_online);

	if(foundonline == 0)
	{
		int found = 0;
		char id[64], user[64], pass[64];
		FILE* f = fopen("serverdata/accounts.txt", "r");
		while(1)
		{
			if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
			{
				fclose(f);
				break;
			}
			if(strcmp(usr, user) == 0)
			{
				found = 1;
				fclose(f);
				break;
			}
		}
		if(found)
		{
			char tmp[1000] = "serverdata/users/";
			strcat(tmp, usr);
			strcat(tmp, "/notifications.txt");
			FILE *f = fopen(tmp, "a");
		    fprintf(f, "%s\n", msg);
			fclose(f); 
		}
	}
}

void view_comment(char* path, char* user, char* id, int cl)
{
	char tmp[1000], tmp2[1000], usrreac[100], reac[100], text[1000], buffer[1000], foo[10];
	int nrreac = 0;
	strcpy(tmp, path);
	strcat(tmp, "/");
	strcat(tmp, user);
	strcat(tmp, "*");
	strcat(tmp, id);
	strcpy(tmp2, tmp);
	strcat(tmp2, "r");
	FILE* f = fopen(tmp2, "r");
	while(1)
	{
		if(fscanf(f, "%s %s", usrreac, reac) == EOF)
		{
			fclose(f);
			break;
		}
		nrreac++;
	}

	f = fopen(tmp, "r");
	int firstline = 0;
	bzero(text,1000);						
	while (fgets(buffer, sizeof(buffer), f))
	{
		buffer[strlen(buffer)-1] = 0;
		if(firstline == 0)
		{
			firstline = 1;
			strcat(buffer, "\033[0;92m | \033[0;36m");
			snprintf(foo, 10, "%d", nrreac);
			strcat(buffer, foo);
			strcat(buffer, " \033[0;92mReactions\033[0m\n");
		}
		else strcat(buffer, "\n");
		strcat(text, buffer);
	}
	fclose(f);
	text[strlen(text)-1] = 0;
	if(write( cl, text, sizeof(text)) <= 0)
		perror("[server]Eroare la write() catre client.\n");
}

void view_profile(char* path, char* filename, int cl)
{
	char text[1000] = "\033[2J\033[H", buffer[1000], pathtoprofile[1000];
	strcpy(pathtoprofile, path);
	strcat(pathtoprofile, "/");
	strcat(pathtoprofile, filename);
	FILE *f = fopen(pathtoprofile, "r");
	while (fgets(buffer, sizeof(buffer), f)) strcat(text, buffer);
	fclose(f);
	if(write( cl, text, sizeof(text)) <= 0)
		perror("[server]Eroare la write() catre client.\n");
}

void view_post(char* path, char* filename, int cl)
{
	char text[1000] = "\033[2J\033[H", buffer[1000], pathtopost[1000], tmp[1000], files[1000][1000];
	int nrcomments = 0, nrreac = 0, val[1000], nrsort = 0;
	strcpy(pathtopost, path);
	strcat(pathtopost, "/");
	strcat(pathtopost, filename);
	strcpy(tmp, pathtopost);
	strcat(tmp, "/post.txt");
	FILE *f = fopen(tmp, "r");
	while (fgets(buffer, sizeof(buffer), f)) strcat(text, buffer);
	fclose(f);
	text[strlen(text)-1] = 0;
	if(write( cl, text, sizeof(text)) <= 0)
		perror("[server]Eroare la write() catre client.\n");

	strcpy(tmp, pathtopost);
	strcat(tmp,"/comments");
	DIR* directory2 = opendir(tmp);
	struct dirent* currentfile2;

	while( (currentfile2 = readdir(directory2)) != NULL)
		if(strcmp(currentfile2->d_name, ".") != 0 && strcmp(currentfile2->d_name, "..") != 0) nrcomments++;
	closedir(directory2);
	nrcomments = nrcomments/2;

	strcpy(tmp, pathtopost);
	strcat(tmp, "/reactions.txt");
	char usrreac[100], reac[100];
	f = fopen(tmp, "r");
	while(1)
	{
		if(fscanf(f, "%s %s", usrreac, reac) == EOF)
		{
			fclose(f);
			break;
		}
		nrreac++;
	}
	strcpy(text, "\033[0;36m");
	char foo[10], dotted[100] = "\033[0;92m----------------------";
	snprintf(foo, 10, "%d", nrcomments);
	strcat(text, foo);
	for(int i = 0; i < strlen(foo); i++) strcat(dotted, "-");
	strcat(text, " \033[0;92mComments | \033[0;36m");
	snprintf(foo, 10, "%d", nrreac);
	strcat(text, foo);
	for(int i = 0; i < strlen(foo); i++) strcat(dotted, "-");
	strcat(dotted, "\033[0m");
	strcat(text, " \033[0;92mReactions\033[0m\n");
	strcat(text, dotted);
	if(nrcomments == 0 && nrreac == 0)
	{
		strcat(text, "\n\033[0;92mBe the first to comment and react on this post!\033[0m\n");
		strcat(text, dotted);
	}
	else if(nrcomments == 0 && nrreac > 0)
	{
		strcat(text, "\n\033[0;92mBe the first to comment on this post!\033[0m\n");
		strcat(text, dotted);
	}
	else if(nrcomments > 0 && nrreac == 0)
	{
		strcat(text, "\n\033[0;92mBe the first to react on this post!\033[0m\n");
		strcat(text, dotted);
	}
	if(write( cl, text, sizeof(text)) <= 0)
		perror("[server]Eroare la write() catre client.\n");
	
	strcpy(tmp, pathtopost);
	strcat(tmp,"/comments");
	directory2 = opendir(tmp);

	while( (currentfile2 = readdir(directory2)) != NULL)
		if(strcmp(currentfile2->d_name, ".") != 0 && strcmp(currentfile2->d_name, "..") != 0)
		{
			if(currentfile2->d_name[strlen(currentfile2->d_name)-1] != 'r')
			{
				char tmp2[100];
				nrsort++;
				nrreac = 0;
				strcpy(tmp2, tmp);
				strcat(tmp2, "/");
				strcat(tmp2, currentfile2->d_name);
				strcpy(files[nrsort], tmp2);
				strcat(tmp2, "r");
				f = fopen(tmp2, "r");
				while(1)
				{
					if(fscanf(f, "%s %s", usrreac, reac) == EOF)
					{
						fclose(f);
						break;
					}
					nrreac++;
				}
				val[nrsort] = nrreac;
			}	
		}
	closedir(directory2);
	for(int i = 1; i < nrsort; i++)
		for(int j = i+1; j <= nrsort; j++)
			if(val[i] < val[j])
			{
				char auxfile[1000];
				int auxval = val[i];
				strcpy(auxfile, files[i]);
				val[i] = val[j];
				strcpy(files[i], files[j]);
				val[j] = auxval;
				strcpy(files[j], auxfile);
			}
	for(int i = 1; i <= nrsort; i++)
	{
		char tmp2[1000];
		nrreac = 0;
		strcpy(tmp, files[i]);
		strcpy(tmp2, files[i]);
		strcat(tmp2, "r");
		f = fopen(tmp2, "r");
		while(1)
		{
			if(fscanf(f, "%s %s", usrreac, reac) == EOF)
			{
				fclose(f);
				break;
			}
			nrreac++;
		}

		f = fopen(tmp, "r");
		int firstline = 0;
		bzero(text,1000);						
		while (fgets(buffer, sizeof(buffer), f))
		{
			buffer[strlen(buffer)-1] = 0;
			if(firstline == 0)
			{
				firstline = 1;
				strcat(buffer, "\033[0;92m | \033[0;36m");
				snprintf(foo, 10, "%d", nrreac);
				strcat(buffer, foo);
				strcat(buffer, " \033[0;92mReactions\033[0m\n");
			}
			else strcat(buffer, "\n");
			strcat(text, buffer);
		}
		fclose(f);
		text[strlen(text)-1] = 0;
		if(write( cl, text, sizeof(text)) <= 0)
			perror("[server]Eroare la write() catre client.\n");
	}
}

void add_comment(char* path, char* filename, char* user, char* postname, int cl, int id)
{
	char dotted[100] = "--------------------------------------------", msg[1000] = "\033[2J\033[H\033[1;92m[Comment] \033[0;92mEditing comment on post \033[0;94m[", text[1000] = {0}, myname[100];
	strcat(msg, postname);
	strcat(msg, "] \033[0;92mfrom \033[0;94m[");
	strcat(msg, user);
	strcat(msg, "]\033[0;92m\n");
	for(int i = 0; i < strlen(user); i++) strcat(dotted, "-");
	for(int i = 0; i < strlen(postname); i++) strcat(dotted, "-");
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 1;
	strcpy(myname, online[id].username);
	pthread_mutex_unlock(&lock_update_online);
	strcat(msg, dotted);
	strcat(msg, "\033[0m");
	if (write (cl, msg, sizeof(msg)) <= 0)
		perror ("[server]Eroare la write() catre client.\n");
	
	char fullpath[1000], pathto1[1000], pathto1r[1000], buffer[10];
	int count = 0;
	strcpy(fullpath, path);
	strcat(fullpath, "/");
	strcat(fullpath, filename);
	strcat(fullpath, "/comments");
	DIR* directory = opendir(fullpath);
	struct dirent* currentfile;

	while( (currentfile = readdir(directory)) != NULL)
		if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0) count++;
	closedir(directory);
	count = count/2;

	snprintf(buffer, 10, "%d", count+1);
	strcpy(pathto1, fullpath);
	strcat(pathto1, "/");
	strcat(pathto1, myname);
	strcat(pathto1, "*");
	strcat(pathto1, buffer);
	strcpy(pathto1r, pathto1);
	strcat(pathto1r, "r");
	FILE* f = fopen(pathto1, "a");
	fclose(f);
	f = fopen(pathto1r, "a");
	fclose(f);

	while(1)
	{
		char msgtmp[1000], tmp[1000] = {0}, tmplines[1000][1000] = {0};
		if (read (cl, msgtmp, sizeof(msgtmp)) <= 0)
			perror ("[server]Eroare la read() de la client.\n");
		strcpy(tmp, msgtmp);
		split(tmp, tmplines, " ");
		if(strcmp(tmplines[0], "/post") == 0)
		{
			strcpy(dotted, "---------------");
			for(int i = 0; i < strlen(myname); i++) strcat(dotted, "-");
			if(access(pathto1, F_OK) == 0)
			{
				f = fopen(pathto1, "a");
				fprintf(f, "\033[0;92mComment from \033[0;94m[%s]\033[0m\n\033[0;92m%s\033[0m%s\n\033[0;92m%s\033[0m\n", myname, dotted, text, dotted);
				fclose(f);
				view_post(path, filename, cl);
				break;
			}
			else
			{
				strcpy(msg, "\033[2J\033[H\033[1;92m[Comment] \033[0;92mPost has been deleted\033[0m");
				if (write (cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				break;
			}
		}
		else if(strcmp(tmplines[0], "/exit") == 0)
		{
			unlink(pathto1);
			unlink(pathto1r);
			strcpy(msg, "\033[2J\033[H\033[1;92m[Comment] \033[0;92mEditing cancelled \033[0m");
			if (write (cl, msg, sizeof(msg)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			break;
		}
		else 
		{
			strcat(text, "\n\033[0;92m| ");
			strcat(text, msgtmp);
			strcat(text, "\033[0m");
		}
	}
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

void add_reaction_post(char* path, char* filename, char* user, char* postname, int cl, int id)
{
	char dotted[100] = "--------------------------------------------", msg[1000] = "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mAdd a reaction to post \033[0;94m[", myname[100];
	strcat(msg, postname);
	strcat(msg, "] \033[0;92mfrom \033[0;94m[");
	strcat(msg, user);
	strcat(msg, "]\033[0;92m\n");
	for(int i = 0; i < strlen(user); i++) strcat(dotted, "-");
	for(int i = 0; i < strlen(postname); i++) strcat(dotted, "-");
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 1;
	strcpy(myname, online[id].username);
	pthread_mutex_unlock(&lock_update_online);
	strcat(msg, dotted);
	strcat(msg, "\033[0m");
	if (write (cl, msg, sizeof(msg)) <= 0)
		perror ("[server]Eroare la write() catre client.\n");

	while(1)
	{
		char msgtmp[1000];
		if (read (cl, msgtmp, sizeof(msgtmp)) <= 0)
			perror ("[server]Eroare la read() de la client.\n");
		if(strcmp(msgtmp, "/exit") == 0)
		{
			strcpy(msg, "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mAdding reaction cancelled \033[0m");
			if (write (cl, msg, sizeof(msg)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			break;
		}
		else if(strcmp(msgtmp, "happy") == 0 || strcmp(msgtmp, "sad") == 0 || strcmp(msgtmp, "angry") == 0 || strcmp(msgtmp, "haha") == 0 || strcmp(msgtmp, "like") == 0)
		{
			char fullpath[1000], usrreac[100], typer[10];
			strcpy(fullpath, path);
			strcat(fullpath, "/");
			strcat(fullpath, filename);
			strcat(fullpath, "/reactions.txt");
			if(access(fullpath, F_OK) == 0)
			{
				FILE* f = fopen(fullpath, "r");
				int found = 0;
				while(1)
				{
					if(fscanf(f, "%s %s", usrreac, typer) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(myname, usrreac) == 0)
					{
						found = 1;
						break;
					}
				}
				if(found)
				{
					strcpy(msg, "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mAlready reacted to this post\033[0m");
					if (write (cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
					break;
				}
				else
				{
					f = fopen(fullpath, "a");
					fprintf(f, "%s %s\n", myname, msgtmp);
					fclose(f);
					view_post(path, filename, cl);
					break;
				}
			}
			else
			{
				strcpy(msg, "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mPost has been deleted\033[0m");
				if (write (cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				break;
			}
		}
		else
		{
			strcpy(msg, "\033[1;92m[Reaction] \033[0;92mUnknown reaction\033[0m");
			if (write (cl, msg, sizeof(msg)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
		}
	}
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

void add_reaction_comment(char* path, char* filename, char* user, char* postname, int cl, int id)
{
	char dotted[100] = "----------------------------------------------------", msg[1000] = "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mAdd a reaction to a comment in \033[0;94m[", myname[100];
	char msg2[1000] = "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mMultiple comments from the same user have been found:\n----------------------------------------------------------------\033[0m";
	strcat(msg, postname);
	strcat(msg, "] \033[0;92mfrom \033[0;94m[");
	strcat(msg, user);
	strcat(msg, "]\033[0;92m\n");
	for(int i = 0; i < strlen(user); i++) strcat(dotted, "-");
	for(int i = 0; i < strlen(postname); i++) strcat(dotted, "-");
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 1;
	strcpy(myname, online[id].username);
	pthread_mutex_unlock(&lock_update_online);
	strcat(msg, dotted);
	strcat(msg, "\033[0m");
	
	int countcollisions = 0, error = 0;
	char pathtoreac[1000], save[1000][1000];
	strcpy(pathtoreac, path);
	strcat(pathtoreac, "/");
	strcat(pathtoreac, filename);
	strcat(pathtoreac, "/comments");

	DIR* directory = opendir(pathtoreac);
	struct dirent* currentfile;

	while( (currentfile = readdir(directory)) != NULL)
		if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
		{
			char name2[1000], names2[1000][1000];
			strcpy(name2, currentfile->d_name);
			split(name2, names2, "*");
			if(strcmp(names2[0], user) == 0 && currentfile->d_name[strlen(currentfile->d_name)-1] != 'r')
			{
				countcollisions++;
				strcpy(save[countcollisions], names2[1]);
			}
		}
	closedir(directory);
	if(countcollisions > 1)
	{
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
		for(int i = 1; i <= countcollisions; i++)
		{
			strcpy(msg2, "\033[0;92mComment \033[0;94m[");
			strcat(msg2, save[i]);
			strcat(msg2, "]\033[0m");
			if (write (cl, msg2, sizeof(msg2)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			view_comment(pathtoreac, user, save[i], cl);
		}
		strcpy(msg2, "\033[0;92m----------------------------------------------------------------\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");

		while(1)
		{
			char in[1000];
			int fval = 0;
			if (read (cl, in, sizeof(in)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			for(int i = 1; i <= countcollisions; i++)
				if(strcmp(save[i], in) == 0) fval = 1;
			if(strcmp(in, "/exit") == 0)
			{
				error = 1;
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mAdding reaction cancelled\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				break;
			}
			if(fval)
			{
				strcat(pathtoreac, "/");
				strcat(pathtoreac, user);
				strcat(pathtoreac, "*");
				strcat(pathtoreac, in);
				strcat(pathtoreac, "r");
				break;
			}
			else
			{
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mInvalid id of a comment\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
	}
	else if(countcollisions == 1)
	{
		strcat(pathtoreac, "/");
		strcat(pathtoreac, user);
		strcat(pathtoreac, "*");
		strcat(pathtoreac, save[1]);
		strcat(pathtoreac, "r");
	}
	else
	{
		error = 1;
		strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mComment hasn't been found\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
	}

	if(error == 0)
	{
		if (write (cl, msg, sizeof(msg)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");

		while(1)
		{
			char msgtmp[1000];
			if (read (cl, msgtmp, sizeof(msgtmp)) <= 0)
				perror ("[server]Eroare la read() de la client.\n");
			if(strcmp(msgtmp, "/exit") == 0)
			{
				strcpy(msg, "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mAdding reaction cancelled \033[0m");
				if (write (cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				break;
			}
			else if(strcmp(msgtmp, "happy") == 0 || strcmp(msgtmp, "sad") == 0 || strcmp(msgtmp, "angry") == 0 || strcmp(msgtmp, "haha") == 0 || strcmp(msgtmp, "like") == 0)
			{
				char usrreac[100], typer[10];
				if(access(pathtoreac, F_OK) == 0)
				{
					FILE* f = fopen(pathtoreac, "r");
					int found = 0;
					while(1)
					{
						if(fscanf(f, "%s %s", usrreac, typer) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(myname, usrreac) == 0)
						{
							found = 1;
							break;
						}
					}
					if(found)
					{
						strcpy(msg, "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mAlready reacted to this post\033[0m");
						if (write (cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
						break;
					}
					else
					{
						f = fopen(pathtoreac, "a");
						fprintf(f, "%s %s\n", myname, msgtmp);
						fclose(f);
						view_post(path, filename, cl);
						break;
					}
				}
				else
				{
					strcpy(msg, "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mPost has been deleted\033[0m");
					if (write (cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
					break;
				}
			}
			else
			{
				strcpy(msg, "\033[1;92m[Reaction] \033[0;92mUnknown reaction\033[0m");
				if (write (cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
	}
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

void show_reaction_post(char* path, char* filename, char* user, char* postname, int cl, int id)
{
	char dotted[100] = "--------------------------------------------", msg[1000] = "\033[2J\033[H\033[1;92m[Reactions] \033[0;92mReactions of the post \033[0;94m[", pathtoreac[1000];
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 1;
	pthread_mutex_unlock(&lock_update_online);
	strcat(msg, postname);
	strcat(msg, "] \033[0;92mfrom \033[0;94m[");
	strcat(msg, user);
	strcat(msg, "]\033[0;92m\n");
	for(int i = 0; i < strlen(user); i++) strcat(dotted, "-");
	for(int i = 0; i < strlen(postname); i++) strcat(dotted, "-");
	strcat(msg, dotted);
	strcat(msg, "\033[0m");
	strcpy(pathtoreac, path);
	strcat(pathtoreac, "/");
	strcat(pathtoreac, filename);
	strcat(pathtoreac, "/reactions.txt");
	FILE* f = fopen(pathtoreac, "r");
	char usrreac[100], reac[100], buffer[10];
	int cont = 0;
	while(1)
	{
		if(fscanf(f, "%s %s", usrreac, reac) == EOF)
		{
			fclose(f);
			break;
		}
		cont++;
		snprintf(buffer, 10, "%d", cont);
		strcat(msg, "\n\033[0;94m[");
		strcat(msg, buffer);
		strcat(msg, "] \033[0;36m[");
		strcat(msg, usrreac);
		strcat(msg, "] \033[0;92mreacted with \033[0;36m[");
		strcat(msg, reac);
		strcat(msg, "]\033[0m");
	}
	if(cont == 0)strcat(msg, "\n\033[0;92mBe the first to react to this post\033[0m");
	strcat(msg, "\n\033[0;92m");
	strcat(msg, dotted);
	strcat(msg, "\033[0m");
	if (write (cl, msg, sizeof(msg)) <= 0)
		perror ("[server]Eroare la write() catre client.\n");
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

void show_reaction_comment(char* path, char* filename, char* user, char* postname, int cl, int id)
{
	char dotted[100] = "------------------------------------------------", msg[1000] = "\033[2J\033[H\033[1;92m[Reactions] \033[0;92mReactions of a comment from \033[0;94m[", myname[100];
	char msg2[1000] = "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mMultiple comments from the same user have been found:\n----------------------------------------------------------------\033[0m";
	strcat(msg, user);
	strcat(msg, "] \033[0;92min \033[0;94m[");
	strcat(msg, postname);
	strcat(msg, "]\033[0;92m\n");
	for(int i = 0; i < strlen(user); i++) strcat(dotted, "-");
	for(int i = 0; i < strlen(postname); i++) strcat(dotted, "-");
	pthread_mutex_lock(&lock_update_online);
	strcpy(myname, online[id].username);
	online[id].busy = 1;
	pthread_mutex_unlock(&lock_update_online);
	strcat(msg, dotted);
	strcat(msg, "\033[0m");
	
	int countcollisions = 0, error = 0;
	char pathtoreac[1000], save[1000][1000];
	strcpy(pathtoreac, path);
	strcat(pathtoreac, "/");
	strcat(pathtoreac, filename);
	strcat(pathtoreac, "/comments");

	DIR* directory = opendir(pathtoreac);
	struct dirent* currentfile;

	while( (currentfile = readdir(directory)) != NULL)
		if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
		{
			char name2[1000], names2[1000][1000];
			strcpy(name2, currentfile->d_name);
			split(name2, names2, "*");
			if(strcmp(names2[0], user) == 0 && currentfile->d_name[strlen(currentfile->d_name)-1] != 'r')
			{
				countcollisions++;
				strcpy(save[countcollisions], names2[1]);
			}
		}
	closedir(directory);
	if(countcollisions > 1)
	{
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
		for(int i = 1; i <= countcollisions; i++)
		{
			strcpy(msg2, "\033[0;92mComment \033[0;94m[");
			strcat(msg2, save[i]);
			strcat(msg2, "]\033[0m");
			if (write (cl, msg2, sizeof(msg2)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			view_comment(pathtoreac, user, save[i], cl);
		}
		strcpy(msg2, "\033[0;92m----------------------------------------------------------------\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");

		while(1)
		{
			char in[1000];
			int fval = 0;
			if (read (cl, in, sizeof(in)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			for(int i = 1; i <= countcollisions; i++)
				if(strcmp(save[i], in) == 0) fval = 1;
			if(strcmp(in, "/exit") == 0)
			{
				error = 1;
				strcpy(msg2, "\033[1;92m[Reactions] \033[0;92mShowing reaction cancelled\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				break;
			}
			if(fval)
			{
				strcat(pathtoreac, "/");
				strcat(pathtoreac, user);
				strcat(pathtoreac, "*");
				strcat(pathtoreac, in);
				strcat(pathtoreac, "r");
				break;
			}
			else
			{
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mInvalid id of a comment\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
	}
	else if(countcollisions == 1)
	{
		strcat(pathtoreac, "/");
		strcat(pathtoreac, user);
		strcat(pathtoreac, "*");
		strcat(pathtoreac, save[1]);
		strcat(pathtoreac, "r");
	}
	else
	{
		error = 1;
		strcpy(msg2, "\033[1;92m[Reactions] \033[0;92mComment hasn't been found\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
	}

	if(error == 0)
	{
		FILE* f = fopen(pathtoreac, "r");
		char usrreac[100], reac[100], buffer[10];
		int cont = 0;
		while(1)
		{
			if(fscanf(f, "%s %s", usrreac, reac) == EOF)
			{
				fclose(f);
				break;
			}
			cont++;
			snprintf(buffer, 10, "%d", cont);
			strcat(msg, "\n\033[0;94m[");
			strcat(msg, buffer);
			strcat(msg, "] \033[0;36m[");
			strcat(msg, usrreac);
			strcat(msg, "] \033[0;92mreacted with \033[0;36m[");
			strcat(msg, reac);
			strcat(msg, "]\033[0m");
		}
		if(cont == 0)strcat(msg, "\n\033[0;92mBe the first to react to this comment\033[0m");
		strcat(msg, "\n\033[0;92m");
		strcat(msg, dotted);
		strcat(msg, "\033[0m");
		if (write (cl, msg, sizeof(msg)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
	}
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

void delete_comment(char* path, char* filename, char* user, char* myname, int cl, int id)
{
	char msg2[1000] = "\033[2J\033[H\033[1;92m[Comment] \033[0;92mMultiple comments from the same user have been found:\n----------------------------------------------------------------\033[0m";
	int countcollisions = 0, error = 0;
	char pathtoreac[1000], pathtocomm[1000], save[1000][1000];
	strcpy(pathtoreac, path);
	strcat(pathtoreac, "/");
	strcat(pathtoreac, filename);
	strcat(pathtoreac, "/comments");
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 1;
	pthread_mutex_unlock(&lock_update_online);

	DIR* directory = opendir(pathtoreac);
	struct dirent* currentfile;

	while( (currentfile = readdir(directory)) != NULL)
		if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
		{
			char name2[1000], names2[1000][1000];
			strcpy(name2, currentfile->d_name);
			split(name2, names2, "*");
			if(strcmp(names2[0], myname) == 0 && currentfile->d_name[strlen(currentfile->d_name)-1] != 'r')
			{
				countcollisions++;
				strcpy(save[countcollisions], names2[1]);
			}
		}
	closedir(directory);
	if(countcollisions > 1)
	{
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
		for(int i = 1; i <= countcollisions; i++)
		{
			strcpy(msg2, "\033[0;92mComment \033[0;94m[");
			strcat(msg2, save[i]);
			strcat(msg2, "]\033[0m");
			if (write (cl, msg2, sizeof(msg2)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			view_comment(pathtoreac, myname, save[i], cl);
		}
		strcpy(msg2, "\033[0;92m----------------------------------------------------------------\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");

		while(1)
		{
			char in[1000];
			int fval = 0;
			if (read (cl, in, sizeof(in)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			for(int i = 1; i <= countcollisions; i++)
				if(strcmp(save[i], in) == 0) fval = 1;
			if(strcmp(in, "/exit") == 0)
			{
				error = 1;
				strcpy(msg2, "\033[1;92m[Comment] \033[0;92mComment deletion cancelled\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				break;
			}
			if(fval)
			{
				strcat(pathtoreac, "/");
				strcat(pathtoreac, myname);
				strcat(pathtoreac, "*");
				strcat(pathtoreac, in);
				strcpy(pathtocomm, pathtoreac);
				strcat(pathtoreac, "r");
				break;
			}
			else
			{
				strcpy(msg2, "\033[1;92m[Comment] \033[0;92mInvalid id of a comment\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
	}
	else if(countcollisions == 1)
	{
		strcat(pathtoreac, "/");
		strcat(pathtoreac, myname);
		strcat(pathtoreac, "*");
		strcat(pathtoreac, save[1]);
		strcpy(pathtocomm, pathtoreac);
		strcat(pathtoreac, "r");
	}
	else
	{
		error = 1;
		strcpy(msg2, "\033[1;92m[Comment] \033[0;92mComment hasn't been found\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
	}

	if(error == 0)
	{
		strcpy(msg2, "\033[1;92m[Comment] \033[0;92mAre you sure you want to delete this comment? (Y/n)\033[0m");
		if(write(cl, msg2, sizeof(msg2)) <= 0)
			perror("[server]Eroare la write() catre client");
		while(1)
		{
			char readoption[1000];
			if(read(cl, readoption, sizeof(readoption)) <= 0)
				perror("[server]Eroare la write() catre client");

			if(strcmp(readoption, "Y") == 0)
			{
				if(access(pathtoreac, F_OK) == 0 && access(pathtocomm, F_OK) == 0)
				{
					unlink(pathtoreac);
					unlink(pathtocomm);
					view_post(path, filename, cl);
					break;
				}
				else
				{
					strcpy(msg2, "\033[1;92m[Comment] \033[0;92mComment already deleted\033[0m");
					if(write(cl, msg2, sizeof(msg2)) <= 0)
						perror("[server]Eroare la write() catre client");
					break;
				}
			}
			else if(strcmp(readoption, "n") == 0)
			{
				strcpy(msg2, "\033[1;92m[Comment] \033[0;92mComment deletion cancelled\033[0m");
				if(write(cl, msg2, sizeof(msg2)) <= 0)
					perror("[server]Eroare la write() catre client");
				break;
			}
			else
			{
				strcpy(msg2, "\033[1;92m[Comment] \033[0;92mInvalid option\033[0m");
				if(write(cl, msg2, sizeof(msg2)) <= 0)
					perror("[server]Eroare la write() catre client");
			}
		}
	}
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

void delete_reaction_post(char* path, char* filename, char* user, char* myname, int cl, int id)
{
	char msg2[1000], pathtoreac[1000], pathtoreac2[1000], usr[100], typer[100];
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 1;
	pthread_mutex_unlock(&lock_update_online);
	strcpy(pathtoreac, path);
	strcat(pathtoreac, "/");
	strcat(pathtoreac, filename);
	strcpy(pathtoreac2, pathtoreac);
	strcat(pathtoreac, "/reactions.txt");
	strcat(pathtoreac2, "/reactions2.txt");
	strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mAre you sure you want to delete this reaction? (Y/n)\033[0m");
	if(write(cl, msg2, sizeof(msg2)) <= 0)
		perror("[server]Eroare la write() catre client");
	while(1)
	{
		char readoption[1000];
		if(read(cl, readoption, sizeof(readoption)) <= 0)
			perror("[server]Eroare la write() catre client");

		if(strcmp(readoption, "Y") == 0)
		{
			if(access(pathtoreac, F_OK) == 0)
			{
				FILE* f = fopen(pathtoreac, "r");
				FILE* f2 = fopen(pathtoreac2, "a");
				while(1)
				{
					if(fscanf(f,"%s %s", usr, typer) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					if(strcmp(usr, myname) != 0)
						fprintf(f2, "%s %s\n", usr, typer);
				}
				f = fopen(pathtoreac, "w");
				f2 = fopen(pathtoreac2, "r");
				while(1)
				{
					if(fscanf(f2,"%s %s", usr, typer) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					fprintf(f, "%s %s\n", usr, typer);
				}
				unlink(pathtoreac2);
				view_post(path, filename, cl);
				break;
			}
			else
			{
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mPost has been deleted\033[0m");
				if(write(cl, msg2, sizeof(msg2)) <= 0)
					perror("[server]Eroare la write() catre client");
				break;
			}
		}
		else if(strcmp(readoption, "n") == 0)
		{
			strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mReaction deletion cancelled\033[0m");
			if(write(cl, msg2, sizeof(msg2)) <= 0)
				perror("[server]Eroare la write() catre client");
			break;
		}
		else
		{
			strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mInvalid option\033[0m");
			if(write(cl, msg2, sizeof(msg2)) <= 0)
				perror("[server]Eroare la write() catre client");
		}
	}
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

void delete_reaction_comment(char* path, char* filename, char* user, char* myname, int cl, int id)
{
	char msg2[1000] = "\033[2J\033[H\033[1;92m[Reaction] \033[0;92mMultiple comments from the same user have been found:\n----------------------------------------------------------------\033[0m";
	int countcollisions = 0, error = 0;
	char pathtoreac[1000], save[1000][1000];
	strcpy(pathtoreac, path);
	strcat(pathtoreac, "/");
	strcat(pathtoreac, filename);
	strcat(pathtoreac, "/comments");
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 1;
	pthread_mutex_unlock(&lock_update_online);

	DIR* directory = opendir(pathtoreac);
	struct dirent* currentfile;

	while( (currentfile = readdir(directory)) != NULL)
		if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
		{
			char name2[1000], names2[1000][1000];
			strcpy(name2, currentfile->d_name);
			split(name2, names2, "*");
			if(strcmp(names2[0], user) == 0 && currentfile->d_name[strlen(currentfile->d_name)-1] != 'r')
			{
				countcollisions++;
				strcpy(save[countcollisions], names2[1]);
			}
		}
	closedir(directory);
	if(countcollisions > 1)
	{
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
		for(int i = 1; i <= countcollisions; i++)
		{
			strcpy(msg2, "\033[0;92mComment \033[0;94m[");
			strcat(msg2, save[i]);
			strcat(msg2, "]\033[0m");
			if (write (cl, msg2, sizeof(msg2)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			view_comment(pathtoreac, user, save[i], cl);
		}
		strcpy(msg2, "\033[0;92m----------------------------------------------------------------\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");

		while(1)
		{
			char in[1000];
			int fval = 0;
			if (read (cl, in, sizeof(in)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
			for(int i = 1; i <= countcollisions; i++)
				if(strcmp(save[i], in) == 0) fval = 1;
			if(strcmp(in, "/exit") == 0)
			{
				error = 1;
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mReaction deletion cancelled\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				break;
			}
			if(fval)
			{
				strcat(pathtoreac, "/");
				strcat(pathtoreac, user);
				strcat(pathtoreac, "*");
				strcat(pathtoreac, in);
				strcat(pathtoreac, "r");
				break;
			}
			else
			{
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mInvalid id of a comment\033[0m");
				if (write (cl, msg2, sizeof(msg2)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
	}
	else if(countcollisions == 1)
	{
		strcat(pathtoreac, "/");
		strcat(pathtoreac, user);
		strcat(pathtoreac, "*");
		strcat(pathtoreac, save[1]);
		strcat(pathtoreac, "r");
	}
	else
	{
		error = 1;
		strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mComment hasn't been found\033[0m");
		if (write (cl, msg2, sizeof(msg2)) <= 0)
			perror ("[server]Eroare la write() catre client.\n");
	}

	if(error == 0)
	{
		char pathtoreac2[1000];
		strcpy(pathtoreac2, pathtoreac);
		strcat(pathtoreac2, "2");
		strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mAre you sure you want to delete this reaction? (Y/n)\033[0m");
		if(write(cl, msg2, sizeof(msg2)) <= 0)
			perror("[server]Eroare la write() catre client");
		while(1)
		{
			char readoption[1000];
			if(read(cl, readoption, sizeof(readoption)) <= 0)
				perror("[server]Eroare la write() catre client");

			if(strcmp(readoption, "Y") == 0)
			{
				char usr[100], typer[100];
				if(access(pathtoreac, F_OK) == 0)
				{
					FILE* f = fopen(pathtoreac, "r");
					FILE* f2 = fopen(pathtoreac2, "a");
					while(1)
					{
						if(fscanf(f,"%s %s", usr, typer) == EOF)
						{
							fclose(f);
							fclose(f2);
							break;
						}
						if(strcmp(usr, myname) != 0)
							fprintf(f2, "%s %s\n", usr, typer);
					}
					f = fopen(pathtoreac, "w");
					f2 = fopen(pathtoreac2, "r");
					while(1)
					{
						if(fscanf(f2,"%s %s", usr, typer) == EOF)
						{
							fclose(f);
							fclose(f2);
							break;
						}
						fprintf(f, "%s %s\n", usr, typer);
					}
					unlink(pathtoreac2);
					view_post(path, filename, cl);
					break;
				}
				else
				{
					strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mComment has been deleted\033[0m");
					if(write(cl, msg2, sizeof(msg2)) <= 0)
						perror("[server]Eroare la write() catre client");
					break;
				}
			}
			else if(strcmp(readoption, "n") == 0)
			{
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mReaction deletion cancelled\033[0m");
				if(write(cl, msg2, sizeof(msg2)) <= 0)
					perror("[server]Eroare la write() catre client");
				break;
			}
			else
			{
				strcpy(msg2, "\033[1;92m[Reaction] \033[0;92mInvalid option\033[0m");
				if(write(cl, msg2, sizeof(msg2)) <= 0)
					perror("[server]Eroare la write() catre client");
			}
		}
	}
	pthread_mutex_lock(&lock_update_online);
	online[id].busy = 0;
	pthread_mutex_unlock(&lock_update_online);
}

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */

int main ()
{
  	struct sockaddr_in server;	// structura folosita de server
  	struct sockaddr_in from;	
  	int sd;		//descriptorul de socket 	
	pthread_t th[100];    //Identificatorii thread-urilor care se vor crea

  	/* crearea unui socket */
  	if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
    	perror ("[server]Eroare la socket().\n");
      	return errno;
    }
  	/* utilizarea optiunii SO_REUSEADDR */
  	int on=1;
  	setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
  
  	/* pregatirea structurilor de date */
  	bzero (&server, sizeof (server));
  	bzero (&from, sizeof (from));
  
  	/* umplem structura folosita de server */
  	/* stabilirea familiei de socket-uri */
    server.sin_family = AF_INET;	
  	/* acceptam orice adresa */
    server.sin_addr.s_addr = htonl (INADDR_ANY);
  	/* utilizam un port utilizator */
    server.sin_port = htons (PORT);
  
  	/* atasam socketul */
  	if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1)
  	{
      	perror ("[server]Eroare la bind().\n");
      	return errno;
    }

  	/* punem serverul sa asculte daca vin clienti sa se conecteze */
  	if (listen (sd, 2) == -1)
    {
      	perror ("[server]Eroare la listen().\n");
      	return errno;
    }
  	/* servim in mod concurent clientii...folosind thread-uri */
  	while (1)
    {
      	int client;
      	thData * td; //parametru functia executata de thread     
	  	unsigned int length = sizeof (from);

      	printf ("[server]Asteptam la portul %d...\n",PORT);
      	fflush (stdout);

      	/* acceptam un client (stare blocanta pina la realizarea conexiunii) */
      	if ( (client = accept (sd, (struct sockaddr *) &from, &length)) < 0)
	  	{
	  		perror ("[server]Eroare la accept().\n");
	  		continue;
	  	}

        /* s-a realizat conexiunea, se astepta mesajul */
		int assignedid = -1;
		for(int i = 0; i < MAXONLINE; i++)
			if(online[i].isonline == 0)
			{
				pthread_mutex_lock(&lock_update_online);
				assignedid = i;
				online[i].isonline = 1;
				pthread_mutex_unlock(&lock_update_online);
				break;
			}
		if(assignedid != -1)
		{
			td=(struct thData*)malloc(sizeof(struct thData));	
			td->idThread = assignedid;
			td->cl = client;

			pthread_create(&th[assignedid], NULL, &treat, td);	      
			pthread_detach(th[assignedid]);
		}
		else
		{
			char msg[1000] = "\033[1;91m[Server] \033[0;91mServer is full. Please try again later.\033[0m";
			if( write(client, msg, sizeof(msg)) <= 0)
				perror("Eroare la write() la respingerea la conectare");
			strcpy(msg, "/exit");
			if( write(client, msg, sizeof(msg)) <= 0)
				perror("Eroare la write() la respingerea la conectare");
		}	
	}//while    
};
		
static void *treat(void * arg)
{		
	int loggedIn = 0, admin = 0;
	struct thData tdL; 
	tdL= *((struct thData*)arg);	
	
	/* s-a realizat conexiunea, se astepta mesajul */
    printf ("[thread]- %d - Asteptam mesajul...\n", tdL.idThread);
	fflush (stdout);

	/* citirea mesajului */
	while(1)
    {
		char input[1000] = {0}, output[1000] = {0}, lines[1000][1000] = {0};
		int size;
		
		pthread_mutex_lock(&lock_update_online);
		if(loggedIn && online[tdL.idThread].busy == 0)
		{
			char tmp[1000] = "serverdata/users/";
			strcat(tmp, online[tdL.idThread].username);
			strcat(tmp,"/notifications.txt");

			FILE* ft = fopen(tmp, "r");
			fseek(ft, 0, SEEK_END);
			int filesize = ftell(ft);
			fclose(ft);
			if(filesize > 0)
			{
				char tmp2[1000];
				strcpy(tmp2, "\033[1;92m[Notifications] \033[0;94m");
				strcat(tmp2, online[tdL.idThread].username);
				strcat(tmp2, "\033[0;92m here's some stuff you've missed:\n\033[0m");
				char tmp3[1000] = "\033[0;92m--------------------------------------------------";
				for(int i = 0; i < strlen(online[tdL.idThread].username); i++)
					strcat(tmp3, "-");
				strcat(tmp3, "\033[0m");
				strcat(tmp2, tmp3);
				if(write( tdL.cl, tmp2, sizeof(tmp2)) <= 0)
					perror("[server]Eroare la write() catre client.\n");
				char buffer[1000];
				FILE *f = fopen(tmp, "r");
				while (fgets(buffer, sizeof(buffer), f))
				{
					buffer[strlen(buffer)-1] = 0;
					if(write( tdL.cl, buffer, sizeof(buffer)) <= 0)
						perror("[server]Eroare la write() catre client.\n");
				}
				fclose(f);
				if(write( tdL.cl, tmp3, sizeof(tmp3)) <= 0)
					perror("[server]Eroare la write() catre client.\n");
				f = fopen(tmp, "w");
				fclose(f);
			}
		}
		pthread_mutex_unlock(&lock_update_online);

		if (read (tdL.cl, input, sizeof(input)) <= 0)
		{
	  		perror ("[server]Eroare la read() de la client.\n");
	  		continue;		/* continuam sa ascultam */
		}
				
		printf("Am primit %s\n", input);
		fflush(stdout);

		size = split(input, lines, " ");

		if(strcmp(lines[0], "/exit") == 0)
		{
			if (write (tdL.cl, lines[0], sizeof(lines[0])) <= 0)
			{
				perror ("[server]Eroare la write() catre client.\n");
	 			continue;		/* continuam sa ascultam */
			}
			pthread_mutex_lock(&lock_update_online);
			online[tdL.idThread].isonline = 0;
			online[tdL.idThread].fd = 0;
			online[tdL.idThread].id = 0;
			online[tdL.idThread].busy = 0;
			memset(online[tdL.idThread].username, 0, sizeof(online[tdL.idThread].username));
			pthread_mutex_unlock(&lock_update_online);
			break;
		}
		else if(strcmp(lines[0], "/create") == 0 && (loggedIn == 0 || admin == 1))
		{
			char username[64], password[64];
			strcpy(output, "\033[1;92m[Account] \033[0;92mPlease enter your desired username\033[0m");
			if (write (tdL.cl, output, sizeof(output)) <= 0)
			{
				perror ("[server]Eroare la write() catre client.\n");
	 			continue;		/* continuam sa ascultam */
			}
			bzero(input,1000);
			if (read (tdL.cl, input, sizeof(input)) <= 0)
			{
	  			perror ("[server]Eroare la read() de la client.\n");
	  			continue;		/* continuam sa ascultam */
			}
			if(strcmp(input,"/exit") == 0)
			{
				strcpy(output, "\033[1;92m[Account] \033[0;92mAccount creation cancelled\033[0m");
				if (write (tdL.cl, output, sizeof(output)) <= 0)
				{
					perror ("[server]Eroare la write() catre client.\n");
	 				continue;		/* continuam sa ascultam */
				}
				break;
			}
			else strcpy(username, input);
				
			if(strcmp(input,"/exit") != 0)
			{
				strcpy(output, "\033[1;92m[Account] \033[0;92mPlease enter your password\033[0m");
				if (write (tdL.cl, output, sizeof(output)) <= 0)
				{
					perror ("[server]Eroare la write() catre client.\n");
	 				continue;		/* continuam sa ascultam */
				}
				bzero(input, 1000);
				if (read (tdL.cl, input, sizeof(input)) <= 0)
				{
	  				perror ("[server]Eroare la read() de la client.\n");
	  				continue;		/* continuam sa ascultam */
				}
				if(strcmp(input,"/exit") == 0)
				{
					strcpy(output, "\033[1;92m[Account] \033[0;92mAccount creation cancelled\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
					{
						perror ("[server]Eroare la write() catre client.\n");
	 					continue;		/* continuam sa ascultam */
					}
				}
				if(strcmp(input,"/exit") != 0)
				{
					strcpy(password, input);
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char user[64], pass[64], id[64];						
					int found = 0;					
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(username, user) == 0)
						{
							found = 1;
							fclose(f);
							break;
						}
					}
					if(found)
					{
						strcpy(output, "\033[1;92m[Account] \033[0;92mUsername already exists! Please try again\033[0m");
						if (write (tdL.cl, output, sizeof(output)) <= 0)
						{
							perror ("[server]Eroare la write() catre client.\n");
	 						continue;		/* continuam sa ascultam */
						}
					}
					else
					{
						int success = 0;
						if(admin == 1 && strcmp(lines[1], "admin") == 0)
						{
							success = 1;
							f = fopen("serverdata/accounts.txt", "a");
							fprintf(f, "%s %s %s\n", lines[1], username, password);
							fclose(f);
						}
						else if(admin == 0 && lines[1][0] == 0)
						{
							success = 1;
							int nr = atoi(id); nr++;
							char buffer[64]; snprintf(buffer, 64, "%d", nr);
							f = fopen("serverdata/accounts.txt", "a");
							fprintf(f, "%s %s %s\n", buffer, username, password);
							fclose(f);
						}
						else if(admin == 0 && strcmp(lines[1], "admin") == 0)
						{
							strcpy(output, "\033[1;92m[Account] \033[0;92mYou are not an admin!\033[0m");
							if (write (tdL.cl, output, sizeof(output)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
						}
						else
						{
							strcpy(output, "\033[1;92m[Account] \033[0;92mInvalid create arguments\033[0m");
							if (write (tdL.cl, output, sizeof(output)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
						}
						if(success)
						{
							strcpy(output, "\033[1;92m[Account] \033[0;92mAccount created succesfully\033[0m");
							if (write (tdL.cl, output, sizeof(output)) <= 0)
							{
								perror ("[server]Eroare la write() catre client.\n");
			 						continue;		/* continuam sa ascultam */
							}
							char tmp[1000] = "serverdata/users/";
							strcat(tmp, username);
							mkdir(tmp, 0700);
							strcat(tmp, "/notifications.txt");
							int notfile = open(tmp, O_RDWR | O_CREAT, 0700);
							close(notfile);
							strcpy(tmp, "serverdata/users/");
							strcat(tmp, username);
							strcat(tmp, "/friends.txt");
							notfile = open(tmp, O_RDWR | O_CREAT, 0700);
							close(notfile);
							strcpy(tmp, "serverdata/users/");
							strcat(tmp, username);
							strcat(tmp, "/profile*public");
							notfile = open(tmp, O_RDWR | O_CREAT, 0700);
							close(notfile);
							strcpy(tmp, "serverdata/users/");
							strcat(tmp, username);
							strcat(tmp, "/friendrequests.txt");
							notfile = open(tmp, O_RDWR | O_CREAT, 0700);
							close(notfile);
							strcpy(tmp, "serverdata/users/");
							strcat(tmp, username);
							strcat(tmp, "/chats");
							mkdir(tmp, 0700);
							strcpy(tmp, "serverdata/users/");
							strcat(tmp, username);
							strcat(tmp, "/posts");
							mkdir(tmp, 0700);
						}
					}
				}
			}
		}
		else if(strcmp(lines[0], "/login") == 0 && loggedIn == 0)
		{
			char id[64], username[64], password[64];
			FILE* f = fopen("serverdata/accounts.txt", "r");
			while(1)
			{
				if(fscanf(f,"%s %s %s",id, username, password) == EOF)
				{
					fclose(f);
					break;
				}
				if(strcmp(username,lines[1]) == 0 && strcmp(password, lines[2]) == 0 && lines[2][0] != 0)
				{
					if(strcmp(id, "admin") == 0)admin = 1;
					loggedIn = 1;
					fclose(f);
					break;
				}	
			}
			if(loggedIn)					
			{
				if(admin == 1)
				{
					strcpy(output, "\033[0;92mLogged in! Welcome \033[0;91m[admin] ");
					strcat(output, lines[1]);
					strcat(output, "\033[0m");
				}
				else
				{
					strcpy(output, "\033[0;92mLogged in! Welcome \033[0m");
					strcat(output, lines[1]);
				}
				if (write (tdL.cl, output, sizeof(output)) <= 0)
				{
					perror ("[server]Eroare la write() catre client.\n");
	 				continue;		/* continuam sa ascultam */
				}
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].fd = tdL.cl;
				strcpy(online[tdL.idThread].username, username);
				online[tdL.idThread].id = atoi(id);
				pthread_mutex_unlock(&lock_update_online);
			}
			else
			{
				strcpy(output, "\033[0;92mLogin failed! Incorrect username or password\033[0m");
				if (write (tdL.cl, output, sizeof(output)) <= 0)
				{
					perror ("[server]Eroare la write() catre client.\n");
	 				continue;		/* continuam sa ascultam */
				}
			}
		}
		else if(strcmp(lines[0], "/chat") == 0 && loggedIn)
		{
			char msg[1000] = {0}, tmpline[100];
			int roomid = -1;
			for(int i = 1; i <= size; i++)
				for(int j = i+1; j <= size; j++)
					if(strcmp(lines[i],lines[j]) > 0)
					{
						strcpy(tmpline, lines[i]);
						strcpy(lines[i],lines[j]);
						strcpy(lines[j],tmpline);
					}
			for(int i = 0; i < MAXCHATROOMS; i++)
				if(chatrooms[i].free == 0)
				{
					roomid = i;
					break;
				}
			if(roomid != -1)
			{
				int roompoz = -1, exited = 0;
				strcpy(msg, "\033[1;92m[Chat] \033[0;92mPlease enter the room name:\033[0m");
				if(write(tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client.\n");

				while(1)
				{
					bzero(msg, 1000);
					if (read (tdL.cl, msg, sizeof(msg)) <= 0)
	  					perror ("[server]Eroare la read() de la client.\n");
					if(msg[0] == 0)break;
					if(strcmp(msg, "/exit") == 0)
					{
						strcpy(msg, "\033[1;92m[Chat] \033[0;92mRoom creation cancelled.\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
						exited = 1;
						break;
					}
					else break;
				}
				if(exited == 0)
				{
					char chatfilename[1000] = {0};

					pthread_mutex_lock(&lock_update_chatrooms);
					if(msg[0] == 0)
					{
						char buffer[100];
						snprintf(buffer, 100, "%d", roomid);
						strcpy(chatrooms[roomid].name, buffer);
					}
					else strcpy(chatrooms[roomid].name, msg);

					strcpy(chatfilename, chatrooms[roomid].name);
					chatrooms[roomid].free = 1;
					chatrooms[roomid].nrinvited = 0;
					strcpy(chatrooms[roomid].invited[chatrooms[roomid].nrinvited++], online[tdL.idThread].username);
					char chatfilenameowner[1000];
					strcpy(chatfilenameowner, chatfilename);
					
					for(int i = 1; i <= size; i++)
						{
							strcpy(chatrooms[roomid].invited[chatrooms[roomid].nrinvited++], lines[i]);
							strcat(chatfilenameowner, "*");
							strcat(chatfilenameowner, lines[i]);
						}

					for(int i = 0; i < chatrooms[roomid].nrinvited; i++)
						for(int j = i+1; j < chatrooms[roomid].nrinvited; j++)
							if(strcmp(chatrooms[roomid].invited[i],chatrooms[roomid].invited[j]) > 0)
							{
								char tmpline[1000];
								strcpy(tmpline,chatrooms[roomid].invited[i]);
								strcpy(chatrooms[roomid].invited[i],chatrooms[roomid].invited[j]);
								strcpy(chatrooms[roomid].invited[j],tmpline);
							}

					char pathowner[1000] = "serverdata/users/";
					pthread_mutex_lock(&lock_update_online);
					strcat(pathowner, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(pathowner, "/chats/");
					strcat(pathowner, chatfilenameowner);
					FILE *fileowner = fopen(pathowner, "a");
					fclose(fileowner);
					strcpy(chatrooms[roomid].files[chatrooms[roomid].nrfiles++], pathowner);

					for(int i = 0; i < MAXROOMCAPACITY; i++)
						if(chatrooms[roomid].inside[i] == 0)
						{
							roompoz = i;
							chatrooms[roomid].inside[roompoz] = tdL.cl;
							break;
						}
					pthread_mutex_unlock(&lock_update_chatrooms);

					strcpy(msg, "\033[1;92m[Chat] \033[0;94m");
					pthread_mutex_lock(&lock_update_online);
					strcat(msg, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);

					pthread_mutex_lock(&lock_update_chatrooms);
					strcat(msg, " \033[0;92mcreated a chat \033[0;94m[");
					strcat(msg, chatrooms[roomid].name);
					strcat(msg, "] \033[0;92mwith you");
					pthread_mutex_unlock(&lock_update_chatrooms);

					pthread_mutex_lock(&lock_update_online);
					for(int i = 1; i <= size; i++)
					{	
						int foundonline = 0;
						for(int j = 0; j < MAXONLINE; j++)
						if(strcmp(online[j].username, lines[i]) == 0)
						{
							char output[1000] = {0}, chatfilename2[1000] = {0}, tmplines[100][100], tmpline[100];
							int tmpsize = 0;							
							strcpy(output, msg);
							strcpy(chatfilename2, chatfilename);
							strcpy(tmplines[0], online[tdL.idThread].username);
							for(int k = 1; k <= size; k++)
								if(strcmp(online[j].username, lines[k]) != 0)
								{
									strcat(output, " and ");
									strcat(output, "\033[0;94m[");
									strcat(output, lines[k]);
									strcat(output, "]\033[0;92m");
									strcpy(tmplines[++tmpsize], lines[k]);
								}
							strcat(output, ".\033[0m");
							for(int k = 0; k <= tmpsize; k++)
								for(int l = k+1; l <= tmpsize; l++)
								if(strcmp(tmplines[k],tmplines[l]) > 0)
								{
									strcpy(tmpline, tmplines[k]);
									strcpy(tmplines[k],tmplines[l]);
									strcpy(tmplines[l],tmpline);
								}
							for(int k = 0; k <= tmpsize; k++)
							{
								strcat(chatfilename2, "*");
								strcat(chatfilename2, tmplines[k]);
							}
							char path[1000] = "serverdata/users/";
							strcat(path, online[j].username);
							strcat(path, "/chats/");
							strcat(path, chatfilename2);
							FILE *file = fopen(path, "a");
							fclose(file);
							pthread_mutex_lock(&lock_update_chatrooms);
							strcpy(chatrooms[roomid].files[chatrooms[roomid].nrfiles++], path);
							pthread_mutex_unlock(&lock_update_chatrooms);
							
							if(online[j].busy == 0) write(online[j].fd, output, strlen(output));
							else
							{
								char tmp[1000] = "serverdata/users/";
								strcat(tmp, online[j].username);
								strcat(tmp, "/notifications.txt");
								FILE *f = fopen(tmp, "a");
								fprintf(f, "%s\n", output);
								fclose(f); 
							}
							foundonline = 1;
							break;
						}
						if(foundonline == 0)
						{
							int found = 0;
							char id[64], user[64], pass[64];
							FILE* f = fopen("serverdata/accounts.txt", "r");
							while(1)
							{
								if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
								{
									fclose(f);
									break;
								}
								if(strcmp(lines[i], user) == 0)
								{
									found = 1;
									fclose(f);
									break;
								}
							}
							if(found)
							{
								char output[1000] = {0}, chatfilename2[1000] = {0}, tmplines[100][100], tmpline[100];
								int tmpsize = 0;
								strcpy(output, msg);
								strcpy(chatfilename2, chatfilename);
								strcpy(tmplines[0], online[tdL.idThread].username);
								for(int k = 1; k <= size; k++)
								if(strcmp(lines[i], lines[k]) != 0)
								{
									strcat(output, " and ");
									strcat(output, "\033[0;94m[");
									strcat(output, lines[k]);
									strcat(output, "]\033[0;92m");
									strcpy(tmplines[++tmpsize], lines[k]);
								}
								strcat(output, ".\033[0m");
								for(int k = 0; k <= tmpsize; k++)
									for(int l = k+1; l <= tmpsize; l++)
									if(strcmp(tmplines[k],tmplines[l]) > 0)
									{
										strcpy(tmpline, tmplines[k]);
										strcpy(tmplines[k],tmplines[l]);
										strcpy(tmplines[l],tmpline);
									}
								for(int k = 0; k <= tmpsize; k++)
								{
									strcat(chatfilename2, "*");
									strcat(chatfilename2, tmplines[k]);
								}
								char path[1000] = "serverdata/users/";
								strcat(path, lines[i]);
								strcat(path, "/chats/");
								strcat(path, chatfilename2);
								FILE *file = fopen(path, "a");
								fclose(file);
								pthread_mutex_lock(&lock_update_chatrooms);
								strcpy(chatrooms[roomid].files[chatrooms[roomid].nrfiles++], path);
								pthread_mutex_unlock(&lock_update_chatrooms);

								char tmp[1000] = "serverdata/users/";
								strcat(tmp, lines[i]);
								strcat(tmp, "/notifications.txt");
								FILE *f = fopen(tmp, "a");
								fprintf(f, "%s\n", output);
								fclose(f); 
							}
						}			
					}
					pthread_mutex_unlock(&lock_update_online);

					pthread_mutex_lock(&lock_update_chatrooms);
					strcpy(msg, "\033[2J\033[H\033[1;92m[Chat] \033[0;94m[");
					strcat(msg, chatrooms[roomid].name);
					strcat(msg, "] \033[0;92mhas been created.\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la read() de la client.\n");
					pthread_mutex_unlock(&lock_update_chatrooms);
					for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
					{
						FILE* f = fopen(chatrooms[roomid].files[i], "a");
						fprintf(f, "%s\n", msg);
						fclose(f);
					}					

					pthread_mutex_lock(&lock_update_online);
					online[tdL.idThread].busy = 1;
					pthread_mutex_unlock(&lock_update_online);
					while(1)
					{
						bzero(msg, 1000);
						if (read (tdL.cl, msg, sizeof(msg)) <= 0)
		  					perror ("[server]Eroare la read() de la client.\n");
						if(strcmp(msg, "/exit") == 0)
						{
							strcpy(msg, "\033[2J\033[H\033[1;92m[Chat] \033[0;92mYou have left the chat.\033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() de la client.\n");
							strcpy(msg, "\033[1;92m[Chat] \033[0;94m[");
							pthread_mutex_lock(&lock_update_online);
							strcat(msg, online[tdL.idThread].username);
							pthread_mutex_unlock(&lock_update_online);
							strcat(msg, "] \033[0;92mhas left the chat.\033[0m");
							pthread_mutex_lock(&lock_update_chatrooms);
							for(int i = 0; i < MAXROOMCAPACITY; i++)
								if(i != roompoz && chatrooms[roomid].inside[i] != 0)
								{
									if(write (chatrooms[roomid].inside[i], msg, sizeof(msg)) <= 0)
										perror("[chat]Eroare la write in /chat\n");
								}
							for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
							{
								FILE* f;
								if( (f  = fopen(chatrooms[roomid].files[i], "r")) )
								{
									fclose(f);
									f = fopen(chatrooms[roomid].files[i], "a");
									fprintf(f, "%s\n", msg);
									fclose(f);
								}
							}
							chatrooms[roomid].inside[roompoz] = 0;
							pthread_mutex_unlock(&lock_update_chatrooms);
							pthread_mutex_lock(&lock_update_online);
							online[tdL.idThread].busy = 0;
							pthread_mutex_unlock(&lock_update_online);						
							break;
						}
						else
						{
							pthread_mutex_lock(&lock_update_chatrooms);
							for(int i = 0; i < MAXROOMCAPACITY; i++)
								if(i != roompoz && chatrooms[roomid].inside[i] != 0)
								{
									char msg2[1000];
									pthread_mutex_lock(&lock_update_online);
									strcpy(msg2, online[tdL.idThread].username);
									pthread_mutex_unlock(&lock_update_online);									
									strcat(msg2, ": ");
									strcat(msg2, msg);
									if(write (chatrooms[roomid].inside[i], msg2, sizeof(msg2)) <= 0)
										perror("[chat]Eroare la write in /chat\n");
								}
							for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
							{
								char msg2[1000];
								pthread_mutex_lock(&lock_update_online);
								strcpy(msg2, online[tdL.idThread].username);
								pthread_mutex_unlock(&lock_update_online);
								strcat(msg2, ": ");
								strcat(msg2, msg);
								FILE* f;
								if( (f  = fopen(chatrooms[roomid].files[i], "r")) )
								{
									fclose(f);
									f = fopen(chatrooms[roomid].files[i], "a");
									fprintf(f, "%s\n", msg2);
									fclose(f);
								}
							}
							pthread_mutex_unlock(&lock_update_chatrooms);
						}
					}
					pthread_mutex_lock(&lock_update_chatrooms);
					chatrooms[roomid].inside[roompoz] = 0;
					int tobefreed = 1;
					for(int i = 0; i < MAXROOMCAPACITY; i++)
						if(chatrooms[roomid].inside[i] != 0) tobefreed = 0;
					pthread_mutex_unlock(&lock_update_chatrooms);				
					if(tobefreed)
					{
						pthread_mutex_lock(&lock_update_chatrooms);
						strcpy(msg, "\033[1;92m[Chat] \033[0;92mEverybody has left the chat. \033[0;94m[");
						strcat(msg, chatrooms[roomid].name);		
						strcat(msg, "] \033[0;92mclosed.\033[0m");
						for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
						{
							FILE* f;
							if( (f  = fopen(chatrooms[roomid].files[i], "r")) )
							{
								fclose(f);
								f = fopen(chatrooms[roomid].files[i], "a");
								fprintf(f, "%s\n", msg);
								fclose(f);
							}
						}
						for(int i = 0; i < chatrooms[roomid].nrinvited; i++)
							send_notification(chatrooms[roomid].invited[i], msg);
						chatrooms[roomid].free = 0;
						chatrooms[roomid].nrinvited = 0;
						chatrooms[roomid].nrfiles = 0;
						memset(chatrooms[roomid].name, 0, sizeof(chatrooms[roomid].name));
						memset(chatrooms[roomid].invited, 0, sizeof(chatrooms[roomid].invited));
						memset(chatrooms[roomid].files, 0, sizeof(chatrooms[roomid].files));
						pthread_mutex_unlock(&lock_update_chatrooms);				
					}
				}
			}
			else
			{
				bzero(msg, 1000);
				strcpy(msg, "\033[1;94m[Chat]\033[0;94m]No chat rooms available. Please try again later.\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la read() de la client.\n");
			}
		}
		else if(strcmp(lines[0], "/join") == 0 && loggedIn)
		{
			int allowed = 0, roompoz = -1, roomid = -1;
			char msg[1000] = {0};

			pthread_mutex_lock(&lock_update_chatrooms);
			char chatname[1000];
			strcpy(chatname, lines[1]);
			for(int i = 2; i <= size; i++)
			{
				strcat(chatname, " ");
				strcat(chatname, lines[i]);
			}
			int count = 0;
			int roomcollisions[MAXCHATROOMS];
			for(int i = 0; i < MAXCHATROOMS; i++)
				if(strcmp(chatrooms[i].name, chatname) == 0)
				{
					roomid = i;
					roomcollisions[++count] = i;
				}
			if(count > 1)
			{
				strcpy(msg, "\033[1;92m[Chat] \033[0;92mMultiple rooms with the same name have been found:\n---------------------------------------------------\033[0m");
				if(write( tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client.\n");
				for(int i = 1; i <= count; i++)
				{
					char buffer[100];
					snprintf(buffer, 100, "%d", i);
					strcpy(msg, "\033[1;94m[");
					strcat(msg, buffer);
					strcat(msg, "] \033[0;92mChat \033[0;94m[");
					strcat(msg, chatrooms[roomcollisions[i]].name);
					strcat(msg, "] \033[0;92mwith");
					int ok = 0;
					pthread_mutex_lock(&lock_update_online);
					for(int j = 0; j < chatrooms[roomid].nrinvited; j++)
						if(strcmp(online[tdL.idThread].username, chatrooms[roomcollisions[i]].invited[j]) != 0)
						{
							if(ok == 0)
							{
								strcat(msg, " \033[0;94m[");
								strcat(msg, chatrooms[roomcollisions[i]].invited[j]);
								strcat(msg, "]");
								ok = 1;
							}
							else
							{
								strcat(msg, "\033[0;92m and\033[0;94m[");
								strcat(msg, chatrooms[roomcollisions[i]].invited[j]);
								strcat(msg, "]");
							}
						}
					strcat(msg, "\033[0;92m.\033[0m");
					pthread_mutex_unlock(&lock_update_online);
					if(write( tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client.\n");
				}
				strcpy(msg, "\033[0;92m---------------------------------------------------\n\033[1;92m[Chat] \033[0;92mPlease enter the room number you want to join:\033[0m");
				if(write( tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client.\n");
				while(1)
				{
					char readline[1000];
					int nroption;
					if(read( tdL.cl, readline, sizeof(readline)) <= 0)
						perror("[server]Eroare la read() de la client.\n");
					nroption = atoi(readline);
					if(strcmp(readline, "/exit") == 0)
					{
						roomid = -1;
						break;
					}
					else if(nroption >=1 && nroption <= count)
					{
						roomid = roomcollisions[nroption];
						break;
					}
					else
					{
						strcpy(msg, "\033[0;92mInvalid number of a room. Please enter a valid number:\033[0m");
						if(write( tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
					}
				}
			}
			pthread_mutex_unlock(&lock_update_chatrooms);

			pthread_mutex_lock(&lock_update_chatrooms);
			if(roomid != -1)
			{
				pthread_mutex_lock(&lock_update_online);
				for(int i = 0; i < chatrooms[roomid].nrinvited; i++)
					if(strcmp(online[tdL.idThread].username, chatrooms[roomid].invited[i]) == 0)
					{
						allowed = 1;
						break;
					}
				pthread_mutex_unlock(&lock_update_online);
			}
			pthread_mutex_unlock(&lock_update_chatrooms);

			if(allowed)
			{
				char path[1000] = "serverdata/users/";
				pthread_mutex_lock(&lock_update_online);
				strcat(path, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(path, "/chats/");
				strcat(path, chatname);

				pthread_mutex_lock(&lock_update_chatrooms);
				for(int i = 0; i < chatrooms[roomid].nrinvited; i++)
				{
					pthread_mutex_lock(&lock_update_online);
					if(strcmp(chatrooms[roomid].invited[i], online[tdL.idThread].username) != 0)
					{
						strcat(path, "*");
						strcat(path, chatrooms[roomid].invited[i]);
					}
					pthread_mutex_unlock(&lock_update_online);
				}
				pthread_mutex_unlock(&lock_update_chatrooms);

				int filesize = 0;
				FILE* ft;
				if( (ft = fopen(path, "r") ) )
				{
					fclose(ft);
					FILE* ft = fopen(path, "r");
					fseek(ft, 0, SEEK_END);
					filesize = ftell(ft);
					fclose(ft);
								
					if(filesize > 0)
					{	
						char buffer[1000];
						FILE *f = fopen(path, "r");
						while (fgets(buffer, sizeof(buffer), f))
						{
							buffer[strlen(buffer)-1] = 0;
							if(write( tdL.cl, buffer, sizeof(buffer)) <= 0)
								perror("[server]Eroare la write() catre client.\n");
						}
						fclose(f);
					}				
				}

				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 1;
				pthread_mutex_unlock(&lock_update_online);
				pthread_mutex_lock(&lock_update_chatrooms);
				for(int i = 0; i < MAXROOMCAPACITY; i++)
					if(chatrooms[roomid].inside[i] == 0)
					{
						roompoz = i;
						chatrooms[roomid].inside[roompoz] = tdL.cl;
						break;
					}
				pthread_mutex_unlock(&lock_update_chatrooms);
				if(filesize == 0)strcpy(msg, "\033[2J\033[H\033[1;92m[Chat] \033[0;92mYou have joined the chat.\033[0m");
					else strcpy(msg, "\033[1;92m[Chat] \033[0;92mYou have joined the chat.\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la read() de la client.\n");
				strcpy(msg, "\033[1;92m[Chat] \033[0;94m[");
				pthread_mutex_lock(&lock_update_online);
				strcat(msg, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(msg, "] \033[0;92mhas joined the chat.\033[0m");
				pthread_mutex_lock(&lock_update_chatrooms);
				for(int i = 0; i < MAXROOMCAPACITY; i++)
					if(i != roompoz && chatrooms[roomid].inside[i] != 0)
					{
						if(write (chatrooms[roomid].inside[i], msg, sizeof(msg)) <= 0)
							perror("[chat]Eroare la write in /chat\n");
					}
				pthread_mutex_unlock(&lock_update_chatrooms);
				for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
				{
					FILE* f;
					if( (f  = fopen(chatrooms[roomid].files[i], "r")) )
					{
						fclose(f);
						f = fopen(chatrooms[roomid].files[i], "a");
						fprintf(f, "%s\n", msg);
						fclose(f);
					}
				}

				while(1)
				{
					bzero(msg, 1000);
					if (read (tdL.cl, msg, sizeof(msg)) <= 0)
	  					perror ("[server]Eroare la read() de la client.\n");
					if(strcmp(msg, "/exit") == 0)
					{
						strcpy(msg, "\033[2J\033[H\033[1;92m[Chat] \033[0;92mYou have left the chat.\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() de la client.\n");
						strcpy(msg, "\033[1;92m[Chat] \033[0;94m[");
						pthread_mutex_lock(&lock_update_online);
						strcat(msg, online[tdL.idThread].username);
						pthread_mutex_unlock(&lock_update_online);
						strcat(msg, "] \033[0;92mhas left the chat.\033[0m");
						pthread_mutex_lock(&lock_update_chatrooms);
						for(int i = 0; i < MAXROOMCAPACITY; i++)
							if(i != roompoz && chatrooms[roomid].inside[i] != 0)
							{
								if(write (chatrooms[roomid].inside[i], msg, sizeof(msg)) <= 0)
									perror("[chat]Eroare la write in /chat\n");
							}
						for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
						{
							FILE* f;
							if( (f  = fopen(chatrooms[roomid].files[i], "r")) )
							{
								fclose(f);
								f = fopen(chatrooms[roomid].files[i], "a");
								fprintf(f, "%s\n", msg);
								fclose(f);
							}
						}
						chatrooms[roomid].inside[roompoz] = 0;
						pthread_mutex_unlock(&lock_update_chatrooms);
						pthread_mutex_lock(&lock_update_online);
						online[tdL.idThread].busy = 0;
						pthread_mutex_unlock(&lock_update_online);
						break;
					}
					else
					{
						pthread_mutex_lock(&lock_update_chatrooms);
						for(int i = 0; i < MAXROOMCAPACITY; i++)
							if(i != roompoz && chatrooms[roomid].inside[i] != 0)
							{
								char msg2[1000];
								pthread_mutex_lock(&lock_update_online);
								strcpy(msg2, online[tdL.idThread].username);
								pthread_mutex_unlock(&lock_update_online);									
								strcat(msg2, ": ");
								strcat(msg2, msg);
								if(write (chatrooms[roomid].inside[i], msg2, sizeof(msg2)) <= 0)
									perror("[chat]Eroare la write in /chat\n");
							}
						for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
						{
							char msg2[1000];
							pthread_mutex_lock(&lock_update_online);
							strcpy(msg2, online[tdL.idThread].username);
							pthread_mutex_unlock(&lock_update_online);
							strcat(msg2, ": ");
							strcat(msg2, msg);
							FILE* f;
							if( (f  = fopen(chatrooms[roomid].files[i], "r")) )
							{
								fclose(f);
								f = fopen(chatrooms[roomid].files[i], "a");
								fprintf(f, "%s\n", msg2);
								fclose(f);
							}
						}
						pthread_mutex_unlock(&lock_update_chatrooms);
					}
				}
				pthread_mutex_lock(&lock_update_chatrooms);
				chatrooms[roomid].inside[roompoz] = 0;
				int tobefreed = 1;
				for(int i = 0; i < MAXROOMCAPACITY; i++)
					if(chatrooms[roomid].inside[i] != 0) tobefreed = 0;
				pthread_mutex_unlock(&lock_update_chatrooms);				
				if(tobefreed)
				{
					pthread_mutex_lock(&lock_update_chatrooms);
					strcpy(msg, "\033[1;92m[Chat] \033[0;92mEverybody has left the chat. \033[0;94m[");
					strcat(msg, chatrooms[roomid].name);		
					strcat(msg, "] \033[0;92mclosed.\033[0m");			
					for(int i = 0; i < chatrooms[roomid].nrfiles; i++)
					{
						FILE* f;
						if( (f  = fopen(chatrooms[roomid].files[i], "r")) )
						{
							fclose(f);
							f = fopen(chatrooms[roomid].files[i], "a");
							fprintf(f, "%s\n", msg);
							fclose(f);
						}
					}	
					for(int i = 0; i < chatrooms[roomid].nrinvited; i++)
						send_notification(chatrooms[roomid].invited[i], msg);
					chatrooms[roomid].free = 0;
					chatrooms[roomid].nrinvited = 0;
					chatrooms[roomid].nrfiles = 0;
					memset(chatrooms[roomid].name, 0, sizeof(chatrooms[roomid].name));
					memset(chatrooms[roomid].invited, 0, sizeof(chatrooms[roomid].invited));
					memset(chatrooms[roomid].files, 0, sizeof(chatrooms[roomid].files));
					pthread_mutex_unlock(&lock_update_chatrooms);				
				}
			}
			else
			{
				strcpy(msg, "\033[1;92m[Chat] \033[0;92mYou have not been invited to this chat.\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la read() de la client.\n");
			}
		}
		else if(strcmp(lines[0], "/view") == 0)
		{
			if(strcmp(lines[1], "chats") == 0 && loggedIn)
			{
				char path[1000] = "serverdata/users/";
				int userexists = 1;
				if(lines[2][0] != 0 && admin == 1)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char user[64], pass[64], id[64];									
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], user) == 0)
						{
							userexists = 1;
							strcat(path, lines[2]);
							strcat(path, "/chats");
							fclose(f);
							break;
						}
					}
				}
				else
				{
					pthread_mutex_lock(&lock_update_online);
					strcat(path, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(path, "/chats");
				}

				if(userexists)
				{
					DIR *directory;
					directory = opendir(path);
					struct dirent *currentfile;

					int count = 0;
					char msg[1000] = "\033[1;92m[Chat] \033[0;92mList of your current chats:\n----------------------------------\033[0m";
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client.\n");

	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							count++;
							char buffer[100] = {0}, msg2[1000] = "\033[1;94m[", name[1000] = {0}, names[1000][1000] = {0};
							int tmpsize = 0;
							strcpy(name, currentfile->d_name);
							tmpsize = split(name, names, "*");

							if(tmpsize > 0)
							{
								snprintf(buffer, 100, "%d", count);
								strcat(msg2, buffer);
								strcat(msg2, "] \033[0;92mChat \033[0;94m[");
								strcat(msg2, names[0]);
								strcat(msg2, "] \033[0;92mwith you");
								for(int i = 1; i <= tmpsize; i++)
								{
									strcat(msg2, " and \033[0;94m[");
									strcat(msg2, names[i]);
									strcat(msg2, "]\033[0;92m");
								}
								strcat(msg2, ".\033[0m");
								if(write( tdL.cl, msg2, sizeof(msg2)) <= 0)
									perror("[server]Eroare la write() catre client.\n");
							}
						}
					}
					closedir(directory);
					if(count == 0)
					{
						strcpy(msg, "\033[0;92mNo chats were found :(\033[0m");
						if(write( tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
					}
					strcpy(msg, "\033[0;92m----------------------------------\033[0m");
					if(write( tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client.\n");
				}
				else
				{
					char msg[1000] ="\033[1;92m[Chat] \033[0;92mUser doesn't exist\033[0m";
					if(write( tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1],"chat") == 0 && loggedIn)
			{
				char path[1000] = "serverdata/users/", chatname[1000] = {0}, msg[1000] = {0};
				int userexists = 0;
				if(admin == 1)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char user[64], pass[64], id[64];									
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], user) == 0)
						{
							userexists = 1;
							strcat(path, lines[2]);
							strcat(path, "/chats");
							fclose(f);
							strcpy(chatname, lines[3]);
							for(int i = 4; i <= size; i++)
							{
								strcat(chatname, " ");
								strcat(chatname, lines[i]);
							}
							break;
						}
					}
				}
				else
				{
					pthread_mutex_lock(&lock_update_online);
					strcat(path, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(path, "/chats");

					strcpy(chatname, lines[2]);
					for(int i = 3; i <= size; i++)
					{
						strcat(chatname, " ");
						strcat(chatname, lines[i]);
					}
				}

				if(userexists)
				{
					DIR *directory = opendir(path);
					struct dirent *currentfile;

					int countcollisions = 0;
					char foundcollisions[1000][1000];
	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							char name[1000] = {0}, names[1000][1000] = {0};
							strcpy(name,currentfile->d_name);
							split(name, names, "*");
							if(strcmp(chatname, names[0]) == 0)
								strcpy(foundcollisions[++countcollisions], currentfile->d_name);
						}
	   				}
					closedir(directory);
					if(countcollisions == 0)
					{
						strcpy(msg, "\033[1;92m[Chat]\033[0;92m Chat name doesn't exist.\033[0m");
						if(write( tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
					}
					else if(countcollisions == 1)
					{
						char tmp[1000];
						strcpy(tmp, path);
						strcat(tmp, "/");
						strcat(tmp, foundcollisions[1]);

						FILE* ft = fopen(tmp, "r");
						fseek(ft, 0, SEEK_END);
						int filesize = ftell(ft);
						fclose(ft);
									
						if(filesize > 0)
						{	
							char buffer[1000];
							FILE *f = fopen(tmp, "r");
							while (fgets(buffer, sizeof(buffer), f))
							{
								buffer[strlen(buffer)-1] = 0;
								if(write( tdL.cl, buffer, sizeof(buffer)) <= 0)
									perror("[server]Eroare la write() catre client.\n");
							}
							fclose(f);
						}
					}
					else
					{
						char msg[1000] = "\033[1;92m[Chat] \033[0;92mMultiple chats with the same name have been found:\n--------------------------------------------------------\033[0m";
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
						for(int i = 1; i <= countcollisions; i++)	
						{
							char buffer[100] = {0}, msg2[1000] = "\033[1;94m[", name[1000] = {0}, names[1000][1000] = {0};
							int tmpsize = 0;
							strcpy(name, foundcollisions[i]);
							tmpsize = split(name, names, "*");

							if(tmpsize > 0)
							{
								snprintf(buffer, 100, "%d", i);
								strcat(msg2, buffer);
								strcat(msg2, "] \033[0;92mChat \033[0;94m[");
								strcat(msg2, names[0]);
								strcat(msg2, "] \033[0;92mwith you");
								for(int i = 1; i <= tmpsize; i++)
								{
									strcat(msg2, " and \033[0;94m[");
									strcat(msg2, names[i]);
									strcat(msg2, "]\033[0;92m");
								}
								strcat(msg2, ".\033[0m");
								if(write( tdL.cl, msg2, sizeof(msg2)) <= 0)
									perror("[server]Eroare la write() catre client.\n");
							}
						}
						strcpy(msg,"\033[0;92m--------------------------------------------------------\n\033[1;92m[Chat] \033[0;92mPlease enter the number of the chat you want to open:\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
						while(1)
						{
							char readoption[1000];
							bzero(readoption, 1000);
							if(read(tdL.cl, readoption, sizeof(readoption)) <= 0)
								perror("[server]Eroare la read() de la client.\n");
							if(strcmp(readoption, "/exit") == 0)
							{
								strcpy(msg,"\033[1;92m[Chat] \033[0;92mViewing chat cancelled.\033[0m");
								if(write(tdL.cl, msg, sizeof(msg)) <= 0)
									perror("[server]Eroare la write() catre client.\n");
								break;
							}
							else
							{
								int nroption = atoi(readoption);
								if(nroption >= 1 && nroption <= countcollisions)
								{
									char tmp[1000];
									strcpy(tmp, path);
									strcat(tmp, "/");
									strcat(tmp, foundcollisions[nroption]);

									FILE* ft = fopen(tmp, "r");
									fseek(ft, 0, SEEK_END);
									int filesize = ftell(ft);
									fclose(ft);
									
									if(filesize > 0)
									{	
										char buffer[1000];
										FILE *f = fopen(tmp, "r");
										while (fgets(buffer, sizeof(buffer), f))
										{
											buffer[strlen(buffer)-1] = 0;
											if(write( tdL.cl, buffer, sizeof(buffer)) <= 0)
												perror("[server]Eroare la write() catre client.\n");
										}
										fclose(f);
									}
									break;
								}
								else
								{
									strcpy(msg,"\033[1;92m[Chat] \033[0;92mInvalid option. Please enter another number:\033[0m");
									if(write(tdL.cl, msg, sizeof(msg)) <= 0)
										perror("[server]Eroare la write() catre client.\n");
								}
							}
						}
					}
				}
				else
				{
					strcpy(msg,"\033[1;92m[Chat] \033[0;92mUser doesn't exist\033[0m");
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1], "post") == 0)
			{
				char user[100], name[1000], path[1000] = "serverdata/accounts.txt", msg[1000];
				strcpy(user, lines[2]);
				strcpy(name, lines[3]);
				for(int i = 4; i <= size; i++)
				{
					strcat(name, " ");
					strcat(name, lines[i]);
				}

				int exists = 0;
				char id[64], usr[64], pass[64];
				FILE* f = fopen(path, "r");
				while(1)
				{
					if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(usr, user) == 0)
					{
						fclose(f);
						exists = 1;
						break;
					}
				}

				if(exists)
				{
					int found = 0;
					strcpy(path, "serverdata/users/");
					strcat(path, user);
					strcat(path, "/posts");
					DIR* directory = opendir(path);
					struct dirent* currentfile;

	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							char name2[1000] = {0}, names2[1000][1000] = {0};
							strcpy(name2, currentfile->d_name);
							split(name2, names2, "*");
							if(strcmp(name, names2[0]) == 0)
							{
								found = 1;
								if(strcmp(names2[1], "public") == 0)
								{
									view_post(path, currentfile->d_name, tdL.cl);
									break;
								}
								else if(strcmp(names2[1], "private") == 0)
								{
									char myname[100];
									pthread_mutex_lock(&lock_update_online);
									strcpy(myname, online[tdL.idThread].username);
									pthread_mutex_unlock(&lock_update_online);
									if(strcmp(myname, user) == 0 || admin == 1) view_post(path, currentfile->d_name, tdL.cl);
									else
									{
										strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
									break;
								}
								else if(strcmp(names2[1], "friends") == 0)
								{
									char pathtouser[1000] = "serverdata/users/", myname[100];
									pthread_mutex_lock(&lock_update_online);
									strcpy(myname, online[tdL.idThread].username);
									pthread_mutex_unlock(&lock_update_online);
									strcat(pathtouser, user);
									strcat(pathtouser, "/friends.txt");

									char frnd[100], typef[100];
									int friendfound = 0;
									f = fopen(pathtouser, "r");
									while(1)
									{
										if(fscanf(f, "%s %s", frnd, typef) == EOF)
										{
											fclose(f);
											break;
										}
										if(strcmp(frnd, myname) == 0)
										{
											friendfound = 1;
											fclose(f);
											break;
										}
									}

									if(friendfound || strcmp(myname,user) == 0 || admin == 1) view_post(path, currentfile->d_name, tdL.cl);
									else
									{
										strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
									break;
								}
							}
						}
					}
					closedir(directory);

					if(found == 0)
					{
						strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else
				{
					strcpy(msg, "\033[1;92m[Post] \033[0;92mUser doesn't exist \033[0m");
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
			else if(strcmp(lines[1], "profile") == 0)
			{
				char user[100], path[1000] = "serverdata/accounts.txt", msg[1000];
				strcpy(user, lines[2]);

				int exists = 0;
				char id[64], usr[64], pass[64];
				FILE* f = fopen(path, "r");
				while(1)
				{
					if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(usr, user) == 0)
					{
						fclose(f);
						exists = 1;
						break;
					}
				}

				if(exists)
				{
					int found = 0;
					strcpy(path, "serverdata/users/");
					strcat(path, user);

					DIR* directory = opendir(path);
					struct dirent* currentfile;

	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							char name2[1000] = {0}, names2[1000][1000] = {0};
							strcpy(name2, currentfile->d_name);
							split(name2, names2, "*");
							if(strcmp(names2[0], "profile") == 0)
							{
								found = 1;
								if(strcmp(names2[1], "public") == 0)
								{
									view_profile(path, currentfile->d_name, tdL.cl);
									break;
								}
								else if(strcmp(names2[1], "private") == 0)
								{
									char myname[100];
									pthread_mutex_lock(&lock_update_online);
									strcpy(myname, online[tdL.idThread].username);
									pthread_mutex_unlock(&lock_update_online);
									if(strcmp(myname, user) == 0 || admin == 1) view_profile(path, currentfile->d_name, tdL.cl);
									else
									{
										strcpy(msg, "\033[1;92m[Profile] \033[0;92mProfile is private\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
									break;
								}
								else if(strcmp(names2[1], "friends") == 0)
								{
									char pathtouser[1000] = "serverdata/users/", myname[100];
									pthread_mutex_lock(&lock_update_online);
									strcpy(myname, online[tdL.idThread].username);
									pthread_mutex_unlock(&lock_update_online);
									strcat(pathtouser, user);
									strcat(pathtouser, "/friends.txt");

									char frnd[100], typef[100];
									int friendfound = 0;
									f = fopen(pathtouser, "r");
									while(1)
									{
										if(fscanf(f, "%s %s", frnd, typef) == EOF)
										{
											fclose(f);
											break;
										}
										if(strcmp(frnd, myname) == 0)
										{
											friendfound = 1;
											fclose(f);
											break;
										}
									}

									if(friendfound || strcmp(myname,user) == 0 || admin == 1) view_profile(path, currentfile->d_name, tdL.cl);
									else
									{
										strcpy(msg, "\033[1;92m[Profile] \033[0;92mProfile is private\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
									break;
								}
							}
						}
					}
					closedir(directory);

					if(found == 0)
					{
						strcpy(msg, "\033[1;92m[Profile] \033[0;92mProfile doesn't exist\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else
				{
					strcpy(msg, "\033[1;92m[Profile] \033[0;92mUser doesn't exist \033[0m");
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
			else
			{
				if(loggedIn == 0 && (strcmp(lines[1], "chats") == 0 || strcmp(lines[1], "chat") == 0))
				{
					char msg[1000] = "\033[0;92mYou must be logged in to use that command\033[0m";
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
				else
				{
					char msg[1000] = "\033[1;92m[View] \033[0;92mUnknown view arguments\033[0m";
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
		}
		else if(strcmp(lines[0], "/delete") == 0 && loggedIn)
		{
			if(strcmp(lines[1], "chats") == 0)
			{
				char msg[1000];
				
				strcpy(msg, "\033[1;92m[Chat] \033[0;92m Are you sure you want to delete all the chats? (Y/n) \033[0m");
				if(write(tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client");
				char readoption[1000] = {0};

				while(1)
				{
					bzero(readoption, 1000);
					if(read(tdL.cl, readoption, sizeof(readoption)) <=0)
						perror("[server]Eroare la read() de la client");
					if(strcmp(readoption, "Y") == 0 || strcmp(readoption, "n") == 0)break;
					else
					{
						strcpy(msg, "\033[1;92m[Chat] \033[0;92m Invalid option. Please try again (Y/n) \033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				if(strcmp(readoption, "Y") == 0)
				{
					char path[1000] = "serverdata/users/";
					int userexists = 0;
					if(lines[2][0] != 0 && admin == 1)
					{
						FILE* f = fopen("serverdata/accounts.txt", "r");
						char user[64], pass[64], id[64];									
						while(1)
						{
							if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(lines[2], user) == 0)
							{
								userexists = 1;
								strcat(path, lines[2]);
								strcat(path, "/chats");
								fclose(f);
								break;
							}
						}
					}
					else
					{
						pthread_mutex_lock(&lock_update_online);
						strcat(path, online[tdL.idThread].username);
						pthread_mutex_unlock(&lock_update_online);
						strcat(path, "/chats");
						userexists = 1;
					}

					if(userexists)
					{
						DIR *directory = opendir(path);
						struct dirent *currentfile;

						while ( (currentfile = readdir(directory)) != NULL)
						{
							if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
							{
								char tmppath[1000];
								strcpy(tmppath, path);
								strcat(tmppath,"/");
								strcat(tmppath, currentfile->d_name);
								if( unlink(tmppath) < 0)
									perror("Eroare la stergerea chaturilor clientului\n");							
							}
						}
						closedir(directory);
						strcpy(msg, "\033[1;92m[Chat] \033[0;92mAll chats have been deleted\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
					else
					{
						strcpy(msg, "\033[1;92m[Chat] \033[0;92mUser doesn't exist\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else
				{
					strcpy(msg, "\033[1;92m[Chat] \033[0;92mChats deletion cancelled\033[0m");
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
			else if(strcmp(lines[1], "chat") == 0)
			{
				char path[1000] = "serverdata/users/", chatname[1000] = {0}, msg[1000] = {0};
				int userexists = 0;
				if(admin == 1)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char user[64], pass[64], id[64];									
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], user) == 0)
						{
							userexists = 1;
							strcat(path, lines[2]);
							strcat(path, "/chats");
							fclose(f);
							strcpy(chatname, lines[3]);
							for(int i = 4; i <= size; i++)
							{
								strcat(chatname, " ");
								strcat(chatname, lines[i]);
							}
							break;
						}
					}
				}
				else
				{
					userexists = 1;
					pthread_mutex_lock(&lock_update_online);
					strcat(path, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(path, "/chats");
					strcpy(chatname, lines[2]);
					for(int i = 3; i <= size; i++)
					{
						strcat(chatname, " ");
						strcat(chatname, lines[i]);
					}
				}

				if(userexists)
				{
					DIR *directory = opendir(path);
					struct dirent *currentfile;

					int countcollisions = 0;
					char foundcollisions[1000][1000];
	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							char name[1000] = {0}, names[1000][1000] = {0};
							strcpy(name,currentfile->d_name);
							split(name, names, "*");
							if(strcmp(chatname, names[0]) == 0)
								strcpy(foundcollisions[++countcollisions], currentfile->d_name);
						}
	   				}
					closedir(directory);
					if(countcollisions == 0)
					{
						strcpy(msg, "\033[1;92m[Chat]\033[0;92m Chat name doesn't exist.\033[0m");
						if(write( tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
					}
					else if(countcollisions == 1)
					{
						strcpy(msg, "\033[1;92m[Chat] \033[0;92m Are you sure you want to delete this chat? (Y/n) \033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						char readoption[1000] = {0};

						while(1)
						{
							bzero(readoption, 1000);
							if(read(tdL.cl, readoption, sizeof(readoption)) <=0)
								perror("[server]Eroare la read() de la client");
							if(strcmp(readoption, "Y") == 0 || strcmp(readoption, "n") == 0)break;
							else
							{
								strcpy(msg, "\033[1;92m[Chat] \033[0;92m Invalid option. Please try again (Y/n) \033[0m");
								if(write(tdL.cl, msg, sizeof(msg)) <= 0)
									perror("[server]Eroare la write() catre client");
							}
						}
						if(strcmp(readoption, "Y") == 0)
						{						
							char tmp[1000];
							strcpy(tmp, path);
							strcat(tmp, "/");
							strcat(tmp, foundcollisions[1]);

							if( unlink(tmp) < 0)
								perror("Eroare la stergerea chaturilor clientului\n");
							strcpy(msg, "\033[1;92m[Chat] \033[0;92m Chat has been deleted. \033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}
						else
						{
							strcpy(msg, "\033[1;92m[Chat] \033[0;92m Chat deletion cancelled. \033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}		
					}
					else
					{
						char msg[1000] = "\033[1;92m[Chat] \033[0;92mMultiple chats with the same name have been found:\n--------------------------------------------------------\033[0m";
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
						for(int i = 1; i <= countcollisions; i++)	
						{
							char buffer[100] = {0}, msg2[1000] = "\033[1;94m[", name[1000] = {0}, names[1000][1000] = {0};
							int tmpsize = 0;
							strcpy(name, foundcollisions[i]);
							tmpsize = split(name, names, "*");

							if(tmpsize > 0)
							{
								snprintf(buffer, 100, "%d", i);
								strcat(msg2, buffer);
								strcat(msg2, "] \033[0;92mChat \033[0;94m[");
								strcat(msg2, names[0]);
								strcat(msg2, "] \033[0;92mwith you");
								for(int i = 1; i <= tmpsize; i++)
								{
									strcat(msg2, " and \033[0;94m[");
									strcat(msg2, names[i]);
									strcat(msg2, "]\033[0;92m");
								}
								strcat(msg2, ".\033[0m");
								if(write( tdL.cl, msg2, sizeof(msg2)) <= 0)
									perror("[server]Eroare la write() catre client.\n");
							}
						}
						strcpy(msg,"\033[0;92m--------------------------------------------------------\n\033[1;92m[Chat] \033[0;92mPlease enter the number of the chat you want to delete:\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client.\n");
						while(1)
						{
							char readoption[1000];
							bzero(readoption, 1000);
							if(read(tdL.cl, readoption, sizeof(readoption)) <= 0)
								perror("[server]Eroare la read() de la client.\n");
							if(strcmp(readoption, "/exit") == 0)
							{
								strcpy(msg,"\033[1;92m[Chat] \033[0;92mDeleting chat cancelled.\033[0m");
								if(write(tdL.cl, msg, sizeof(msg)) <= 0)
									perror("[server]Eroare la write() catre client.\n");
								break;
							}
							else
							{
								int nroption = atoi(readoption);
								if(nroption >= 1 && nroption <= countcollisions)
								{
									strcpy(msg, "\033[1;92m[Chat] \033[0;92m Are you sure you want to delete this chat? (Y/n) \033[0m");
									if(write(tdL.cl, msg, sizeof(msg)) <= 0)
										perror("[server]Eroare la write() catre client");
									char readoption[1000] = {0};

									while(1)
									{
										bzero(readoption, 1000);
										if(read(tdL.cl, readoption, sizeof(readoption)) <=0)
											perror("[server]Eroare la read() de la client");
										if(strcmp(readoption, "Y") == 0 || strcmp(readoption, "n") == 0)break;
										else
										{
											strcpy(msg, "\033[1;92m[Chat] \033[0;92m Invalid option. Please try again (Y/n) \033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
									}		
									if(strcmp(readoption, "Y") == 0)
									{						
										char tmp[1000];
										strcpy(tmp, path);
										strcat(tmp, "/");
										strcat(tmp, foundcollisions[1]);

										if( unlink(tmp) < 0)
											perror("Eroare la stergerea chaturilor clientului\n");
										strcpy(msg, "\033[1;92m[Chat] \033[0;92m Chat has been deleted. \033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
									else
									{
										strcpy(msg, "\033[1;92m[Chat] \033[0;92m Chat deletion cancelled. \033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}		
									break;
								}
								else
								{
									strcpy(msg,"\033[1;92m[Chat] \033[0;92mInvalid option. Please enter another number:\033[0m");
									if(write(tdL.cl, msg, sizeof(msg)) <= 0)
										perror("[server]Eroare la write() catre client.\n");
								}
							}
						}
					}
				}
				else
				{
					strcpy(msg,"\033[1;92m[Chat] \033[0;92mUser doesn't exist\033[0m");
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client.\n");
				}
			}
			if(strcmp(lines[1], "post") == 0)
			{
				char user[100], name[1000], path[1000] = "serverdata/accounts.txt", msg[1000];
				int userexists = 0;
				if(admin == 1)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char usr[64], pass[64], id[64];									
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], usr) == 0)
						{
							userexists = 1;
							strcpy(user, lines[2]);
							fclose(f);
							strcpy(name, lines[3]);
							for(int i = 4; i <= size; i++)
							{
								strcat(name, " ");
								strcat(name, lines[i]);
							}
							break;
						}
					}
				}
				else
				{
					userexists = 1;
					pthread_mutex_lock(&lock_update_online);
					strcpy(user, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcpy(name, lines[2]);
					for(int i = 3; i <= size; i++)
					{
						strcat(name, " ");
						strcat(name, lines[i]);
					}
				}

				if(userexists)
				{
					int found = 0;
					strcpy(path, "serverdata/users/");
					strcat(path, user);
					strcat(path, "/posts");
					DIR* directory = opendir(path);
					struct dirent* currentfile;

	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							char name2[1000] = {0}, names2[1000][1000] = {0};
							strcpy(name2, currentfile->d_name);
							split(name2, names2, "*");
							if(strcmp(name, names2[0]) == 0)
							{
								found = 1;
								strcpy(msg, "\033[1;92m[Post] \033[0;92mAre you sure you want to delete this post? (Y/n)\033[0m");
								if(write(tdL.cl, msg, sizeof(msg)) <= 0)
									perror("[server]Eroare la write() catre client");
								while(1)
								{
									char readoption[1000];
									if(read(tdL.cl, readoption, sizeof(readoption)) <= 0)
										perror("[server]Eroare la write() catre client");

									if(strcmp(readoption, "Y") == 0)
									{
										char fullpath[1000];
										strcpy(fullpath,path);
										strcat(fullpath, "/");
										strcat(fullpath, currentfile->d_name);
										delete_directory(fullpath);
										strcpy(msg, "\033[1;92m[Post] \033[0;92mPost has been deleted\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
										break;
									}
									else if(strcmp(readoption, "n") == 0)
									{
										strcpy(msg, "\033[1;92m[Post] \033[0;92mPost deletion cancelled\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
										break;
									}
									else
									{
										strcpy(msg, "\033[1;92m[Post] \033[0;92mInvalid option\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
								}
								break;
							}
						}
					}
					closedir(directory);

					if(found == 0)
					{
						strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else
				{
					strcpy(msg, "\033[1;92m[Post] \033[0;92mUser doesn't exist \033[0m");
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
			else if(strcmp(lines[1], "comment") == 0)
			{
				char user[100], name[1000], path[1000] = "serverdata/accounts.txt", msg[1000], myname[1000];
				int userexists = 0;
				if(admin == 1)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char usr[64], pass[64], id[64];									
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], usr) == 0)
						{
							userexists = 1;
							strcpy(myname, lines[2]);
							strcpy(user, lines[3]);
							fclose(f);
							strcpy(name, lines[4]);
							for(int i = 5; i <= size; i++)
							{
								strcat(name, " ");
								strcat(name, lines[i]);
							}
							break;
						}
					}
				}
				else
				{
					userexists = 1;
					pthread_mutex_lock(&lock_update_online);
					strcpy(myname, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcpy(user, lines[2]);
					strcpy(name, lines[3]);
					for(int i = 4; i <= size; i++)
					{
						strcat(name, " ");
						strcat(name, lines[i]);
					}
				}
				int exists = 0;
				char id[64], usr[64], pass[64];
				FILE* f = fopen(path, "r");
				while(1)
				{
					if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(usr, user) == 0)
					{
						fclose(f);
						exists = 1;
						break;
					}
				}

				if(exists && userexists)
				{
					int found = 0;
					strcpy(path, "serverdata/users/");
					strcat(path, user);
					strcat(path, "/posts");
					DIR* directory = opendir(path);
					struct dirent* currentfile;

	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							char name2[1000] = {0}, names2[1000][1000] = {0};
							strcpy(name2, currentfile->d_name);
							split(name2, names2, "*");
							if(strcmp(name, names2[0]) == 0)
							{
								found = 1;
								if(strcmp(names2[1], "public") == 0)
								{
									delete_comment(path, currentfile->d_name, user, myname, tdL.cl, tdL.idThread);
									break;
								}
								else if(strcmp(names2[1], "private") == 0)
								{
									if(strcmp(myname, user) == 0 || admin == 1) delete_comment(path, currentfile->d_name, user, myname, tdL.cl, tdL.idThread);
									else
									{
										strcpy(msg, "\033[1;92m[Comment] \033[0;92mPost doesn't exist\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
									break;
								}
								else if(strcmp(names2[1], "friends") == 0)
								{
									char pathtouser[1000] = "serverdata/users/";
									strcat(pathtouser, user);
									strcat(pathtouser, "/friends.txt");

									char frnd[100], typef[100];
									int friendfound = 0;
									f = fopen(pathtouser, "r");
									while(1)
									{
										if(fscanf(f, "%s %s", frnd, typef) == EOF)
										{
											fclose(f);
											break;
										}
										if(strcmp(frnd, myname) == 0)
										{
											friendfound = 1;
											fclose(f);
											break;
										}
									}

									if(friendfound || strcmp(myname, user) == 0 || admin == 1) delete_comment(path, currentfile->d_name, user, myname, tdL.cl, tdL.idThread);
									else
									{
										strcpy(msg, "\033[1;92m[Comment] \033[0;92mPost doesn't exist\033[0m");
										if(write(tdL.cl, msg, sizeof(msg)) <= 0)
											perror("[server]Eroare la write() catre client");
									}
									break;
								}
							}
						}
					}
					closedir(directory);

					if(found == 0)
					{
						strcpy(msg, "\033[1;92m[Comment] \033[0;92mPost doesn't exist\033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else
				{
					strcpy(msg, "\033[1;92m[Comment] \033[0;92mUser doesn't exist \033[0m");
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
			else if(strcmp(lines[1], "reaction") == 0)
			{
				if(strcmp(lines[2], "post") == 0)
				{
					char user[100], name[1000], path[1000] = "serverdata/accounts.txt", msg[1000], myname[1000];
					int userexists = 0;
					if(admin == 1)
					{
						FILE* f = fopen("serverdata/accounts.txt", "r");
						char usr[64], pass[64], id[64];									
						while(1)
						{
							if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(lines[3], usr) == 0)
							{
								userexists = 1;
								strcpy(myname, lines[3]);
								strcpy(user, lines[4]);
								fclose(f);
								strcpy(name, lines[5]);
								for(int i = 6; i <= size; i++)
								{
									strcat(name, " ");
									strcat(name, lines[i]);
								}
								break;
							}
						}
					}
					else
					{
						userexists = 1;
						pthread_mutex_lock(&lock_update_online);
						strcpy(myname, online[tdL.idThread].username);
						pthread_mutex_unlock(&lock_update_online);
						strcpy(user, lines[3]);
						strcpy(name, lines[4]);
						for(int i = 5; i <= size; i++)
						{
							strcat(name, " ");
							strcat(name, lines[i]);
						}
					}
					int exists = 0;
					char id[64], usr[64], pass[64];
					FILE* f = fopen(path, "r");
					while(1)
					{
						if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(usr, user) == 0)
						{
							fclose(f);
							exists = 1;
							break;
						}
					}

					if(exists && userexists)
					{
						int found = 0;
						strcpy(path, "serverdata/users/");
						strcat(path, user);
						strcat(path, "/posts");
						DIR* directory = opendir(path);
						struct dirent* currentfile;

		   				while ( (currentfile = readdir(directory)) != NULL)
						{
							if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
							{
								char name2[1000] = {0}, names2[1000][1000] = {0};
								strcpy(name2, currentfile->d_name);
								split(name2, names2, "*");
								if(strcmp(name, names2[0]) == 0)
								{
									found = 1;
									if(strcmp(names2[1], "public") == 0)
									{
										delete_reaction_post(path, currentfile->d_name, user, myname, tdL.cl, tdL.idThread);
										break;
									}
									else if(strcmp(names2[1], "private") == 0)
									{
										if(strcmp(myname, user) == 0 || admin == 1) delete_reaction_post(path, currentfile->d_name, user, myname, tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
									else if(strcmp(names2[1], "friends") == 0)
									{
										char pathtouser[1000] = "serverdata/users/";
										strcat(pathtouser, user);
										strcat(pathtouser, "/friends.txt");

										char frnd[100], typef[100];
										int friendfound = 0;
										f = fopen(pathtouser, "r");
										while(1)
										{
											if(fscanf(f, "%s %s", frnd, typef) == EOF)
											{
												fclose(f);
												break;
											}
											if(strcmp(frnd, myname) == 0)
											{
												friendfound = 1;
												fclose(f);
												break;
											}
										}

										if(friendfound || strcmp(user,myname) == 0 || admin == 1) delete_reaction_post(path, currentfile->d_name, user, myname, tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
								}
							}
						}
						closedir(directory);

						if(found == 0)
						{
							strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}
					}
					else
					{
						strcpy(msg, "\033[1;92m[Reaction] \033[0;92mUser doesn't exist \033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else if(strcmp(lines[2], "comment") == 0)
				{
					char user1[100], user2[100], name[1000], msg[1000], path[1000] = "serverdata/accounts.txt", myname[1000];
					int userexists = 0;
					if(admin == 1)
					{
						FILE* f = fopen("serverdata/accounts.txt", "r");
						char usr[64], pass[64], id[64];									
						while(1)
						{
							if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(lines[3], usr) == 0)
							{
								userexists = 1;
								fclose(f);
								strcpy(myname, lines[3]);
								strcpy(user1, lines[4]);
								strcpy(user2, lines[5]);
								strcpy(name, lines[6]);
								for(int i = 7; i <= size; i++)
								{
									strcat(name, " ");
									strcat(name, lines[i]);
								}
								break;
							}
						}
					}
					else
					{
						userexists = 1;
						pthread_mutex_lock(&lock_update_online);
						strcpy(myname, online[tdL.idThread].username);
						pthread_mutex_unlock(&lock_update_online);
						strcpy(user1, lines[3]);
						strcpy(user2, lines[4]);
						strcpy(name, lines[5]);
						for(int i = 6; i <= size; i++)
						{
							strcat(name, " ");
							strcat(name, lines[i]);
						}
					}
					int exists = 0;
					char id[64], usr[64], pass[64];
					FILE* f = fopen(path, "r");
					while(1)
					{
						if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(usr, user2) == 0)
						{
							fclose(f);
							exists = 1;
							break;
						}
					}

					if(exists && userexists)
					{
						int found = 0;
						strcpy(path, "serverdata/users/");
						strcat(path, user2);
						strcat(path, "/posts");
						DIR* directory = opendir(path);
						struct dirent* currentfile;

		   				while ( (currentfile = readdir(directory)) != NULL)
						{
							if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
							{
								char name2[1000] = {0}, names2[1000][1000] = {0};
								strcpy(name2, currentfile->d_name);
								split(name2, names2, "*");
								if(strcmp(name, names2[0]) == 0)
								{
									found = 1;
									if(strcmp(names2[1], "public") == 0)
									{
										delete_reaction_comment(path, currentfile->d_name, user1, myname, tdL.cl, tdL.idThread);
										break;
									}
									else if(strcmp(names2[1], "private") == 0)
									{
										if(strcmp(myname, user2) == 0 || admin == 1) delete_reaction_comment(path, currentfile->d_name, user1, myname, tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
									else if(strcmp(names2[1], "friends") == 0)
									{
										char pathtouser[1000] = "serverdata/users/";
										strcat(pathtouser, user2);
										strcat(pathtouser, "/friends.txt");

										char frnd[100], typef[100];
										int friendfound = 0;
										f = fopen(pathtouser, "r");
										while(1)
										{
											if(fscanf(f, "%s %s", frnd, typef) == EOF)
											{
												fclose(f);
												break;
											}
											if(strcmp(frnd, myname) == 0)
											{
												friendfound = 1;
												fclose(f);
												break;
											}
										}

										if(friendfound || strcmp(user2,myname) == 0 || admin == 1) delete_reaction_comment(path, currentfile->d_name, user1, myname, tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
								}
							}
						}
						closedir(directory);

						if(found == 0)
						{
							strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}
					}
					else
					{
						strcpy(msg, "\033[1;92m[Post] \033[0;92mUser doesn't exist \033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					} 
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Delete] \033[0;92mUnknown delete arguments\033[0m";
				if(write(tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client");
			}
		}
		else if(strcmp(lines[0], "/add") == 0 && loggedIn)
		{
			if(strcmp(lines[1], "friend") == 0)
			{
				char friend[1000], type[1000], type2[1000], msg[1000] = "\033[1;92m[Friend] \033[0;92mNew friend request from \033[0;94m[";
				strcpy(friend, lines[2]);
				strcpy(type, lines[3]);
				strcpy(type2, lines[3]);
				for(int i = 4; i <= size; i++)
				{
					strcat(type, " ");
					strcat(type, lines[i]);
					strcat(type2, "-");
					strcat(type2, lines[i]);
				}
				pthread_mutex_lock(&lock_update_online);
				strcat(msg, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(msg,"] \033[0;92mas \033[0;94m[");
				strcat(msg, type);
				strcat(msg, "]\033[0m");

				int alreadyfriend = 0, exists = 0, alreadyrequested = 0;
				char usr[64], typef[1000], path[1000] = "serverdata/users/";
				pthread_mutex_lock(&lock_update_online);
				strcat(path, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(path, "/friends.txt");
				FILE* ft = fopen(path, "r");
				while(1)
				{
					if(fscanf(ft,"%s %s",usr, typef) == EOF)
					{
						fclose(ft);
						break;
					}
					if(strcmp(friend, usr) == 0)
					{
						alreadyfriend = 1;
						fclose(ft);
						break;
					}
				}

				char id1[64], user1[64], pass1[64];
				ft = fopen("serverdata/accounts.txt", "r");
				while(1)
				{
					if(fscanf(ft,"%s %s %s",id1, user1, pass1) == EOF)
					{
						fclose(ft);
						break;
					}
					if(strcmp(friend, user1) == 0)
					{
						exists = 1;
						fclose(ft);
						break;
					}
				}

				if(exists)
				{
					char path[1000] = "serverdata/users/", frnd[1000], typef[1000], myusername[64];
					strcat(path,friend);
					strcat(path,"/friendrequests.txt");
					pthread_mutex_lock(&lock_update_online);
					strcpy(myusername, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					FILE *f = fopen(path, "r");
					while(1)
					{
						if(fscanf(f,"%s %s", frnd, typef) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(myusername, frnd) == 0)
						{
							alreadyrequested = 1;
							fclose(f);
							break;
						}
					}
				}
				
				if(alreadyfriend == 0 && alreadyrequested == 0)
				{
					int foundonline = 0;
					pthread_mutex_lock(&lock_update_online);
					for(int j = 0; j < MAXONLINE; j++)
					if(online[j].busy == 0 && strcmp(online[j].username, friend) == 0)
					{
						write (online[j].fd, msg, sizeof(msg));
						foundonline = 1;
						char tmp[1000] = "serverdata/users/";
						strcat(tmp, online[j].username);
						strcat(tmp, "/friendrequests.txt");
						FILE *f = fopen(tmp, "a");
						fprintf(f, "%s %s\n", online[tdL.idThread].username, type2);
						fclose(f);
						strcpy(msg, "\033[1;92m[Friend] \033[0;92mFriend request sent.\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
						break;
					}
					else if(online[j].busy == 1 && strcmp(online[j].username, friend) == 0)
					{
						char tmp[1000] = "serverdata/users/";
						strcat(tmp, online[j].username);
						strcat(tmp, "/notifications.txt");
						FILE *f = fopen(tmp, "a");
						fprintf(f, "%s\n", msg);
						fclose(f);
						foundonline = 1;
						strcpy(tmp, "serverdata/users/");
						strcat(tmp, online[j].username);
						strcat(tmp, "/friendrequests.txt");
						f = fopen(tmp, "a");
						fprintf(f, "%s %s\n", online[tdL.idThread].username, type2);
						fclose(f);
						strcpy(msg, "\033[1;92m[Friend] \033[0;92mFriend request sent.\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
						break;
					}
					pthread_mutex_unlock(&lock_update_online);
					if(foundonline == 0)
					{
						int found = 0;
						char id[64], user[64], pass[64];
						FILE* f = fopen("serverdata/accounts.txt", "r");
						while(1)
						{
							if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(friend, user) == 0)
							{
								found = 1;
								fclose(f);
								break;
							}
						}
						if(found)
						{
							char tmp[1000] = "serverdata/users/";
							strcat(tmp, friend);
							strcat(tmp, "/notifications.txt");
							FILE *f = fopen(tmp, "a");
							fprintf(f, "%s\n", msg);
							fclose(f);
							strcpy(tmp, "serverdata/users/");
							strcat(tmp, friend);
							strcat(tmp, "/friendrequests.txt");
							f = fopen(tmp, "a");
							pthread_mutex_lock(&lock_update_online);
							fprintf(f, "%s %s\n", online[tdL.idThread].username, type2);
							pthread_mutex_unlock(&lock_update_online);
							fclose(f);
							strcpy(msg, "\033[1;92m[Friend] \033[0;92mFriend request sent.\033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
						}
						else
						{
							strcpy(msg, "\033[1;92m[Friend] \033[0;92mUser doesn't exist.\033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
						}
					}
				}
				else
				{
					if(alreadyfriend)
					{
						strcpy(msg, "\033[1;92m[Friend] \033[0;92mUser is already a friend.\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
					else if(alreadyrequested)
					{
						strcpy(msg, "\033[1;92m[Friend] \033[0;92mFriend request already sent.\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
			}
			else if(strcmp(lines[1], "comment") == 0)
			{
				int isbanned = 0;
				char usr1[100], typeb[100], myname[100];
				pthread_mutex_lock(&lock_update_online);
				strcpy(myname, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				FILE* f = fopen("serverdata/banlist.txt", "r");						
				while(1)
				{
					if(fscanf(f,"%s %s",usr1, typeb) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(myname, usr1) == 0 && strcmp(typeb, "comment") == 0)
					{
						isbanned = 1;
						fclose(f);
						break;
					}
				}
				if(isbanned == 0)
				{
					char user[100], name[1000], path[1000] = "serverdata/accounts.txt", msg[1000];
					strcpy(user, lines[2]);
					strcpy(name, lines[3]);
					for(int i = 4; i <= size; i++)
					{
						strcat(name, " ");
						strcat(name, lines[i]);
					}

					int exists = 0;
					char id[64], usr[64], pass[64];
					FILE* f = fopen(path, "r");
					while(1)
					{
						if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(usr, user) == 0)
						{
							fclose(f);
							exists = 1;
							break;
						}
					}

					if(exists)
					{
						int found = 0;
						strcpy(path, "serverdata/users/");
						strcat(path, user);
						strcat(path, "/posts");
						DIR* directory = opendir(path);
						struct dirent* currentfile;

		   				while ( (currentfile = readdir(directory)) != NULL)
						{
							if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
							{
								char name2[1000] = {0}, names2[1000][1000] = {0};
								strcpy(name2, currentfile->d_name);
								split(name2, names2, "*");
								if(strcmp(name, names2[0]) == 0)
								{
									found = 1;
									if(strcmp(names2[1], "public") == 0)
									{
										add_comment(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
										break;
									}
									else if(strcmp(names2[1], "private") == 0)
									{
										char myname[100];
										pthread_mutex_lock(&lock_update_online);
										strcpy(myname, online[tdL.idThread].username);
										pthread_mutex_unlock(&lock_update_online);
										if(strcmp(myname, user) == 0 || admin == 1) add_comment(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Comment] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
									else if(strcmp(names2[1], "friends") == 0)
									{
										char pathtouser[1000] = "serverdata/users/", myname[100];
										pthread_mutex_lock(&lock_update_online);
										strcpy(myname, online[tdL.idThread].username);
										pthread_mutex_unlock(&lock_update_online);
										strcat(pathtouser, user);
										strcat(pathtouser, "/friends.txt");

										char frnd[100], typef[100];
										int friendfound = 0;
										f = fopen(pathtouser, "r");
										while(1)
										{
											if(fscanf(f, "%s %s", frnd, typef) == EOF)
											{
												fclose(f);
												break;
											}
											if(strcmp(frnd, myname) == 0)
											{
												friendfound = 1;
												fclose(f);
												break;
											}
										}

										if(friendfound || strcmp(myname, user) == 0 || admin == 1) add_comment(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Comment] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
								}
							}
						}
						closedir(directory);

						if(found == 0)
						{
							strcpy(msg, "\033[1;92m[Comment] \033[0;92mPost doesn't exist\033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}
					}
					else
					{
						strcpy(msg, "\033[1;92m[Comment] \033[0;92mUser doesn't exist \033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else
				{
					char msg[1000] = "\033[1;92m[Comment] \033[0;92mYou are banned from commenting\033[0m";
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
			else if(strcmp(lines[1], "reaction") == 0)
			{
				int isbanned = 0;
				char usr1[100], typeb[100], myname[100];
				pthread_mutex_lock(&lock_update_online);
				strcpy(myname, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				FILE* f = fopen("serverdata/banlist.txt", "r");						
				while(1)
				{
					if(fscanf(f,"%s %s",usr1, typeb) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(myname, usr1) == 0 && strcmp(typeb, "reaction") == 0)
					{
						isbanned = 1;
						fclose(f);
						break;
					}
				}
				if(isbanned == 0)
				{
					if(strcmp(lines[2], "post") == 0)
					{
						char user[100], name[1000], path[1000] = "serverdata/accounts.txt", msg[1000];
						strcpy(user, lines[3]);
						strcpy(name, lines[4]);
						for(int i = 5; i <= size; i++)
						{
							strcat(name, " ");
							strcat(name, lines[i]);
						}

						int exists = 0;
						char id[64], usr[64], pass[64];
						FILE* f = fopen(path, "r");
						while(1)
						{
							if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(usr, user) == 0)
							{
								fclose(f);
								exists = 1;
								break;
							}
						}

						if(exists)
						{
							int found = 0;
							strcpy(path, "serverdata/users/");
							strcat(path, user);
							strcat(path, "/posts");
							DIR* directory = opendir(path);
							struct dirent* currentfile;

			   				while ( (currentfile = readdir(directory)) != NULL)
							{
								if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
								{
									char name2[1000] = {0}, names2[1000][1000] = {0};
									strcpy(name2, currentfile->d_name);
									split(name2, names2, "*");
									if(strcmp(name, names2[0]) == 0)
									{
										found = 1;
										if(strcmp(names2[1], "public") == 0)
										{
											add_reaction_post(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
											break;
										}
										else if(strcmp(names2[1], "private") == 0)
										{
											char myname[100];
											pthread_mutex_lock(&lock_update_online);
											strcpy(myname, online[tdL.idThread].username);
											pthread_mutex_unlock(&lock_update_online);
											if(strcmp(myname, user) == 0 || admin == 1) add_reaction_post(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
											else
											{
												strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
												if(write(tdL.cl, msg, sizeof(msg)) <= 0)
													perror("[server]Eroare la write() catre client");
											}
											break;
										}
										else if(strcmp(names2[1], "friends") == 0)
										{
											char pathtouser[1000] = "serverdata/users/", myname[100];
											pthread_mutex_lock(&lock_update_online);
											strcpy(myname, online[tdL.idThread].username);
											pthread_mutex_unlock(&lock_update_online);
											strcat(pathtouser, user);
											strcat(pathtouser, "/friends.txt");

											char frnd[100], typef[100];
											int friendfound = 0;
											f = fopen(pathtouser, "r");
											while(1)
											{
												if(fscanf(f, "%s %s", frnd, typef) == EOF)
												{
													fclose(f);
													break;
												}
												if(strcmp(frnd, myname) == 0)
												{
													friendfound = 1;
													fclose(f);
													break;
												}
											}

											if(friendfound || strcmp(user,myname) == 0 || admin == 1) add_reaction_post(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
											else
											{
												strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
												if(write(tdL.cl, msg, sizeof(msg)) <= 0)
													perror("[server]Eroare la write() catre client");
											}
											break;
										}
									}
								}
							}
							closedir(directory);

							if(found == 0)
							{
								strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
								if(write(tdL.cl, msg, sizeof(msg)) <= 0)
									perror("[server]Eroare la write() catre client");
							}
						}
						else
						{
							strcpy(msg, "\033[1;92m[Reaction] \033[0;92mUser doesn't exist \033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}
					}
					else if(strcmp(lines[2], "comment") == 0)
					{
						if(strcmp(lines[4], "from") == 0)
						{
							char user1[100], user2[100], name[1000], msg[1000], path[1000] = "serverdata/accounts.txt";
							strcpy(user1, lines[3]);
							strcpy(user2, lines[5]);
							strcpy(name, lines[6]);
							for(int i = 7; i <= size; i++)
							{
								strcat(name, " ");
								strcat(name, lines[i]);
							}

							int exists = 0;
							char id[64], usr[64], pass[64];
							FILE* f = fopen(path, "r");
							while(1)
							{
								if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
								{
									fclose(f);
									break;
								}
								if(strcmp(usr, user2) == 0)
								{
									fclose(f);
									exists = 1;
									break;
								}
							}

							if(exists)
							{
								int found = 0;
								strcpy(path, "serverdata/users/");
								strcat(path, user2);
								strcat(path, "/posts");
								DIR* directory = opendir(path);
								struct dirent* currentfile;

				   				while ( (currentfile = readdir(directory)) != NULL)
								{
									if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
									{
										char name2[1000] = {0}, names2[1000][1000] = {0};
										strcpy(name2, currentfile->d_name);
										split(name2, names2, "*");
										if(strcmp(name, names2[0]) == 0)
										{
											found = 1;
											if(strcmp(names2[1], "public") == 0)
											{
												add_reaction_comment(path, currentfile->d_name, user1, names2[0], tdL.cl, tdL.idThread);
												break;
											}
											else if(strcmp(names2[1], "private") == 0)
											{
												char myname[100];
												pthread_mutex_lock(&lock_update_online);
												strcpy(myname, online[tdL.idThread].username);
												pthread_mutex_unlock(&lock_update_online);
												if(strcmp(myname, user2) == 0 || admin == 1) add_reaction_comment(path, currentfile->d_name, user1, names2[0], tdL.cl, tdL.idThread);
												else
												{
													strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
													if(write(tdL.cl, msg, sizeof(msg)) <= 0)
														perror("[server]Eroare la write() catre client");
												}
												break;
											}
											else if(strcmp(names2[1], "friends") == 0)
											{
												char pathtouser[1000] = "serverdata/users/", myname[100];
												pthread_mutex_lock(&lock_update_online);
												strcpy(myname, online[tdL.idThread].username);
												pthread_mutex_unlock(&lock_update_online);
												strcat(pathtouser, user2);
												strcat(pathtouser, "/friends.txt");

												char frnd[100], typef[100];
												int friendfound = 0;
												f = fopen(pathtouser, "r");
												while(1)
												{
													if(fscanf(f, "%s %s", frnd, typef) == EOF)
													{
														fclose(f);
														break;
													}
													if(strcmp(frnd, myname) == 0)
													{
														friendfound = 1;
														fclose(f);
														break;
													}
												}

												if(friendfound || strcmp(user2,myname) == 0 || admin == 1) add_reaction_comment(path, currentfile->d_name, user1, names2[0], tdL.cl, tdL.idThread);
												else
												{
													strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
													if(write(tdL.cl, msg, sizeof(msg)) <= 0)
														perror("[server]Eroare la write() catre client");
												}
												break;
											}
										}
									}
								}
								closedir(directory);

								if(found == 0)
								{
									strcpy(msg, "\033[1;92m[Reaction] \033[0;92mPost doesn't exist\033[0m");
									if(write(tdL.cl, msg, sizeof(msg)) <= 0)
										perror("[server]Eroare la write() catre client");
								}
							}
							else
							{
								strcpy(msg, "\033[1;92m[Reaction] \033[0;92mUser doesn't exist \033[0m");
								if(write(tdL.cl, msg, sizeof(msg)) <= 0)
									perror("[server]Eroare la write() catre client");
							}
						} 
					}
				}
				else
				{
					char msg[1000] = "\033[1;92m[Reaction] \033[0;92mYou are banned from reacting\033[0m";
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Add] \033[0;92mUnknown add arguments\033[0m";
				if(write(tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client");
			}
		}
		else if(strcmp(lines[0], "/confirm") == 0 && loggedIn)
		{
			if(strcmp(lines[1], "friend") == 0)
			{
				char friend[1000], type[1000], type2[1000], msg[1000] = "\033[1;92m[Friend] \033[0;94m[";
				strcpy(friend, lines[2]);
				strcpy(type, lines[3]);
				strcpy(type2, lines[3]);
				for(int i = 4; i <= size; i++)
				{
					strcat(type, " ");
					strcat(type, lines[i]);
					strcat(type2, "-");
					strcat(type2, lines[i]);
				}
				pthread_mutex_lock(&lock_update_online);
				strcat(msg, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(msg,"] \033[0;92maccepted your friend request as \033[0;94m[");
				strcat(msg, type);
				strcat(msg, "]\033[0m");
				int found = 0;
				char user[100], type3[1000], path[1000] = "serverdata/users/";
				pthread_mutex_lock(&lock_update_online);
				strcat(path, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(path, "/friendrequests.txt");
				FILE* f = fopen(path, "r");
				while(1)
				{
					if(fscanf(f,"%s %s",user, type3) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(friend, user) == 0 && strcmp(type3,type2) == 0)
					{
						found = 1;
						fclose(f);
						break;
					}
				}
				if(found)
				{
					char tmp[1000] = "serverdata/users/";
					strcat(tmp, friend);
					strcat(tmp, "/friends.txt");
					FILE *f = fopen(tmp, "a");
					pthread_mutex_lock(&lock_update_online);
					fprintf(f, "%s %s\n", online[tdL.idThread].username, type2);
					pthread_mutex_unlock(&lock_update_online);
					fclose(f);
					strcpy(tmp, "serverdata/users/");
					pthread_mutex_lock(&lock_update_online);
					strcat(tmp, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(tmp, "/friends.txt");
					f = fopen(tmp, "a");
					fprintf(f, "%s %s\n", friend, type2);
					fclose(f);
					send_notification(friend, msg);
					strcpy(msg, "\033[1;92m[Friend] \033[0;92mFriend request confirmed.\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
					char path1[1000] = "serverdata/users/", path2[1000] = "serverdata/users/", usr[1000], typef[1000];
					pthread_mutex_lock(&lock_update_online);
					strcat(path1, online[tdL.idThread].username);
					strcat(path2, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(path1, "/friendrequests.txt");
					f = fopen(path1, "r");
					strcat(path2, "/friendrequests2.txt");
					FILE* f2 = fopen(path2, "a");
					while(1)
					{
						if(fscanf(f,"%s %s", usr, typef) == EOF)
						{
							fclose(f);
							fclose(f2);
							break;
						}
						if(strcmp(usr, friend) != 0)
							fprintf(f2, "%s %s\n", usr, typef);
					}
					f = fopen(path1, "w");
					f2 = fopen(path2, "r");
					while(1)
					{
						if(fscanf(f2,"%s %s", usr, typef) == EOF)
						{
							fclose(f);
							fclose(f2);
							break;
						}
						fprintf(f, "%s %s\n", usr, typef);
					}
					unlink(path2);
				}
				else
				{
					strcpy(msg, "\033[1;92m[Friend] \033[0;92mUser doesn't exist.\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Confirm] \033[0;92mUnknown confirm arguments\033[0m";
				if(write(tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client");
			}
		}
		else if(strcmp(lines[0], "/decline") == 0 && loggedIn)
		{
			if(strcmp(lines[1], "friend") == 0)
			{
				char friend[1000], type[1000], type2[1000], msg[1000] = "\033[1;92m[Friend] \033[0;94m[";
				strcpy(friend, lines[2]);
				strcpy(type, lines[3]);
				strcpy(type2, lines[3]);
				for(int i = 4; i <= size; i++)
				{
					strcat(type, " ");
					strcat(type, lines[i]);
					strcat(type2, "-");
					strcat(type2, lines[i]);
				}
				pthread_mutex_lock(&lock_update_online);
				strcat(msg, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(msg,"] \033[0;92mdeclined your friend request as \033[0;94m[");
				strcat(msg, type);
				strcat(msg, "]\033[0m");
				int found = 0;
				char user[100], type3[1000], path[1000] = "serverdata/users/";
				pthread_mutex_lock(&lock_update_online);
				strcat(path, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(path, "/friendrequests.txt");
				FILE* f = fopen(path, "r");
				while(1)
				{
					if(fscanf(f,"%s %s",user, type3) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(friend, user) == 0 && strcmp(type3,type2) == 0)
					{
						found = 1;
						fclose(f);
						break;
					}
				}
				if(found)
				{
					send_notification(friend, msg);
					strcpy(msg, "\033[1;92m[Friend] \033[0;92mFriend request declined.\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
					char path1[1000] = "serverdata/users/", path2[1000] = "serverdata/users/", usr[1000], typef[1000];
					pthread_mutex_lock(&lock_update_online);
					strcat(path1, online[tdL.idThread].username);
					strcat(path2, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(path1, "/friendrequests.txt");
					f = fopen(path1, "r");
					strcat(path2, "/friendrequests2.txt");
					FILE* f2 = fopen(path2, "a");
					while(1)
					{
						if(fscanf(f,"%s %s", usr, typef) == EOF)
						{
							fclose(f);
							fclose(f2);
							break;
						}
						if(strcmp(usr, friend) != 0)
							fprintf(f2, "%s %s\n", usr, typef);
					}
					f = fopen(path1, "w");
					f2 = fopen(path2, "r");
					while(1)
					{
						if(fscanf(f2,"%s %s", usr, typef) == EOF)
						{
							fclose(f);
							fclose(f2);
							break;
						}
						fprintf(f, "%s %s\n", usr, typef);
					}
					unlink(path2);
				}
				else
				{
					strcpy(msg, "\033[1;92m[Friend] \033[0;92mUser doesn't exist.\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Decline] \033[0;92mUnknown decline arguments\033[0m";
				if(write(tdL.cl, msg, sizeof(msg)) <= 0)
					perror("[server]Eroare la write() catre client");
			}
		}
		else if(strcmp(lines[0], "/show") == 0)
		{
			if(strcmp(lines[1], "friends") == 0 && loggedIn)
			{
				char path[1000] = "serverdata/users/", user[1000], type[1000], msg[1000] = "\033[1;92m[Friend] \033[0;92mList of your friends:\n-------------------------------\033[0m\n";
				int userexists = 0;
				if(admin == 1 && lines[2][0] != 0)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char usr[64], pass[64], id[64];									
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], usr) == 0)
						{
							userexists = 1;
							fclose(f);
							strcat(path, lines[2]);
							strcat(path, "/friends.txt");
							break;
						}
					}
				}
				else
				{
					userexists = 1;
					pthread_mutex_lock(&lock_update_online);
					strcat(path, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(path, "/friends.txt");
				}

				if(userexists)
				{
					FILE* f = fopen(path, "r");
					int cont = 0;
					while(1)
					{
						if(fscanf(f,"%s %s", user, type) == EOF)
						{
							fclose(f);
							break;
						}
						else cont++;
						int found = 0;
						pthread_mutex_lock(&lock_update_online);
						for(int i = 0; i < MAXONLINE; i++)
							if(strcmp(online[i].username, user) == 0)
							{
								found = 1;
								char buffer[10];
								snprintf(buffer, 10, "%d", cont);
								strcat(msg, "\033[1;94m[");
								strcat(msg, buffer);
								strcat(msg, "] \033[0;36m");
								strcat(msg, user);
								strcat(msg, " \033[1;92mOnline\033[0m\n");
								break;
							}
						pthread_mutex_unlock(&lock_update_online);
						if(found == 0)
						{
							char buffer[10];
							snprintf(buffer, 10, "%d", cont);
							strcat(msg, "\033[1;94m[");
							strcat(msg, buffer);
							strcat(msg, "] \033[0;36m");
							strcat(msg, user);
							strcat(msg, " \033[1;91mOffline\033[0m\n");
						}
					}
					if(cont == 0) strcat(msg, "\033[0;92mYou don't have any friends :(\033[0m\n");
					strcat(msg, "\033[0;92m-------------------------------\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
				else
				{
					strcpy(msg, "\033[1;92m[Show] \033[0;92mUser doesn't exist\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1], "users") == 0)
			{
				char path[1000] = "serverdata/accounts.txt", id[64], user[64], pass[64], msg[1000] = "\033[1;92m[Friend] \033[0;92mList of users:\n-----------------------\033[0m\n";
				FILE* f = fopen(path, "r");
				int cont = 0;
				while(1)
				{
					if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
					{
						fclose(f);
						break;
					}
					else cont++;
					int found = 0;
					pthread_mutex_lock(&lock_update_online);
					for(int i = 0; i < MAXONLINE; i++)
						if(strcmp(online[i].username, user) == 0)
						{
							found = 1;
							char buffer[10];
							snprintf(buffer, 10, "%d", cont);
							strcat(msg, "\033[1;94m[");
							strcat(msg, buffer);
							strcat(msg, "] \033[0;36m");
							strcat(msg, user);
							strcat(msg, " \033[1;92mOnline\033[0m\n");
							break;
						}
					pthread_mutex_unlock(&lock_update_online);
					if(found == 0)
					{
						char buffer[10];
						snprintf(buffer, 10, "%d", cont);
						strcat(msg, "\033[1;94m[");
						strcat(msg, buffer);
						strcat(msg, "] \033[0;36m");
						strcat(msg, user);
						strcat(msg, " \033[1;91mOffline\033[0m\n");
					}
				}
				if(cont == 0) strcat(msg, "\033[0;92mThe server doesn't have any users :(\033[0m\n");
				strcat(msg, "\033[0;92m-----------------------\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
			else if(strcmp(lines[1], "posts") == 0)
			{
				char path[1000] = "serverdata/users/", msg[1000] = "\033[1;92m[Posts] \033[0;92mList of \033[0;94m[", namereq[100] = {0};
				int error = 0, found = 0;
				if(lines[2][0] != 0) strcpy(namereq, lines[2]);
				else if(loggedIn)
				{
					pthread_mutex_lock(&lock_update_online);
					strcpy(namereq, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
				}
				else if(loggedIn == 0) error = 1;

				if(error == 0)
				{
					strcat(msg, namereq);
					strcat(path, namereq);
					strcat(msg, "] \033[0;92mposts:\n-------------------------");
					for(int i = 0; i < strlen(namereq); i++) strcat(msg, "-");
					strcat(msg, "\n");
					strcat(path, "/posts");

					FILE* f = fopen("serverdata/accounts.txt", "r");
					char id[64], user[64], pass[64];
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(user, lines[2]) == 0)
						{
							found = 1;
							break;
						}
					}
				}				
				
				if((found == 1 || lines[2][0] == 0) && error == 0)
				{
					DIR *directory = opendir(path);
					struct dirent *currentfile;

					int anyposts = 0;
	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							anyposts++;
							char buffer[10], name[1000] = {0}, names[1000][1000] = {0};
							strcpy(name,currentfile->d_name);
							split(name, names, "*");
							snprintf(buffer, 10, "%d", anyposts);
							strcat(msg, "\033[0;94m[");
							strcat(msg, buffer);
							strcat(msg, "] \033[0;36m[");
							if(strcmp(names[1], "public") == 0)strcat(msg, names[0]);
							else if(strcmp(names[1], "private") == 0)
							{
								if(loggedIn)
								{
									pthread_mutex_lock(&lock_update_online);
									if(lines[2][0] != 0 && strcmp(namereq, online[tdL.idThread].username) != 0 && admin == 0) strcat(msg, "Hidden");
									else strcat(msg, names[0]);
									pthread_mutex_unlock(&lock_update_online);
								}
								else strcat(msg, "Hidden");
							}
							else if(lines[2][0] != 0) 
							{
								char tmpath[1000] = "serverdata/users/";
								strcat(tmpath, namereq);
								strcat(tmpath, "/friends.txt");
								
								if(loggedIn)
								{
									FILE* f2 = fopen(tmpath, "r");
									int fnd = 0;
									char usr[1000], typef[1000], myname[100];
									pthread_mutex_lock(&lock_update_online);
									strcpy(myname, online[tdL.idThread].username);
									pthread_mutex_unlock(&lock_update_online);
									while(1)
									{
										if(fscanf(f2,"%s %s", usr, typef) == EOF)
										{
											fclose(f2);
											break;
										}
										if(strcmp(usr, myname) == 0)
										{
											fnd = 1;
											break;
										}
									}
									if(fnd || strcmp(namereq, myname) == 0 || admin == 1) strcat(msg, names[0]);
									else strcat(msg, "Hidden");
								}
								else strcat(msg, "Hidden");
							}
							else if(lines[2][0] == 0) strcat(msg, names[0]);

							strcat(msg, "] ");
							if(strcmp(names[1], "public") == 0) strcat(msg, "\033[1;92mpublic post\033[0m\n");
							else if(strcmp(names[1], "private") == 0) strcat(msg, "\033[1;91mprivate post\033[0m\n");
							else if(strcmp(names[1], "friends") == 0) strcat(msg, "\033[1;93mfriends only\033[0m\n");
						}
	   				}
					closedir(directory);

					if(anyposts == 0)strcat(msg, "\033[0;92mNo posts to show\033[0m\n");
					strcat(msg, "\033[0;92m-------------------------");
					for(int i = 0; i < strlen(namereq); i++) strcat(msg, "-");
					strcat(msg, "\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
				else
				{
					if(found == 0 && error == 0)
					{
						strcpy(msg, "\033[1;92m[Posts] \033[0;92mThe user doesn't exist\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
					else if(error == 1)
					{
						strcpy(msg, "\033[1;92m[Posts] \033[0;92mCan't view posts while not logged in\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
			}
			else if(strcmp(lines[1], "reactions") == 0)
			{
				if(strcmp(lines[2], "post") == 0)
				{
					char user[100], name[1000], path[1000] = "serverdata/accounts.txt", msg[1000];
					strcpy(user, lines[3]);
					strcpy(name, lines[4]);
					for(int i = 5; i <= size; i++)
					{
						strcat(name, " ");
						strcat(name, lines[i]);
					}

					int exists = 0;
					char id[64], usr[64], pass[64];
					FILE* f = fopen(path, "r");
					while(1)
					{
						if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(usr, user) == 0)
						{
							fclose(f);
							exists = 1;
							break;
						}
					}

					if(exists)
					{
						int found = 0;
						strcpy(path, "serverdata/users/");
						strcat(path, user);
						strcat(path, "/posts");
						DIR* directory = opendir(path);
						struct dirent* currentfile;

		   				while ( (currentfile = readdir(directory)) != NULL)
						{
							if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
							{
								char name2[1000] = {0}, names2[1000][1000] = {0};
								strcpy(name2, currentfile->d_name);
								split(name2, names2, "*");
								if(strcmp(name, names2[0]) == 0)
								{
									found = 1;
									if(strcmp(names2[1], "public") == 0)
									{
										show_reaction_post(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
										break;
									}
									else if(strcmp(names2[1], "private") == 0)
									{
										char myname[100];
										pthread_mutex_lock(&lock_update_online);
										strcpy(myname, online[tdL.idThread].username);
										pthread_mutex_unlock(&lock_update_online);
										if(strcmp(myname, user) == 0 || admin == 1) show_reaction_post(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
									else if(strcmp(names2[1], "friends") == 0)
									{
										char pathtouser[1000] = "serverdata/users/", myname[100];
										pthread_mutex_lock(&lock_update_online);
										strcpy(myname, online[tdL.idThread].username);
										pthread_mutex_unlock(&lock_update_online);
										strcat(pathtouser, user);
										strcat(pathtouser, "/friends.txt");

										char frnd[100], typef[100];
										int friendfound = 0;
										f = fopen(pathtouser, "r");
										while(1)
										{
											if(fscanf(f, "%s %s", frnd, typef) == EOF)
											{
												fclose(f);
												break;
											}
											if(strcmp(frnd, myname) == 0)
											{
												friendfound = 1;
												fclose(f);
												break;
											}
										}

										if(friendfound || strcmp(user,myname) == 0 || admin == 1) show_reaction_post(path, currentfile->d_name, user, names2[0], tdL.cl, tdL.idThread);
										else
										{
											strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
											if(write(tdL.cl, msg, sizeof(msg)) <= 0)
												perror("[server]Eroare la write() catre client");
										}
										break;
									}
								}
							}
						}
						closedir(directory);

						if(found == 0)
						{
							strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}
					}
					else
					{
						strcpy(msg, "\033[1;92m[Post] \033[0;92mUser doesn't exist \033[0m");
						if(write(tdL.cl, msg, sizeof(msg)) <= 0)
							perror("[server]Eroare la write() catre client");
					}
				}
				else if(strcmp(lines[2], "comment") == 0)
				{
					if(strcmp(lines[4], "from") == 0)
					{
						char user1[100], user2[100], name[1000], msg[1000], path[1000] = "serverdata/accounts.txt";
						strcpy(user1, lines[3]);
						strcpy(user2, lines[5]);
						strcpy(name, lines[6]);
						for(int i = 7; i <= size; i++)
						{
							strcat(name, " ");
							strcat(name, lines[i]);
						}

						int exists = 0;
						char id[64], usr[64], pass[64];
						FILE* f = fopen(path, "r");
						while(1)
						{
							if(fscanf(f, "%s %s %s", id, usr, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(usr, user2) == 0)
							{
								fclose(f);
								exists = 1;
								break;
							}
						}

						if(exists)
						{
							int found = 0;
							strcpy(path, "serverdata/users/");
							strcat(path, user2);
							strcat(path, "/posts");
							DIR* directory = opendir(path);
							struct dirent* currentfile;

			   				while ( (currentfile = readdir(directory)) != NULL)
							{
								if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
								{
									char name2[1000] = {0}, names2[1000][1000] = {0};
									strcpy(name2, currentfile->d_name);
									split(name2, names2, "*");
									if(strcmp(name, names2[0]) == 0)
									{
										found = 1;
										if(strcmp(names2[1], "public") == 0)
										{
											show_reaction_comment(path, currentfile->d_name, user1, names2[0], tdL.cl, tdL.idThread);
											break;
										}
										else if(strcmp(names2[1], "private") == 0)
										{
											char myname[100];
											pthread_mutex_lock(&lock_update_online);
											strcpy(myname, online[tdL.idThread].username);
											pthread_mutex_unlock(&lock_update_online);
											if(strcmp(myname, user2) == 0 || admin == 1) show_reaction_comment(path, currentfile->d_name, user1, names2[0], tdL.cl, tdL.idThread);
											else
											{
												strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
												if(write(tdL.cl, msg, sizeof(msg)) <= 0)
													perror("[server]Eroare la write() catre client");
											}
											break;
										}
										else if(strcmp(names2[1], "friends") == 0)
										{
											char pathtouser[1000] = "serverdata/users/", myname[100];
											pthread_mutex_lock(&lock_update_online);
											strcpy(myname, online[tdL.idThread].username);
											pthread_mutex_unlock(&lock_update_online);
											strcat(pathtouser, user2);
											strcat(pathtouser, "/friends.txt");

											char frnd[100], typef[100];
											int friendfound = 0;
											f = fopen(pathtouser, "r");
											while(1)
											{
												if(fscanf(f, "%s %s", frnd, typef) == EOF)
												{
													fclose(f);
													break;
												}
												if(strcmp(frnd, myname) == 0)
												{
													friendfound = 1;
													fclose(f);
													break;
												}
											}

											if(friendfound || strcmp(user2,myname) == 0 || admin == 1) show_reaction_comment(path, currentfile->d_name, user1, names2[0], tdL.cl, tdL.idThread);
											else
											{
												strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
												if(write(tdL.cl, msg, sizeof(msg)) <= 0)
													perror("[server]Eroare la write() catre client");
											}
											break;
										}
									}
								}
							}
							closedir(directory);

							if(found == 0)
							{
								strcpy(msg, "\033[1;92m[Post] \033[0;92mPost doesn't exist\033[0m");
								if(write(tdL.cl, msg, sizeof(msg)) <= 0)
									perror("[server]Eroare la write() catre client");
							}
						}
						else
						{
							strcpy(msg, "\033[1;92m[Post] \033[0;92mUser doesn't exist \033[0m");
							if(write(tdL.cl, msg, sizeof(msg)) <= 0)
								perror("[server]Eroare la write() catre client");
						}
					} 
				}
			}
			else 
			{
				if(loggedIn == 0 && strcmp(lines[1], "friends") == 0)
				{
					char msg[1000] = "\033[0;92mYou must be logged in to use that command\033[0m";
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
				else
				{
					char msg[1000] = "\033[1;92m[Show] \033[0;92mUnknown show arguments\033[0m";
					if(write(tdL.cl, msg, sizeof(msg)) <= 0)
						perror("[server]Eroare la write() catre client");
				}
			}
		}
		else if(strcmp(lines[0], "/edit") == 0 && loggedIn)
		{
			if(strcmp(lines[1], "profile") == 0)
			{
				char dotted[100] = "----------------------------", pathtousr[1000] = "serverdata/users/", pathtodir[1000] = "serverdata/users/", myname[1000];
				int exits = 0, userexists = 0;
				if(admin == 1 && lines[2][0] != 0)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char usr[64], pass[64], id[64];									
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], usr) == 0)
						{
							userexists = 1;
							fclose(f);
							strcpy(myname, lines[2]);
							pthread_mutex_lock(&lock_update_online);
							online[tdL.idThread].busy = 1;
							pthread_mutex_unlock(&lock_update_online);
							strcat(pathtousr, myname);
							strcat(pathtodir, myname);
							strcat(pathtodir, "/");
							break;
						}
					}
				}
				else
				{
					userexists = 1;
					pthread_mutex_lock(&lock_update_online);
					strcpy(myname, online[tdL.idThread].username);
					online[tdL.idThread].busy = 1;
					pthread_mutex_unlock(&lock_update_online);
					strcat(pathtousr, myname);
					strcat(pathtodir, myname);
					strcat(pathtodir, "/");
				}

				if(userexists)
				{
					DIR *directory;
					directory = opendir(pathtousr);
					struct dirent *currentfile;

	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							char name2[1000] = {0}, names2[1000][1000] = {0};
							strcpy(name2, currentfile->d_name);
							split(name2, names2, "*");
							if(strcmp(names2[0], "profile") == 0)
							{
								strcat(pathtodir, currentfile->d_name);
								break;
							}
						}
					}
					closedir(directory);

					char msg[1000] = "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing profile \033[0;94m[", msg2[1000] = "\033[1;92m[Profile] \033[0;94m[";
					strcat(msg, myname);
					strcat(msg2, myname);
					strcat(msg2, "] \033[0;92mhas updated their profile\033[0m");
					for(int i = 0; i < strlen(myname); i++) strcat(dotted, "-");
					strcat(msg, "]\033[0m\n\033[0;92m");
					strcat(msg, dotted);
					strcat(msg, "\033[0m");
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");

					char tmpname[1000] = "serverdata/users/";
					strcat(tmpname, myname);
					strcat(tmpname, "/profile2");
					FILE* f = fopen(tmpname, "w");
					while(1)
					{
						char readoption[1000];
						strcpy(msg, "\033[0;92mFirst name: \033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
						if (read (tdL.cl, readoption, sizeof(readoption)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
						if(strcmp(readoption, "/exit") == 0)
						{
							exits = 1;
							fclose(f);
							unlink(tmpname);
							strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							break;
						}
						else 
						{
							fprintf(f,"\033[1;92m[Profile] \033[0;92mProfile of \033[0;94m[%s]\n\033[0;92m%s\nFirst name: %s\n", myname, dotted, readoption);
							break;
						}
					}
					if(exits == 0)
					{
						while(1)
						{
							char readoption[1000];
							strcpy(msg, "\033[0;92mSecond name: \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if (read (tdL.cl, readoption, sizeof(readoption)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if(strcmp(readoption, "/exit") == 0)
							{
								exits = 1;
								fclose(f);
								unlink(tmpname);
								strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								break;
							}
							else 
							{
								fprintf(f,"Second name: %s\n", readoption);
								break;
							}
						}
					}
					if(exits == 0)
					{
						while(1)
						{
							char readoption[1000];
							strcpy(msg, "\033[0;92mDate of birth: \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if (read (tdL.cl, readoption, sizeof(readoption)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if(strcmp(readoption, "/exit") == 0)
							{
								exits = 1;
								fclose(f);
								unlink(tmpname);
								strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								break;
							}
							else 
							{
								fprintf(f,"Date of birth: %s\n",readoption);
								break;
							}
						}
					}
					if(exits == 0)
					{
						while(1)
						{
							char readoption[1000];
							strcpy(msg, "\033[0;92mCurrently living: \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if (read (tdL.cl, readoption, sizeof(readoption)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if(strcmp(readoption, "/exit") == 0)
							{
								exits = 1;
								fclose(f);
								unlink(tmpname);
								strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								break;
							}
							else 
							{
								fprintf(f,"Currently living: %s\n", readoption);
								break;
							}
						}
					}
					if(exits == 0)
					{
						while(1)
						{
							char readoption[1000];
							strcpy(msg, "\033[0;92mHobbies: \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if (read (tdL.cl, readoption, sizeof(readoption)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if(strcmp(readoption, "/exit") == 0)
							{
								exits = 1;
								fclose(f);
								unlink(tmpname);
								strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								break;
							}
							else 
							{
								fprintf(f,"Hobbies: %s\n",readoption);
								break;
							}
						}
					}
					if(exits == 0)
					{
						while(1)
						{
							char readoption[1000];
							strcpy(msg, "\033[0;92mMore about yourself: \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if (read (tdL.cl, readoption, sizeof(readoption)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							if(strcmp(readoption, "/exit") == 0)
							{
								exits = 1;
								fclose(f);
								unlink(tmpname);
								strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								break;
							}
							else 
							{
								fprintf(f,"More about me: %s\n", readoption);
								break;
							}
						}
					}
					if(exits == 0)
					{
						char msgtmp[1000], tmp[1000], tmplines[1000][1000];
						int tmpsize;
						strcpy(msg, "\033[1;92m[Profile] \033[0;92mSet a restriction: \033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
						while(1)
						{
							if (read (tdL.cl, msgtmp, sizeof(msgtmp)) <= 0)
								perror ("[server]Eroare la read() de la client.\n");
							if(strcmp(msgtmp, "public") == 0 || strcmp(msgtmp, "private") == 0 || strcmp(msgtmp, "friends") == 0)
							{
								strcpy(tmpname, msgtmp);
								break;
							}
							else if(strcmp(msgtmp, "/exit") == 0)
							{
								exits = 1;
								strcpy(tmpname, "serverdata/users/");
								strcat(tmpname, myname);
								strcat(tmpname, "/profile2");
								unlink(tmpname);
								strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled\033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								break;
							}
							else
							{
								strcpy(msg, "\033[1;92m[Profile] \033[0;92mUnknown restriction. \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
							}
						}
						if(exits == 0)
						{
							strcpy(msg, "\033[1;92m[Profile] \033[0;92mSend a notification about the update: \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							while(1)
							{
								if (read (tdL.cl, msgtmp, sizeof(msgtmp)) <= 0)
									perror ("[server]Eroare la read() de la client.\n");
								strcpy(tmp, msgtmp);
								tmpsize = split(tmp, tmplines, " ");
								if(strcmp(tmplines[0], "/send") == 0)
								{
									if(strcmp(tmplines[1], "friends") == 0)
									{
										char path[1000] = "serverdata/users/", user[1000], type[1000];
										strcat(path, myname);
										strcat(path, "/friends.txt");
										FILE* f = fopen(path, "r");
										while(1)
										{
											if(fscanf(f,"%s %s", user, type) == EOF)
											{
												fclose(f);
												break;
											}
											if(tmplines[2][0] != 0)
											{
												for(int i = 2; i <= tmpsize; i++)
													if(strcmp(user, tmplines[i]) == 0) send_notification(user, msg2);
											}
											else send_notification(user, msg2);
										}
										strcpy(msg, "\033[1;92m[Profile] \033[0;92mNotification has been sent. \033[0m");
										if (write (tdL.cl, msg, sizeof(msg)) <= 0)
											perror ("[server]Eroare la write() catre client.\n");
										break;
									}
									else if(strcmp(tmplines[1], "groups") == 0)
									{
										char path[1000] = "serverdata/users/", user[1000], type[1000], tmplines2[1000][1000] = {0}, tmp2[1000] = {0};
										int tmpsize2;
										strcpy(tmp2, tmplines[2]);
										for(int i = 3; i <= tmpsize; i++)
										{
											strcat(tmp2, " ");
											strcat(tmp2, tmplines[i]);
										}
										tmpsize2 = split(tmp2, tmplines2, ",");
										strcat(path, myname);
										strcat(path, "/friends.txt");
										FILE* f = fopen(path, "r");
										while(1)
										{
											if(fscanf(f,"%s %s", user, type) == EOF)
											{
												fclose(f);
												break;
											}
											if(tmplines[2][0] != 0)
											{
												for(int i = 0; i <= tmpsize2; i++)
												{
													char tmp3[1000] = {0}, tmplines3[1000][1000] = {0}, ftype[1000] = {0};
													int tmpsize3;
													strcpy(tmp3, tmplines2[i]);
													tmpsize3 = split(tmp3, tmplines3, " ");
													strcpy(ftype, tmplines3[0]);
													for(int j = 1; j <= tmpsize3; j++)
													{
														strcat(ftype, "-");
														strcat(ftype, tmplines3[j]);
													}
													if(strcmp(type, ftype) == 0) send_notification(user, msg2);
												}
											}
											else send_notification(user, msg2);
										}
										strcpy(msg, "\033[1;92m[Profile] \033[0;92mNotification has been sent. \033[0m");
										if (write (tdL.cl, msg, sizeof(msg)) <= 0)
											perror ("[server]Eroare la write() catre client.\n");
										break;
									}
									else
									{
										strcpy(msg, "\033[1;92m[Profile] \033[0;92mUnknown sending attribute\033[0m");
										if (write (tdL.cl, msg, sizeof(msg)) <= 0)
											perror ("[server]Eroare la write() catre client.\n");
									}
								}
								else if(strcmp(tmplines[0], "none") == 0)
								{
									strcpy(msg, "\033[1;92m[Profile] \033[0;92mNotification hasn't been sent. \033[0m");
									if (write (tdL.cl, msg, sizeof(msg)) <= 0)
										perror ("[server]Eroare la write() catre client.\n");
									break;
								}
								else if(strcmp(tmplines[0], "/exit") == 0)
								{
									exits = 1;
									fclose(f);
									strcpy(tmpname, "serverdata/users/");
									strcat(tmpname, myname);
									strcat(tmpname, "/profile2");
									unlink(tmpname);
									strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mEditing cancelled. \033[0m");
									if (write (tdL.cl, msg, sizeof(msg)) <= 0)
										perror ("[server]Eroare la write() catre client.\n");
									break;
								}
								else
								{
									strcpy(msg, "\033[1;92m[Profile] \033[0;92mUnknown sending attribute \033[0m");
									if (write (tdL.cl, msg, sizeof(msg)) <= 0)
										perror ("[server]Eroare la write() catre client.\n");
								}
							}

							if(exits == 0)
							{
								strcpy(msg, "\033[2J\033[H\033[1;92m[Profile] \033[0;92mProfile updated\033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								fprintf(f, "%s\033[0m", dotted);
								fclose(f);
								unlink(pathtodir);
								char newname[1000] = "serverdata/users/";
								strcat(newname, myname);
								strcat(newname, "/profile*");
								strcat(newname, tmpname);
								strcpy(tmpname, "serverdata/users/");
								strcat(tmpname, myname);
								strcat(tmpname, "/profile2");
								rename(tmpname, newname);
							}
						}
					}
					pthread_mutex_lock(&lock_update_online);
					online[tdL.idThread].busy = 0;
					pthread_mutex_unlock(&lock_update_online);
				}
				else
				{
					char msg[1000] = "\033[1;92m[Profile] \033[0;92mUser doesn't exist\033[0m";
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Edit] \033[0;92mUnknown edit arguments\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
		else if(strcmp(lines[0], "/new") == 0 && loggedIn)
		{
			if(strcmp(lines[1], "update") == 0)
			{
				char name[1000], dotted[100] = "---------------------------";
				strcpy(name, lines[2]);
				for(int i = 3; i <= size; i++)
				{
					strcat(name, " ");
					strcat(name, lines[i]);
				}
				char msg[1000] = "\033[2J\033[H\033[1;92m[Update] \033[0;92mEditing update \033[0;94m[", msg2[1000] = "\033[1;92m[Update] \033[0;94m";
				pthread_mutex_lock(&lock_update_online);
				strcat(msg2, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				strcat(msg2, " \033[0;92msent an update \033[0;94m[");
				strcat(msg2, name);
				strcat(msg2, "]\n\033[0;92m");
				for(int i = 0; i < strlen(name); i++) strcat(dotted, "-");
				pthread_mutex_lock(&lock_update_online);
				for(int i = 0; i < strlen(online[tdL.idThread].username); i++) strcat(dotted, "-");
				online[tdL.idThread].busy = 1;
				pthread_mutex_unlock(&lock_update_online);
				strcat(msg2, dotted);
				strcat(msg, name);
				strcat(msg, "]\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");

				while(1)
				{
					char msgtmp[1000], tmp[1000] = {0}, tmplines[1000][1000] = {0};
					int tmpsize;
					if (read (tdL.cl, msgtmp, sizeof(msgtmp)) <= 0)
						perror ("[server]Eroare la read() de la client.\n");
					strcpy(tmp, msgtmp);
					tmpsize = split(tmp, tmplines, " ");
					if(strcmp(tmplines[0], "/send") == 0)
					{
						if(strcmp(tmplines[1], "friends") == 0)
						{
							char path[1000] = "serverdata/users/", user[1000], type[1000];
							strcat(msg2, "\n");
							strcat(msg2, dotted);
							strcat(msg2, "\033[0m");
							pthread_mutex_lock(&lock_update_online);
							strcat(path, online[tdL.idThread].username);
							pthread_mutex_unlock(&lock_update_online);
							strcat(path, "/friends.txt");
							FILE* f = fopen(path, "r");
							while(1)
							{
								if(fscanf(f,"%s %s", user, type) == EOF)
								{
									fclose(f);
									break;
								}
								if(tmplines[2][0] != 0)
								{
									for(int i = 2; i <= tmpsize; i++)
										if(strcmp(user, tmplines[i]) == 0) send_notification(user, msg2);
								}
								else send_notification(user, msg2);
							}
							strcpy(msg, "\033[2J\033[H\033[1;92m[Update] \033[0;92mUpdate has been sent. \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							break;
						}
						else if(strcmp(tmplines[1], "groups") == 0)
						{
							char path[1000] = "serverdata/users/", user[1000], type[1000], tmplines2[1000][1000] = {0}, tmp2[1000] = {0};
							int tmpsize2;
							strcpy(tmp2, tmplines[2]);
							for(int i = 3; i <= tmpsize; i++)
							{
								strcat(tmp2, " ");
								strcat(tmp2, tmplines[i]);
							}
							tmpsize2 = split(tmp2, tmplines2, ",");
							strcat(msg2, "\n");
							strcat(msg2, dotted);
							strcat(msg2, "\033[0m");
							pthread_mutex_lock(&lock_update_online);
							strcat(path, online[tdL.idThread].username);
							pthread_mutex_unlock(&lock_update_online);
							strcat(path, "/friends.txt");
							FILE* f = fopen(path, "r");
							while(1)
							{
								if(fscanf(f,"%s %s", user, type) == EOF)
								{
									fclose(f);
									break;
								}
								if(tmplines[2][0] != 0)
								{
									for(int i = 0; i <= tmpsize2; i++)
									{
										char tmp3[1000] = {0}, tmplines3[1000][1000] = {0}, ftype[1000] = {0};
										int tmpsize3;
										strcpy(tmp3, tmplines2[i]);
										tmpsize3 = split(tmp3, tmplines3, " ");
										strcpy(ftype, tmplines3[0]);
										for(int j = 1; j <= tmpsize3; j++)
										{
											strcat(ftype, "-");
											strcat(ftype, tmplines3[j]);
										}
										if(strcmp(type, ftype) == 0) send_notification(user, msg2);
									}
								}
								else send_notification(user, msg2);
							}
							strcpy(msg, "\033[2J\033[H\033[1;92m[Update] \033[0;92mUpdate has been sent. \033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							break;
						}
						else
						{
							strcpy(msg, "\033[1;92m[Update] \033[0;92mUnknown sending attribute\033[0m");
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
						}
					}
					else if(strcmp(tmplines[0], "/exit") == 0)
					{
						strcpy(msg, "\033[2J\033[H\033[1;92m[Update] \033[0;92mEditing cancelled \033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
						break;
					}
					else 
					{
						strcat(msg2, "\n| ");
						strcat(msg2, msgtmp);
					}
				}
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 0;
				pthread_mutex_unlock(&lock_update_online);
			}
			else if(strcmp(lines[1], "post") == 0)
			{
				int isbanned = 0;
				char usr1[100], typeb[100], myname[100];
				pthread_mutex_lock(&lock_update_online);
				strcpy(myname, online[tdL.idThread].username);
				pthread_mutex_unlock(&lock_update_online);
				FILE* f = fopen("serverdata/banlist.txt", "r");						
				while(1)
				{
					if(fscanf(f,"%s %s",usr1, typeb) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(myname, usr1) == 0 && strcmp(typeb, "post") == 0)
					{
						isbanned = 1;
						fclose(f);
						break;
					}
				}
				if(isbanned == 0)
				{
					char name[1000] = {0}, dotted[100] = "---------------------------", pathtodir[1000] = "serverdata/users/";
					int exits = 0;
					strcpy(name, lines[2]);
					pthread_mutex_lock(&lock_update_online);
					strcat(pathtodir, online[tdL.idThread].username);
					pthread_mutex_unlock(&lock_update_online);
					strcat(pathtodir, "/posts");
					for(int i = 3; i <= size; i++)
					{
						strcat(name, " ");
						strcat(name, lines[i]);
					}
					DIR *directory;
					directory = opendir(pathtodir);
					struct dirent *currentfile;

					int count = 0, foundpost = 0;
	   				while ( (currentfile = readdir(directory)) != NULL)
					{
						if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
						{
							count++;
							char name2[1000] = {0}, names2[1000][1000] = {0};
							strcpy(name2, currentfile->d_name);
							split(name2, names2, "*");
							if(strcmp(name, names2[0]) == 0) foundpost = 1;
						}
					}
					closedir(directory);
					if(name[0] == 0)
					{
						char buffer[10];
						snprintf(buffer, 10, "%d", count+1);
						strcpy(name, buffer);
					}
					if(foundpost == 1)
					{
						while(1)
						{
							char msg[1000] = "\033[1;92m[Post] \033[0;92mPost name already exists. Please enter another name: \033[0m";
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
							bzero(msg, 1000);
							if (read (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la read() de la client.\n");
							if(strcmp(msg, "/exit") == 0)
							{
								exits = 1;
								break;
							}
							directory = opendir(pathtodir);

							count = 0; foundpost = 0;
			   				while ( (currentfile = readdir(directory)) != NULL)
							{
								if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
								{
									count++;
									char name2[1000] = {0}, names2[1000][1000] = {0};
									strcpy(name2, currentfile->d_name);
									split(name2, names2, "*");
									if(strcmp(msg, names2[0]) == 0) foundpost = 1;
								}
							}
							closedir(directory);
							if(msg[0] == 0)
							{
								char buffer[10];
								snprintf(buffer, 10, "%d", count+1);
								strcpy(name, buffer);
								break;
							}
							else if(foundpost == 0)
							{
								strcpy(name, msg);
								break;
							}
						}
					}
					if(exits == 0)
					{
						char msg[1000] = "\033[2J\033[H\033[1;92m[Post] \033[0;92mEditing post \033[0;94m[", msg2[1000] = "\033[1;92m[Post] \033[0;94m", text[1000] = {0};
						pthread_mutex_lock(&lock_update_online);
						strcat(msg2, online[tdL.idThread].username);
						pthread_mutex_unlock(&lock_update_online);
						strcat(msg2, " \033[0;92mhas a new post \033[0;94m[");
						strcat(msg2, name);
						strcat(msg2, "]\n\033[0;92m");
						for(int i = 0; i < strlen(name); i++) strcat(dotted, "-");
						pthread_mutex_lock(&lock_update_online);
						for(int i = 0; i < strlen(online[tdL.idThread].username); i++) strcat(dotted, "-");
						online[tdL.idThread].busy = 1;
						pthread_mutex_unlock(&lock_update_online);
						strcat(msg2, dotted);
						strcat(msg, name);
						strcat(msg, "]\033[0m");
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");

						while(1)
						{
							char msgtmp[1000], tmp[1000] = {0}, tmplines[1000][1000] = {0};
							int tmpsize;
							if (read (tdL.cl, msgtmp, sizeof(msgtmp)) <= 0)
								perror ("[server]Eroare la read() de la client.\n");
							strcpy(tmp, msgtmp);
							tmpsize = split(tmp, tmplines, " ");
							if(strcmp(tmplines[0], "/post") == 0)
							{
								char postpath[1000] = "serverdata/users/";
								pthread_mutex_lock(&lock_update_online);
								strcat(postpath, online[tdL.idThread].username);
								pthread_mutex_unlock(&lock_update_online);
								strcat(postpath, "/posts/");
								strcat(postpath, name);
								strcpy(msg, "\033[1;92m[Post] \033[0;92mSet a restriction: \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								while(1)
								{
									if (read (tdL.cl, msgtmp, sizeof(msgtmp)) <= 0)
										perror ("[server]Eroare la read() de la client.\n");
									if(strcmp(msgtmp, "public") == 0 || strcmp(msgtmp, "private") == 0 || strcmp(msgtmp, "friends") == 0)
									{
										strcat(postpath, "*");
										strcat(postpath, msgtmp);
										break;
									}
									else if(strcmp(msgtmp, "/exit") == 0)
									{
										exits = 1;
										strcpy(msg, "\033[2J\033[H\033[1;92m[Post] \033[0;92mPosting cancelled. \033[0m");
										if (write (tdL.cl, msg, sizeof(msg)) <= 0)
											perror ("[server]Eroare la write() catre client.\n");
										break;
									}
									else
									{
										strcpy(msg, "\033[1;92m[Post] \033[0;92mUnknown restriction. \033[0m");
										if (write (tdL.cl, msg, sizeof(msg)) <= 0)
											perror ("[server]Eroare la write() catre client.\n");
									}
								}
								if(exits == 0)
								{
									strcpy(msg, "\033[1;92m[Post] \033[0;92mSend a notification about the post: \033[0m");
									if (write (tdL.cl, msg, sizeof(msg)) <= 0)
										perror ("[server]Eroare la write() catre client.\n");
									while(1)
									{
										if (read (tdL.cl, msgtmp, sizeof(msgtmp)) <= 0)
											perror ("[server]Eroare la read() de la client.\n");
										strcpy(tmp, msgtmp);
										tmpsize = split(tmp, tmplines, " ");
										if(strcmp(tmplines[0], "/send") == 0)
										{
											if(strcmp(tmplines[1], "friends") == 0)
											{
												char path[1000] = "serverdata/users/", user[1000], type[1000];
												strcat(msg2, text);
												strcat(msg2, "\n");	
												strcat(msg2, dotted);
												strcat(msg2, "\033[0m");
												pthread_mutex_lock(&lock_update_online);
												strcat(path, online[tdL.idThread].username);
												pthread_mutex_unlock(&lock_update_online);
												strcat(path, "/friends.txt");
												FILE* f = fopen(path, "r");
												while(1)
												{
													if(fscanf(f,"%s %s", user, type) == EOF)
													{
														fclose(f);
														break;
													}
													if(tmplines[2][0] != 0)
													{
														for(int i = 2; i <= tmpsize; i++)
															if(strcmp(user, tmplines[i]) == 0) send_notification(user, msg2);
													}
													else send_notification(user, msg2);
												}
												strcpy(msg, "\033[1;92m[Post] \033[0;92mNotification has been sent. \033[0m");
												if (write (tdL.cl, msg, sizeof(msg)) <= 0)
													perror ("[server]Eroare la write() catre client.\n");
												break;
											}
											else if(strcmp(tmplines[1], "groups") == 0)
											{
												char path[1000] = "serverdata/users/", user[1000], type[1000], tmplines2[1000][1000] = {0}, tmp2[1000] = {0};
												int tmpsize2;
												strcpy(tmp2, tmplines[2]);
												for(int i = 3; i <= tmpsize; i++)
												{
													strcat(tmp2, " ");
													strcat(tmp2, tmplines[i]);
												}
												tmpsize2 = split(tmp2, tmplines2, ",");
												strcat(msg2, text);
												strcat(msg2, "\n");	
												strcat(msg2, dotted);
												strcat(msg2, "\033[0m");
												pthread_mutex_lock(&lock_update_online);
												strcat(path, online[tdL.idThread].username);
												pthread_mutex_unlock(&lock_update_online);
												strcat(path, "/friends.txt");
												FILE* f = fopen(path, "r");
												while(1)
												{
													if(fscanf(f,"%s %s", user, type) == EOF)
													{
														fclose(f);
														break;
													}
													if(tmplines[2][0] != 0)
													{
														for(int i = 0; i <= tmpsize2; i++)
														{
															char tmp3[1000] = {0}, tmplines3[1000][1000] = {0}, ftype[1000] = {0};
															int tmpsize3;
															strcpy(tmp3, tmplines2[i]);
															tmpsize3 = split(tmp3, tmplines3, " ");
															strcpy(ftype, tmplines3[0]);
															for(int j = 1; j <= tmpsize3; j++)
															{
																strcat(ftype, "-");
																strcat(ftype, tmplines3[j]);
															}
															if(strcmp(type, ftype) == 0) send_notification(user, msg2);
														}
													}
													else send_notification(user, msg2);
												}
												strcpy(msg, "\033[1;92m[Post] \033[0;92mNotification has been sent. \033[0m");
												if (write (tdL.cl, msg, sizeof(msg)) <= 0)
													perror ("[server]Eroare la write() catre client.\n");
												break;
											}
											else
											{
												strcpy(msg, "\033[1;92m[Post] \033[0;92mUnknown sending attribute\033[0m");
												if (write (tdL.cl, msg, sizeof(msg)) <= 0)
													perror ("[server]Eroare la write() catre client.\n");
											}
										}
										else if(strcmp(tmplines[0], "none") == 0)
										{
											strcpy(msg, "\033[1;92m[Post] \033[0;92mNotification hasn't been sent. \033[0m");
											if (write (tdL.cl, msg, sizeof(msg)) <= 0)
												perror ("[server]Eroare la write() catre client.\n");
											break;
										}
										else if(strcmp(tmplines[0], "/exit") == 0)
										{
											exits = 1;
											strcpy(msg, "\033[2J\033[H\033[1;92m[Post] \033[0;92mPosting cancelled. \033[0m");
											if (write (tdL.cl, msg, sizeof(msg)) <= 0)
												perror ("[server]Eroare la write() catre client.\n");
											break;
										}
										else
										{
											strcpy(msg, "\033[1;92m[Post] \033[0;92mUnknown sending attribute \033[0m");
											if (write (tdL.cl, msg, sizeof(msg)) <= 0)
												perror ("[server]Eroare la write() catre client.\n");
										}
									}

									if(exits == 0)
									{
										strcpy(msg, "\033[2J\033[H\033[1;92m[Post] \033[0;92mSuccessfully posted\033[0m");
										if (write (tdL.cl, msg, sizeof(msg)) <= 0)
											perror ("[server]Eroare la write() catre client.\n");
										mkdir(postpath, 0700);
										char postpathfile[1000];
										strcpy(postpathfile, postpath);
										strcat(postpathfile, "/post.txt");
										int postfile = open(postpathfile, O_RDWR | O_CREAT, 0700);
										close(postfile);
										char msg4[1000] = "\033[1;92m[Post] \033[0;92mPost \033[0;94m[";
										strcat(msg4, name);
										strcat(msg4, "] \033[0;92mwritten by \033[0;94m[");
										pthread_mutex_lock(&lock_update_online);
										strcat(msg4, online[tdL.idThread].username);
										pthread_mutex_unlock(&lock_update_online);
										strcat(msg4, "]\033[0m");
										FILE* f = fopen(postpathfile, "a");
										fprintf(f, "%s\n", msg4);
										strcpy(msg4, "\033[0;92m----------------------------");
										for(int i = 0; i < strlen(name); i++) strcat(msg4, "-");
										pthread_mutex_lock(&lock_update_online);
										for(int i = 0; i < strlen(online[tdL.idThread].username); i++) strcat(msg4, "-");
										pthread_mutex_unlock(&lock_update_online);
										strcat(msg4, "\033[0m");
										fprintf(f, "%s", msg4);
										fprintf(f, "%s\n", text);
										fprintf(f, "%s\n", msg4);
										fclose(f);
										strcpy(postpathfile, postpath);
										strcat(postpathfile, "/comments");
										mkdir(postpathfile, 0700);
										strcpy(postpathfile, postpath);
										strcat(postpathfile, "/reactions.txt");
										postfile = open(postpathfile, O_RDWR | O_CREAT, 0700);
										close(postfile);
										break;
									}
									else break;
								}
								else break;
							}
							else if(strcmp(tmplines[0], "/exit") == 0)
							{
								strcpy(msg, "\033[2J\033[H\033[1;92m[Post] \033[0;92mEditing cancelled \033[0m");
								if (write (tdL.cl, msg, sizeof(msg)) <= 0)
									perror ("[server]Eroare la write() catre client.\n");
								break;
							}
							else 
							{
								strcat(text, "\n\033[0;92m| ");
								strcat(text, msgtmp);
								strcat(text, "\033[0m");
							}
						}
						pthread_mutex_lock(&lock_update_online);
						online[tdL.idThread].busy = 0;
						pthread_mutex_unlock(&lock_update_online);
					}
					else
					{
						char msg[1000] = "\033[2J\033[H\033[1;92m[Post] \033[0;92mPost creation cancelled \033[0m";
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
				else
				{
					char msg[1000] = "\033[1;92m[Post] \033[0;92mYou are banned from posting\033[0m";
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[New] \033[0;92mUnknown new arguments\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
		else if(strcmp(lines[0], "/set") == 0 && loggedIn)
		{
			if(strcmp(lines[1], "profile") == 0)
			{
				if(strcmp(lines[2], "public") == 0 || strcmp(lines[2], "private") == 0 || strcmp(lines[2], "friends") == 0 )
				{
					char myname[100], path[1000] = "serverdata/users/";
					int userexists = 0;
					if(admin == 1 && lines[3][0] != 0)
					{
						FILE* f = fopen("serverdata/accounts.txt", "r");
						char usr[64], pass[64], id[64];									
						while(1)
						{
							if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(lines[3], usr) == 0)
							{
								userexists = 1;
								fclose(f);
								strcpy(myname, lines[3]);
								strcat(path, myname);
								break;
							}
						}
					}
					else
					{
						userexists = 1;
						pthread_mutex_lock(&lock_update_online);
						strcpy(myname, online[tdL.idThread].username);
						pthread_mutex_unlock(&lock_update_online);
						strcat(path, myname);
					}

					if(userexists)
					{
						DIR *directory;
						directory = opendir(path);
						struct dirent *currentfile;

		   				while ( (currentfile = readdir(directory)) != NULL)
						{
							if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
							{
								char name2[1000] = {0}, names2[1000][1000] = {0};
								strcpy(name2, currentfile->d_name);
								split(name2, names2, "*");
								if(strcmp(names2[0], "profile") == 0)
								{
									char newname[1000] = "serverdata/users/", currentpath[1000] = "serverdata/users/";
									strcat(newname, myname);
									strcat(currentpath, myname);
									strcat(currentpath, "/");
									strcat(currentpath, currentfile->d_name);
									strcat(newname, "/profile*");
									strcat(newname, lines[2]);
									rename(currentpath, newname);
									char msg[1000] = "\033[1;92m[Set] \033[0;92mProfile set to ";
									if(strcmp(lines[2], "public") == 0) strcat(msg, "\033[1;92mpublic\033[0m");
									else if(strcmp(lines[2], "private") == 0) strcat(msg, "\033[1;91mprivate\033[0m");
									else if(strcmp(lines[2], "friends") == 0) strcat(msg, "\033[1;93mfriends only\033[0m");
									if (write (tdL.cl, msg, sizeof(msg)) <= 0)
										perror ("[server]Eroare la write() catre client.\n");
									break;
								}
							}
						}
						closedir(directory);
					}
					else
					{
						char msg[1000] = "\033[1;92m[Set] \033[0;92mUnknown set attribute\033[0m";
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
				else
				{
					char msg[1000] = "\033[1;92m[Set] \033[0;92mUser doesn't exist\033[0m";
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1], "post") == 0)
			{
				if(strcmp(lines[2], "public") == 0 || strcmp(lines[2], "private") == 0 || strcmp(lines[2], "friends") == 0 )
				{
					char myname[100], path[1000] = "serverdata/users/", name[1000];
					int found = 0, userexists = 0;
					if(admin == 1)
					{
						FILE* f = fopen("serverdata/accounts.txt", "r");
						char usr[64], pass[64], id[64];									
						while(1)
						{
							if(fscanf(f,"%s %s %s",id, usr, pass) == EOF)
							{
								fclose(f);
								break;
							}
							if(strcmp(lines[3], usr) == 0)
							{
								userexists = 1;
								fclose(f);
								strcpy(name, lines[4]);
								for(int i = 5; i <= size; i++)
								{
									strcat(name, " ");
									strcat(name, lines[i]);
								}
								strcpy(myname, lines[3]);
								strcat(path, myname);
								strcat(path, "/posts");
								break;
							}
						}
					}
					else
					{
						userexists = 1;
						strcpy(name, lines[3]);
						for(int i = 4; i <= size; i++)
						{
							strcat(name, " ");
							strcat(name, lines[i]);
						}
						pthread_mutex_lock(&lock_update_online);
						strcpy(myname, online[tdL.idThread].username);
						pthread_mutex_unlock(&lock_update_online);
						strcat(path, myname);
						strcat(path, "/posts");
					}

					if(userexists)
					{
						DIR *directory;
						directory = opendir(path);
						struct dirent *currentfile;

		   				while ( (currentfile = readdir(directory)) != NULL)
						{
							if(strcmp(currentfile->d_name, ".") != 0 && strcmp(currentfile->d_name, "..") != 0)
							{
								char name2[1000] = {0}, names2[1000][1000] = {0};
								strcpy(name2, currentfile->d_name);
								split(name2, names2, "*");
								if(strcmp(names2[0], name) == 0)
								{
									found = 1;
									char newname[1000] = "serverdata/users/", currentpath[1000] = "serverdata/users/";
									strcat(newname, myname);
									strcat(currentpath, myname);
									strcat(currentpath, "/posts/");
									strcat(currentpath, currentfile->d_name);
									strcat(newname, "/posts/");
									strcat(newname, names2[0]);
									strcat(newname, "*");
									strcat(newname, lines[2]);
									rename(currentpath, newname);
									char msg[1000] = "\033[1;92m[Set] \033[0;92mPost \033[0;94m[";
									strcat(msg, names2[0]);
									strcat(msg, "] \033[0;92mset to ");
									if(strcmp(lines[2], "public") == 0) strcat(msg, "\033[1;92mpublic\033[0m");
									else if(strcmp(lines[2], "private") == 0) strcat(msg, "\033[1;91mprivate\033[0m");
									else if(strcmp(lines[2], "friends") == 0) strcat(msg, "\033[1;93mfriends only\033[0m");
									if (write (tdL.cl, msg, sizeof(msg)) <= 0)
										perror ("[server]Eroare la write() catre client.\n");
									break;
								}
							}
						}
						closedir(directory);
						if(found == 0)
						{
							char msg[1000] = "\033[1;92m[Set] \033[0;92mPost doesn't exist\033[0m";
							if (write (tdL.cl, msg, sizeof(msg)) <= 0)
								perror ("[server]Eroare la write() catre client.\n");
						}
					}
					else
					{
						char msg[1000] = "\033[1;92m[Set] \033[0;92mUnknown set attribute\033[0m";
						if (write (tdL.cl, msg, sizeof(msg)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
				else
				{
					char msg[1000] = "\033[1;92m[Set] \033[0;92mUser doesn't exist\033[0m";
					if (write (tdL.cl, msg, sizeof(msg)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Set] \033[0;92mUnknown set arguments\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
		else if(strcmp(lines[0], "/ban") == 0 && admin == 1)
		{
			if(strcmp(lines[1], "post") == 0)
			{
				int alreadybanned = 0;
				char usr[100], typeb[100];
				FILE* f = fopen("serverdata/banlist.txt", "r");						
				while(1)
				{
					if(fscanf(f,"%s %s",usr, typeb) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(lines[2], usr) == 0 && strcmp(typeb, "post") == 0)
					{
						alreadybanned = 1;
						fclose(f);
						break;
					}
				}
				if(alreadybanned == 0)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char user[64], pass[64], id[64];						
					int found = 0;					
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], user) == 0)
						{
							found = 1;
							fclose(f);
							break;
						}
					}
					if(found)
					{
						f = fopen("serverdata/banlist.txt", "a");
						fprintf(f, "%s %s\n", lines[2], "post");
						fclose(f);
						strcpy(output, "\033[1;92m[Ban] \033[0;92mYou have been banned from posting\033[0m");
						send_notification(lines[2], output);
						strcpy(output, "\033[1;92m[Ban] \033[0;92mUser has been banned from posting\033[0m");
						if (write (tdL.cl, output, sizeof(output)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
					else
					{
						strcpy(output, "\033[1;92m[Ban] \033[0;92mUser doesn't exist\033[0m");
						if (write (tdL.cl, output, sizeof(output)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
				else
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser is already banned from posting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1], "comment") == 0)
			{
				int alreadybanned = 0;
				char usr[100], typeb[100];
				FILE* f = fopen("serverdata/banlist.txt", "r");						
				while(1)
				{
					if(fscanf(f,"%s %s",usr, typeb) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(lines[2], usr) == 0 && strcmp(typeb, "comment") == 0)
					{
						alreadybanned = 1;
						fclose(f);
						break;
					}
				}
				if(alreadybanned == 0)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char user[64], pass[64], id[64];						
					int found = 0;					
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], user) == 0)
						{
							found = 1;
							fclose(f);
							break;
						}
					}
					if(found)
					{
						f = fopen("serverdata/banlist.txt", "a");
						fprintf(f, "%s %s\n", lines[2], "comment");
						fclose(f);
						strcpy(output, "\033[1;92m[Ban] \033[0;92mYou have been banned from commenting\033[0m");
						send_notification(lines[2], output);
						strcpy(output, "\033[1;92m[Ban] \033[0;92mUser has been banned from commenting\033[0m");
						if (write (tdL.cl, output, sizeof(output)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
					else
					{
						strcpy(output, "\033[1;92m[Ban] \033[0;92mUser doesn't exist\033[0m");
						if (write (tdL.cl, output, sizeof(output)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
				else
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser is already banned from commenting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1], "reaction") == 0)
			{
				int alreadybanned = 0;
				char usr[100], typeb[100];
				FILE* f = fopen("serverdata/banlist.txt", "r");						
				while(1)
				{
					if(fscanf(f,"%s %s",usr, typeb) == EOF)
					{
						fclose(f);
						break;
					}
					if(strcmp(lines[2], usr) == 0 && strcmp(typeb, "reaction") == 0)
					{
						alreadybanned = 1;
						fclose(f);
						break;
					}
				}
				if(alreadybanned == 0)
				{
					FILE* f = fopen("serverdata/accounts.txt", "r");
					char user[64], pass[64], id[64];						
					int found = 0;					
					while(1)
					{
						if(fscanf(f,"%s %s %s",id, user, pass) == EOF)
						{
							fclose(f);
							break;
						}
						if(strcmp(lines[2], user) == 0)
						{
							found = 1;
							fclose(f);
							break;
						}
					}
					if(found)
					{
						f = fopen("serverdata/banlist.txt", "a");
						fprintf(f, "%s %s\n", lines[2], "reaction");
						fclose(f);
						strcpy(output, "\033[1;92m[Ban] \033[0;92mYou have been banned from reacting\033[0m");
						send_notification(lines[2], output);
						strcpy(output, "\033[1;92m[Ban] \033[0;92mUser has been banned from reacting\033[0m");
						if (write (tdL.cl, output, sizeof(output)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
					else
					{
						strcpy(output, "\033[1;92m[Ban] \033[0;92mUser doesn't exist\033[0m");
						if (write (tdL.cl, output, sizeof(output)) <= 0)
							perror ("[server]Eroare la write() catre client.\n");
					}
				}
				else
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser is already banned from reacting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Ban] \033[0;92mUnknown ban arguments\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
		else if(strcmp(lines[0], "/unban") == 0 && admin == 1)
		{
			if(strcmp(lines[1], "post") == 0)
			{
				char path1[1000] = "serverdata/banlist.txt", path2[1000] = "serverdata/banlist2.txt", usr[100], typeb[100];
				int found = 0;
				FILE* f = fopen(path1, "r");
				FILE* f2 = fopen(path2, "a");
				while(1)
				{
					if(fscanf(f,"%s %s", usr, typeb) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					if(strcmp(usr, lines[2]) != 0 || (strcmp(usr, lines[2]) == 0 && strcmp(typeb, "post") != 0))
					{
						fprintf(f2, "%s %s\n", usr, typeb);
					}
					else if(strcmp(usr, lines[2]) == 0 && strcmp(typeb, "post") == 0) found = 1;
				}
				f = fopen(path1, "w");
				f2 = fopen(path2, "r");
				while(1)
				{
					if(fscanf(f2,"%s %s", usr, typeb) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					fprintf(f, "%s %s\n", usr, typeb);
				}
				unlink(path2);
				if(found)
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mYou have been unbanned from posting\033[0m");
					send_notification(lines[2], output);
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser has been unbanned from posting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
				else
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser isn't banned from posting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1], "comment") == 0)
			{
				char path1[1000] = "serverdata/banlist.txt", path2[1000] = "serverdata/banlist2.txt", usr[100], typeb[100];
				int found = 0;
				FILE* f = fopen(path1, "r");
				FILE* f2 = fopen(path2, "a");
				while(1)
				{
					if(fscanf(f,"%s %s", usr, typeb) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					if(strcmp(usr, lines[2]) != 0 || (strcmp(usr, lines[2]) == 0 && strcmp(typeb, "comment") != 0))
					{
						fprintf(f2, "%s %s\n", usr, typeb);
					}
					else if(strcmp(usr, lines[2]) == 0 && strcmp(typeb, "comment") == 0) found = 1;
				}
				f = fopen(path1, "w");
				f2 = fopen(path2, "r");
				while(1)
				{
					if(fscanf(f2,"%s %s", usr, typeb) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					fprintf(f, "%s %s\n", usr, typeb);
				}
				unlink(path2);
				if(found)
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mYou have been unbanned from commenting\033[0m");
					send_notification(lines[2], output);
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser has been unbanned from commenting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
				else
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser isn't banned from commenting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else if(strcmp(lines[1], "reaction") == 0)
			{
				char path1[1000] = "serverdata/banlist.txt", path2[1000] = "serverdata/banlist2.txt", usr[100], typeb[100];
				int found = 0;
				FILE* f = fopen(path1, "r");
				FILE* f2 = fopen(path2, "a");
				while(1)
				{
					if(fscanf(f,"%s %s", usr, typeb) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					if(strcmp(usr, lines[2]) != 0 || (strcmp(usr, lines[2]) == 0 && strcmp(typeb, "reaction") != 0))
					{
						fprintf(f2, "%s %s\n", usr, typeb);
					}
					else if(strcmp(usr, lines[2]) == 0 && strcmp(typeb, "reaction") == 0) found = 1;
				}
				f = fopen(path1, "w");
				f2 = fopen(path2, "r");
				while(1)
				{
					if(fscanf(f2,"%s %s", usr, typeb) == EOF)
					{
						fclose(f);
						fclose(f2);
						break;
					}
					fprintf(f, "%s %s\n", usr, typeb);
				}
				unlink(path2);
				if(found)
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mYou have been unbanned from reacting\033[0m");
					send_notification(lines[2], output);
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser has been unbanned from reacting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
				else
				{
					strcpy(output, "\033[1;92m[Ban] \033[0;92mUser isn't banned from reacting\033[0m");
					if (write (tdL.cl, output, sizeof(output)) <= 0)
						perror ("[server]Eroare la write() catre client.\n");
				}
			}
			else
			{
				char msg[1000] = "\033[1;92m[Unban] \033[0;92mUnknown unban arguments\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
		else if(strcmp(lines[0], "/clear") == 0 || strcmp(lines[0], "clear") == 0)
		{
			char msg[1000] = "\033[2J\033[H";
			if (write (tdL.cl, msg, sizeof(msg)) <= 0)
				perror ("[server]Eroare la write() catre client.\n");
		}
		else if(strcmp(lines[0], "/help") == 0)
		{
			if(loggedIn == 0)
			{
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 1;
				pthread_mutex_unlock(&lock_update_online);
				char msg[1000] = "\033[2J\033[H\033[1;92m[Help] \033[0;92mList of available commands as \033[0;94m[Guest]\033[0;92m:\n---------------------------------------------\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[1] \033[0;92m/create - create a new account\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[2] \033[0;92m/login \033[0;36m[username] [password] \033[0;92m- login as \033[0;36m[username]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[3] \033[0;92m/show users - shows users and their status (online/offline)\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[4] \033[0;92m/view post \033[0;36m[user] [postname] \033[0;92m- shows post \033[0;36m[post] \033[0;92mof the user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[5] \033[0;92m/view profile \033[0;36m[user] \033[0;92m- shows profile of \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[6] \033[0;92m/show reactions post \033[0;36m[user] [postname] \033[0;92m- shows reactions of the post \033[0;36m[post] \033[0;92mfrom the user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[7] \033[0;92m/show reactions comment \033[0;36m[user1] \033[0;92mfrom  \033[0;36m[user2] [postname] \033[0;92m- shows reactions of the comment posted by\033[0;36m[user] \033[0;92mfrom the post \033[0;36m[postname] \033[0;92mof the user \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[8] \033[0;92m/exit - exists the session\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;92m---------------------------------------------\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 0;
				pthread_mutex_unlock(&lock_update_online);
			}
			else if(admin == 0 && loggedIn == 1)
			{
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 1;
				pthread_mutex_unlock(&lock_update_online);
				char msg[1000] = "\033[2J\033[H\033[1;92m[Help] \033[0;92mList of available commands as \033[0;94m[user]\033[0;92m:\n---------------------------------------------\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[1] \033[0;92m/chat \033[0;36m[user1] [user2] ... [usern] \033[0;92m- create a chat room with \033[0;36m[user1] [user2] ... [usern] \033[0;92mif they exist\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[2] \033[0;92m/join \033[0;36m[room name] \033[0;92m- join chat room \033[0;36m[room name] \033[0;92mif it exists\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[3] \033[0;92m/view chats - shows your chats\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[4] \033[0;92m/view chat \033[0;36m[chat name]\033[0;92m- shows chat \033[0;36m[chat name]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[5] \033[0;92m/view post \033[0;36m[user] [postname] \033[0;92m- shows post \033[0;36m[post] \033[0;92mof the user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[6] \033[0;92m/view profile \033[0;36m[user] \033[0;92m- shows profile of \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[7] \033[0;92m/delete chat \033[0;36m[chat name] \033[0;92m- deletes chat \033[0;36m[chat name]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[8] \033[0;92m/delete post \033[0;36m[post name] \033[0;92m- deletes post \033[0;36m[post name]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[9] \033[0;92m/delete comment \033[0;36m[user] [post name] \033[0;92m- deletes your comment from post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[10] \033[0;92m/delete reaction post \033[0;36m[user] [post name] \033[0;92m- deletes your reaction from post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[11] \033[0;92m/delete reaction comment \033[0;36m[user1] \033[0;92mfrom \033[0;36m [user2] [post name] \033[0;92m- deletes your reaction from a comment made by \033[0;36m[user1] \033[0;92min a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[12] \033[0;92m/add friend \033[0;36m[friend name] [type]\033[0;92m- send a friend notification to \033[0;36m[friend name] \033[0;92mas \033[0;36m[type]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[13] \033[0;92m/add comment \033[0;36m[user] [post name]\033[0;92m- add a comment in a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[14] \033[0;92m/add reaction post \033[0;36m[user] [post name]\033[0;92m- add a reaction in a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[15] \033[0;92m/add reaction comment \033[0;36m[user1] \033[0;92mfrom \033[0;36m[user2] [post name]\033[0;92m- add a reaction to a comment made by \033[0;36m[user1] \033[0;92in a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[16] \033[0;92m/confirm friend \033[0;36m[friend name] [type]\033[0;92m- confirm a friend request from \033[0;36m[friend name] \033[0;92mas \033[0;36m[type]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[17] \033[0;92m/edit profile - edit your profile\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[18] \033[0;92m/new update \033[0;36m[update name]\033[0;92m- send an update \033[0;36m[update name]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[19] \033[0;92m/new post \033[0;36m[post name]\033[0;92m- create a new post \033[0;36m[update name]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[20] \033[0;92m/set profile \033[0;36m[public/private/friends]\033[0;92m- set profile to \033[0;36m[public/private/friends]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[21] \033[0;92m/set post \033[0;36m[public/private/friends]\033[0;92m- set post to \033[0;36m[public/private/friends]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[22] \033[0;92m/decline friend \033[0;36m[friend name] [type]\033[0;92m- decline a friend request from \033[0;36m[friend name] \033[0;92mas \033[0;36m[type]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[23] \033[0;92m/show users - shows users and their status (online/offline)\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[24] \033[0;92m/show friends - shows friends and their status (online/offline)\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[25] \033[0;92m/show reactions post \033[0;36m[user] [postname] \033[0;92m- shows reactions of the post \033[0;36m[post] \033[0;92mfrom the user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[26] \033[0;92m/show reactions comment \033[0;36m[user1] \033[0;92mfrom  \033[0;36m[user2] [postname] \033[0;92m- shows reactions of the comment posted by \033[0;36m[user] \033[0;92mfrom the post \033[0;36m[postname] \033[0;92mof the user \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[27] \033[0;92m/exit - exists the session\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;92m---------------------------------------------\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 0;
				pthread_mutex_unlock(&lock_update_online);
			}
			else
			{
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 1;
				pthread_mutex_unlock(&lock_update_online);
				char msg[1000] = "\033[2J\033[H\033[1;92m[Help] \033[0;92mList of available commands as \033[0;94m[admin]\033[0;92m:\n---------------------------------------------\033[0m";
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[1] \033[0;92m/chat \033[0;36m[user1] [user2] ... [usern] \033[0;92m- create a chat room with \033[0;36m[user1] [user2] ... [usern] \033[0;92mif they exist\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[2] \033[0;92m/join \033[0;36m[room name] \033[0;92m- join chat room \033[0;36m[room name] \033[0;92mif it exists\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[3] \033[0;92m/view chats \033[0;36m - show chats of user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[4] \033[0;92m/view chat \033[0;36m[user] [chat name]\033[0;92m- shows chat \033[0;36m[chat name] \033[0;92mfrom user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[5] \033[0;92m/view post \033[0;36m[user] [postname] \033[0;92m- shows post \033[0;36m[post] \033[0;92mof the user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[6] \033[0;92m/view profile \033[0;36m[user] \033[0;92m- shows profile of \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[7] \033[0;92m/delete chat \033[0;36m[user] [chat name] \033[0;92m- deletes chat \033[0;36m[chat name] \033[0;92mfrom user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[8] \033[0;92m/delete post \033[0;36m[user] [post name] \033[0;92m- deletes post \033[0;36m[post name] \033[0;92mfrom user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[9] \033[0;92m/delete comment \033[0;36m[user1] [user2] [post name] \033[0;92m- deletes comment made by \033[0;36m[user1]  \033[0;92mfrom post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[10] \033[0;92m/delete reaction post \033[0;36m[user1] [user2] [post name] \033[0;92m- deletes reaction of \033[0;36m[user1] \033[0;92mfrom post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[11] \033[0;92m/delete reaction comment \033[0;36m[user1] [user2] [user3] [post name] \033[0;92m- deletes reaction of \033[0;36m[user1] \033[0;92mfrom a comment made by \033[0;36m[user2] \033[0;92min a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user3]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[12] \033[0;92m/add friend \033[0;36m[friend name] [type]\033[0;92m- send a friend notification to \033[0;36m[friend name] \033[0;92mas \033[0;36m[type]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[13] \033[0;92m/add comment \033[0;36m[user] [post name]\033[0;92m- add a comment in a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[14] \033[0;92m/add reaction post \033[0;36m[user] [post name]\033[0;92m- add a reaction in a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[15] \033[0;92m/add reaction comment \033[0;36m[user1] \033[0;92mfrom \033[0;36m[user2] [post name]\033[0;92m- add a reaction to a comment made by \033[0;36m[user1] \033[0;92in a post \033[0;36m[post name] \033[0;92mmade by \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[16] \033[0;92m/confirm friend \033[0;36m[friend name] [type]\033[0;92m- confirm a friend request from \033[0;36m[friend name] \033[0;92mas \033[0;36m[type]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[17] \033[0;92m/edit profile \033[0;36m[user] \033[0;92m- edits profile of user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[18] \033[0;92m/new update \033[0;36m[update name]\033[0;92m- send an update \033[0;36m[update name]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[19] \033[0;92m/new post \033[0;36m[post name]\033[0;92m- create a new post \033[0;36m[update name]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[20] \033[0;92m/set profile \033[0;36m[public/private/friends] [user]\033[0;92m- set profile of \033[0;36m[user] \033[0;92mto \033[0;36m[public/private/friends]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[21] \033[0;92m/set post \033[0;36m[public/private/friends] [user]\033[0;92m- set post of \033[0;36m[user] \033[0;92mto \033[0;36m[public/private/friends]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[22] \033[0;92m/decline friend \033[0;36m[friend name] [type]\033[0;92m- decline a friend request from \033[0;36m[friend name] \033[0;92mas \033[0;36m[type]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[23] \033[0;92m/show users - shows users and their status (online/offline)\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[24] \033[0;92m/show friends \033[0;36m[user] \033[0;92m- shows friends of \033[0;36m[user] \033[0;92mand their status (online/offline)\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[25] \033[0;92m/show reactions post \033[0;36m[user] [postname] \033[0;92m- shows reactions of the post \033[0;36m[post] \033[0;92mfrom the user \033[0;36m[user]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[26] \033[0;92m/show reactions comment \033[0;36m[user1] \033[0;92mfrom  \033[0;36m[user2] [postname] \033[0;92m- shows reactions of the comment posted by \033[0;36m[user] \033[0;92mfrom the post \033[0;36m[postname] \033[0;92mof the user \033[0;36m[user2]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[27] \033[0;92m/ban user \033[0;36m[user] [post/comment] \033[0;92m- ban user \033[0;36m[user] \033[0;92mfrom \033[0;36m[post/comment]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[28] \033[0;92m/unban user \033[0;36m[user] [post/comment] \033[0;92m- unban user \033[0;36m[user] \033[0;92mfrom \033[0;36m[post/comment]\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;94m[29] \033[0;92m/exit - exists the session\033[0m"); 
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				strcpy(msg, "\033[0;92m---------------------------------------------\033[0m");
				if (write (tdL.cl, msg, sizeof(msg)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
				pthread_mutex_lock(&lock_update_online);
				online[tdL.idThread].busy = 0;
				pthread_mutex_unlock(&lock_update_online);
			}
		}
		else
		{
			if(loggedIn == 0 && (strcmp(lines[0], "/set") == 0 || strcmp(lines[0], "/edit") == 0 || strcmp(lines[0], "/new") == 0 || strcmp(lines[0], "/add") == 0 || strcmp(lines[0], "/decline") == 0 || strcmp(lines[0], "/confirm") == 0 || strcmp(lines[0], "/delete") == 0 || strcmp(lines[0], "/join") == 0 || strcmp(lines[0], "/chat") == 0))
			{
				strcpy(output, "\033[0;92mYou must be logged in to use that command\033[0m");
				if (write (tdL.cl, output, sizeof(output)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
			else if(admin == 0 && (strcmp(lines[0], "/ban") == 0 || strcmp(lines[0], "/unban") == 0))
			{
				strcpy(output, "\033[0;92mYou must be an admin to use that command\033[0m");
				if (write (tdL.cl, output, sizeof(output)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
			else
			{
				strcpy(output, "\033[0;92mUnknown command! Type /help for more information\033[0m");
				if (write (tdL.cl, output, sizeof(output)) <= 0)
					perror ("[server]Eroare la write() catre client.\n");
			}
		}
	}

	/* am terminat cu acest client, inchidem conexiunea */
	close ((intptr_t)arg);
	return(NULL);	
};
