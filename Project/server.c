#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#include <time.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define ParityArrSize 9

static _Atomic unsigned int cli_count = 0;
static int uid = 10;

/* Client structure */
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char name[32];
    char login_date[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout() {
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf(char *arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

char*get_time(){

    time_t rawtime;
    struct tm *info;
    time( &rawtime );
    info = localtime( &rawtime );
    return(asctime(info));
}

void printToLog(char *str){
    FILE *fptr = fopen("log.txt","a");
    fseek(fptr, 0, SEEK_END);
    fprintf(fptr,"%s\n",str);
    fclose(fptr);
}

void print_client_addr(struct sockaddr_in addr) {
    printf("%d.%d.%d.%d",
           addr.sin_addr.s_addr & 0xff,
           (addr.sin_addr.s_addr & 0xff00) >> 8,
           (addr.sin_addr.s_addr & 0xff0000) >> 16,
           (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Add clients to queue */
void queue_add(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients to queue */
void queue_remove(int uid) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            if (clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/*    binary to text  SPC  */
char * add_parity2str(char *strTemp,int err_flag)
{
    char *str=(char*)calloc(sizeof(char),1024);
    strcpy(str,strTemp);
    strcat(str,"|");
    int parity[ParityArrSize]={0};

    int i=0,j=0,k=0,p=0;
    for(i=0;i<strlen(strTemp);i++)
    {
        char c = strTemp[i];
        p=0;
        k=0;
        for (j = 7; j >= 0; --j,k++)
        {
            if(c & (1 << j)){
                p=(++p)%2;
                parity[k]=(++parity[k]%2);
            }
        }
        parity[k]=(p+parity[k]%2);
        str[strlen(str)]=p+'0';
    }
    strcat(str,"|");
    for(i=0;i<(sizeof(parity)/sizeof(parity[0]));i++){
        str[strlen(str)]=parity[i]+'0';
    }

    if(err_flag == 1){
        if(str[strlen(str)-1] == '1'){
            str[strlen(str)-1] = '0';
        }else{
            str[strlen(str)-1] = '1';
        }
    }

    return str;
}

/* end */


/*    text to binary SPC   */
char * remove_parity2str(char *str)
{
    return strtok(str,"|");
}
/* end */

char *get_reciver_name(char *s) {
    char *reciverClientName = (char *) malloc(BUFFER_SZ * sizeof(char));
    strcpy(reciverClientName, s);
    reciverClientName = strtok(reciverClientName, ": ");
    reciverClientName = strtok(NULL, " -> ");

    return reciverClientName;
}

int is_error_msg(char *s) {
    char *message = (char *) malloc(BUFFER_SZ * sizeof(char));
    strcpy(message, s);
    message = strtok(message, ": ");
    message = strtok(NULL, " ");
    return strcmp(message, "!Hata\n") == 0;
}

char * login_list(){
    char *lgn_list = (char *) malloc(sizeof(char) * BUFFER_SZ);
    char name[64];
    strcat(lgn_list,"< Name >\n");

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            sprintf(name, "  < %s > \n",clients[i]->name);
            strcat(lgn_list,name);
        }
    }

    return lgn_list;
}


/* Send message to all clients except sender */
void send_message(char *s, int uid) {
    pthread_mutex_lock(&clients_mutex);

    // set random time to err_flag
    srand(time(NULL));
    int err_flag = rand()%2;
    int login_flag = 0;
    int skip_flag = 0;
    int exit_time_flag = 0;

    // get receiver client name
    char *to_client_name = get_reciver_name(s);

    // set to client
    client_t *to_client = (client_t *) malloc(sizeof(client_t));
    client_t *from_client = (client_t *) malloc(sizeof(client_t));


    // if First Login dont send error mess
    if (strcmp(to_client_name, "Name") == 0) {
        err_flag = 0;
        login_flag = 1;
    }

    /* exit time */
    if (strcmp(to_client_name, "has") == 0) {
        err_flag = 0;
        exit_time_flag = 1;
    }

    /* s with err detection part */
    char *msg_recv = (char *) malloc(sizeof(char) * BUFFER_SZ);
    msg_recv = add_parity2str(s,err_flag);


    // find the receiver client and sender client
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            // find the sender client
            if (clients[i]->uid == uid) {
                from_client = clients[i];
            }
            // find the receiver client
            if (strcmp(clients[i]->name, to_client_name) == 0) {
                to_client = clients[i];
            }
        }
    }


    /* first login time send list to log in client */
    if(login_flag == 1){
        if (write(from_client->sockfd, msg_recv, strlen(msg_recv)) < 0) {
            perror("ERROR: write to descriptor failed");
        }
    }


    /* send all client list*/
    if (exit_time_flag == 0 && login_flag == 0 && strcmp(strchr(s, ':'),": --list all\n\n") == 0){
        sprintf(msg_recv, "%s", login_list());
        msg_recv = add_parity2str(msg_recv,0);
        if (write(from_client->sockfd, msg_recv, strlen(msg_recv)) < 0) {
            perror("ERROR: write to descriptor failed");
        }
        skip_flag = 1;
    }


    // if you message yourself
    if (to_client == from_client) {
        char *err = "You cant send yourself\n";
        err = add_parity2str(err,0);
        if (write(from_client->sockfd, err, strlen(err)) < 0) {
            perror("ERROR: write to descriptor failed");
        }
        skip_flag = 1;
    }

    /* send message to other clients */
    if (skip_flag == 0){

        // private message send
        if (to_client->uid != NULL) {
            if (write(to_client->sockfd, msg_recv, strlen(msg_recv)) < 0) {
                perror("ERROR: write to descriptor failed");
            }

        // global message send
        } else {

            /* login time send other client to message */
            if(login_flag == 1){
                sprintf(msg_recv, "%s has joined - %s\n", from_client->name,from_client->login_date);
                printToLog(msg_recv);
                msg_recv = add_parity2str(msg_recv,0);
            }

            /* send message to client */
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (clients[i]) {
                    if (clients[i]->uid != uid) {

                        if (write(clients[i]->sockfd, msg_recv, strlen(msg_recv)) < 0) {
                            perror("ERROR: write to descriptor failed");
                        }
                    }
                }
            }

        }
    }


    pthread_mutex_unlock(&clients_mutex);
}



/* Handle all communication with the client */
void *handle_client(void *arg) {
    char buff_out[BUFFER_SZ];
    char name[32];
    int leave_flag = 0;

    char *msg_rec = (char *) malloc(sizeof(char) * BUFFER_SZ);

    cli_count++;
    client_t *cli = (client_t *) arg;

    // Name
    if (recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) < 2 || strlen(name) >= 32 - 1) {
        printf("Didn't enter the name.\n");
        leave_flag = 1;
    } else {
        /* first login time */
        strcpy(cli->name, name); // set name
        strcpy(cli->login_date,get_time()); // set login time

        sprintf(buff_out, "%s", login_list());
        printf("%s", buff_out);
        printToLog(buff_out); // write to file

        send_message(buff_out, cli->uid);
    }

    bzero(buff_out, BUFFER_SZ);

    while (1) {

        if (leave_flag) {
            break;
        }

        /* get message from client */
        int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);

        /* remove check part */
        msg_rec = remove_parity2str(buff_out);

        if (receive > 0 && strcmp(strchr(msg_rec, ':'), ": exit\n\n") != 0) {
            /* if message not null */
            if (strlen(buff_out) > 0) {
                /* send message */
                send_message(msg_rec, cli->uid);

                /* print the msg */
                str_trim_lf(msg_rec, strlen(msg_rec));
                printf("%s -> %s\n", msg_rec, cli->name);
                printToLog(msg_rec); // write to file
            }
        /* exit the client */
        } else if (receive == 0 || strcmp(strchr(msg_rec, ':'), ": exit\n\n") == 0) {
            sprintf(buff_out, "%s has left\n", cli->name);
            send_message(buff_out, cli->uid);
            printToLog(buff_out); // write to file
            leave_flag = 1;
        } else {
            printf("ERROR: -1\n");
            leave_flag = 1;
        }

        /* reset string*/
        strcpy(msg_rec, "");
        bzero(buff_out, BUFFER_SZ);
    }

    /* Delete client from queue and yield thread */
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char **argv) {

    char *ip = "127.0.0.1";
    int port = 4444;
    int option = 1;
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);

    if (setsockopt(listenfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char *) &option, sizeof(option)) < 0) {
        perror("ERROR: setsockopt failed");
        return EXIT_FAILURE;
    }

    /* Bind */
    if (bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: Socket binding failed");
        return EXIT_FAILURE;
    }

    /* Listen */
    if (listen(listenfd, 10) < 0) {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
    }

    printf("=== WELCOME TO THE CHATROOM ===\n");

    while (1) {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &clilen);

        /* Check if max clients is reached */
        if ((cli_count + 1) == MAX_CLIENTS) {
            printf("Max clients reached. Rejected: ");
            print_client_addr(cli_addr);
            printf(":%d\n", cli_addr.sin_port);
            close(connfd);
            continue;
        }

        /* Client settings */
        client_t *cli = (client_t *) malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;

        /* Add client to the queue and fork thread */
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void *) cli);

        /* Reduce CPU usage */
        sleep(1);
    }

    return EXIT_SUCCESS;
}
