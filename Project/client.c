#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define LENGTH 2048
#define ParityArrSize 9

// Global variables
volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[32];
int err_flag = 0;

void str_overwrite_stdout() {
    printf("%s", "> ");
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

void catch_ctrl_c_and_exit(int sig) {
    flag = 1;
}

/*    code SPC   */
char * add_parity2str(char *strTemp)
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

    return str;
}
/*  end */

void send_msg_handler() {
    char message[LENGTH] = {};
    char buffer[LENGTH + 32] = {};
    char *msg_code = (char *) malloc(sizeof(char) * LENGTH + 32);
    while (1) {
        str_overwrite_stdout();

        fgets(message, LENGTH, stdin);

        if (strcmp(message, "exit") == 0)
            break;

        sprintf(buffer, "%s: %s\n", name, message);
        msg_code = add_parity2str(buffer);

        //str_trim_lf(msg_code, LENGTH+32);
        send(sockfd, msg_code, strlen(msg_code), 0);

        strcpy(msg_code, "");
        bzero(message, LENGTH);
        bzero(buffer, LENGTH + 32);

    }
    catch_ctrl_c_and_exit(2);
}

/*    decode SPC  */
int parityChackError(char *str){
    char *token,*A,*B;
    int i,j,k,p=0;
    token = strtok(str, "|");
    A = strtok(NULL, "|");
    B = strtok(NULL, "|");
    int arr[ParityArrSize]={0};
    for(i=0;i<strlen(token);i++){
        char c=token[i];
        p=0;
        k=0;
        for (j = 7; j >= 0; --j,k++)
        {
            if(c & (1 << j)){
                p=(++p)%2;
                arr[k]=(++arr[k]%2);
            }
        }
        arr[k]=(p+arr[k]%2);
        if(A[i]!=(p+'0'))
            return 0;
    }
    for(i=0;i<strlen(B);i++){
        if(B[i]!=(arr[i]+'0'))
            return 0;
    }
    return 1;
}

char * remove_parity2str(char *str)
{

    if(parityChackError(str)==0){
        err_flag = 1;
        return strcpy(str,"!Hata");
    }else{
        err_flag = 0;
        return strtok(str,"|");
    }

}

/* end */

void recv_msg_handler() {
    char message[LENGTH] = {};
    char *msg_text = (char*)malloc(sizeof(char) * LENGTH);
    while (1) {
        int receive = recv(sockfd, message, LENGTH, 0);
//        printf("ckeck : %s-\n",message);
        msg_text = remove_parity2str(message);

        if (receive > 0) {
            printf("%s\n", msg_text);
            str_overwrite_stdout();
        } else if (receive == 0) {
            break;
        } else {
            // -1
        }

        strcpy(msg_text, ""); // msg reset
        memset(message, 0, sizeof(message));
    }

}

int main(int argc, char **argv) {

    char *ip = "127.0.0.1";
    int port = 4444;
    signal(SIGINT, catch_ctrl_c_and_exit);

    printf("Please enter your name: ");
    fgets(name, 32, stdin);
    str_trim_lf(name, strlen(name));

    /* name size error */
    if (strlen(name) > 32 || strlen(name) < 2) {
        printf("Name must be less than 30 and more than 2 characters.\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;

    /* Socket settings */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);


    // Connect to Server
    int err = connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (err == -1) {
        printf("ERROR: connect\n");
        return EXIT_FAILURE;
    }

    // Send name
    send(sockfd, name, 32, 0);

    printf("=== WELCOME TO THE CHATROOM ===\n");

    pthread_t recv_msg_thread;
    if (pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0) {
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0) {
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    while (1) {
        if (flag) {
            printf("\nBye\n");
            break;
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
