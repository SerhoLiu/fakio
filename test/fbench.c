/*
 * Simple Fakio socks5 server benchmark from webbench
 *
 * Usage:
 *   webbench --help
 * 
 */ 
#include <unistd.h>
#include <rpc/types.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* Socks5 define */
#define SOCKS_VER 0x05
#define SOCKS_CONNECT 0x01
#define SOCKS_ATYPE_IPV4 0x01
#define SOCKS_ATYPE_DNAME 0x03

#define REQUEST_SIZE 2048
/* values */
volatile int timerexpired=0;
int speed = 0;
int failed = 0;
int bytes = 0;

int clients = 1;
int benchtime = 30;

int get_port = 80;
int socks5_port = 8000;

/* internal */
int mypipe[2];

char host[MAXHOSTNAMELEN];
char socks5_server[MAXHOSTNAMELEN];
char request_t[REQUEST_SIZE];
char socks5_request[REQUEST_SIZE];

static const struct option long_options[]=
{
    {"time",  required_argument,  NULL, 't'},
    {"help", no_argument, NULL, '?'},
    {"clients", required_argument, NULL, 'c'},
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}
};

static int b_socket(const char *host, int clientPort);
static void benchcore(const char* host, int port, const char *request_t);
static int bench(void);
static void build_request_t(const char *url);

static void alarm_handler(int signal)
{
    timerexpired = 1;
}

static void usage(void)
{
   fprintf(stderr,
	"fbench [option]... URL\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
    "  -s|--server <ip>         Run test socks5 server ip address.\n"
    "  -p|--port <num>          Run test socks5 server port. Default 8000.\n"
	"  -?|-h|--help             This information.\n"
	);
};

int main(int argc, char *argv[])
{
    int opt = 0;
    int options_index = 0;
    if(argc==1) {
        usage();
        return 2;
    } 

    while ((opt = getopt_long(argc, argv, "t:c:s:p:?h",
                  long_options, &options_index))!=EOF) {
        switch (opt) {
            case  0 : break;
            case 't': benchtime = atoi(optarg); break;
            case ':':
            case 'h':
            case '?': usage(); return 2; break;
            case 'c': clients = atoi(optarg); break;
            case 'p': socks5_port = atoi(optarg); break;
            case 's': strcpy(socks5_server, optarg); break;
        }
    }
 
    if (optind == argc) {
        fprintf(stderr, "fbench: Missing URL!\n");
		usage();
		return 2;
    }

    if (clients == 0) clients = 1;
    if (benchtime == 0) benchtime = 60;
    if (socks5_port == 0) socks5_port = 8000;

    int temp;
    if (inet_pton(AF_INET, socks5_server, &temp) != 1) {
        fprintf(stderr, "fbench: Missing socks5 server!\n");
        usage();
        return 2;
    }

    /* Copyright */
    fprintf(stderr, "Fbench - By WebBenchmark\n\n");
    build_request_t(argv[optind]);
    printf("\nBenchmarking: ");
    printf(" %s ",argv[optind]);
    if (clients==1) {
        printf("1 client");
    } else {
        printf("%d clients",clients);
    }
    
    printf(", running %d sec.\n", benchtime);
    return bench();
}

void build_request_t(const char *url)
{
    int i;
    char tmp[10];

    bzero(host, MAXHOSTNAMELEN);
    bzero(request_t, REQUEST_SIZE);

    strcpy(request_t,"GET ");     

    if (strstr(url,"://") == NULL) {
        fprintf(stderr, "\n%s: is not a valid URL.\n",url);
        exit(2);
    }
    
    if (strlen(url) > 1500) {
        fprintf(stderr, "URL is too long.\n");
        exit(2);
    }
    
    if (strncasecmp("http://", url, 7) != 0) { 
        fprintf(stderr, "\nOnly HTTP protocol is directly supported.\n");
        exit(2);
    }
  
    i = strstr(url, "://") - url + 3;
    if (strchr(url+i,'/') == NULL) {
        fprintf(stderr, "\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    
    if(index(url+i,':')!=NULL && index(url+i,':')<index(url+i,'/')) {
       strncpy(host, url+i, strchr(url+i,':')-url-i);
       bzero(tmp,10);
       strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
       get_port = atoi(tmp);
       if (get_port == 0) get_port = 80;
    } else {
        strncpy(host,url+i,strcspn(url+i,"/"));
    }

    strcat(request_t+strlen(request_t), url+i+strcspn(url+i,"/"));
    strcat(request_t," HTTP/1.1");
    strcat(request_t,"\r\n");
    strcat(request_t,"User-Agent: FakioBench\r\n");
    strcat(request_t,"Host: ");
    strcat(request_t,host);
    strcat(request_t,"\r\n");
    strcat(request_t,"Connection: close\r\n\r\n");
}

static int build_socks5_request()
{
    bzero(socks5_request, REQUEST_SIZE);
    socks5_request[0] = SOCKS_VER;
    socks5_request[1] = SOCKS_CONNECT;
    socks5_request[2] = 0x00;
    socks5_request[3] = SOCKS_ATYPE_DNAME;

    uint8_t domain_len = strlen(socks5_server);
    socks5_request[4] = domain_len;
    
    int i;
    for (i = 0; i < domain_len; i++) {
            socks5_request[5+i] = host[i];
    }

    uint16_t ports = htons(get_port);
    *(uint16_t *)(socks5_request+5+domain_len) = ports;
    return (7 + domain_len);
}


static int bench(void)
{
    int i,j,k;    
    pid_t pid = 0;
    FILE *f;

    /* check avaibility of target server */
    i = b_socket(socks5_server, socks5_port);
    if (i < 0) { 
        fprintf(stderr, "\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    close(i);
  
    /* create pipe */
    if (pipe(mypipe)) {
        perror("pipe failed.");
        return 3;
    }

    /* fork childs */
    for (i = 0; i < clients; i++) {
        pid = fork();
        if(pid <= 0) {
            sleep(1); /* make childs faster */
            break;
        }
    }

    if (pid < 0) {
        fprintf(stderr, "problems forking worker no. %d\n", i);
        perror("fork failed.");
        return 3;
    }

    if (pid == 0) {
        /* I am a child */
        benchcore(host, get_port, request_t);

        /* write results to pipe */
        f = fdopen(mypipe[1], "w");
        if (f == NULL) {
            perror("open pipe for writing failed.");
            return 3;
        }
        /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
        fprintf(f,"%d %d %d\n",speed, failed, bytes);
        fclose(f);
        return 0;
    } else {
        f = fdopen(mypipe[0],"r");
        if (f == NULL) {
            perror("open pipe for reading failed.");
            return 3;
        }
        setvbuf(f, NULL, _IONBF, 0);
        speed = failed = bytes = 0;

        while (1) {
            pid = fscanf(f, "%d %d %d", &i, &j, &k);
            if(pid<2) {
                fprintf(stderr,"Some of our childrens died.\n");
                break;
            }
            speed += i;
            failed += j;
            bytes += k;
            /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
            if(--clients == 0) break;
        }
        fclose(f);

        printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
                (int)((speed+failed)/(benchtime/60.0f)),
                (int)(bytes/(float)benchtime),
                speed,
                failed);
    }

    return i;
}

void benchcore(const char *host,const int port,const char *req)
{
    int get_rlen, socks_rlen;
    char buf[1500];
    int s, i;
    struct sigaction sa;

    /* setup alarm signal handler */
    sa.sa_handler = alarm_handler;
    sa.sa_flags=0;
    if (sigaction(SIGALRM, &sa, NULL)) {
        exit(3);
    }
    alarm(benchtime);

    get_rlen = strlen(req);
    socks_rlen = build_socks5_request();
    
    nexttry: while(1) {
        if (timerexpired) {
            if (failed>0) {
                failed--;
            }
            return;
        }
    
        s = b_socket(socks5_server, socks5_port);                          
        if (s < 0) {
            failed++;
            continue;
        }
        if (socks_rlen != write(s, socks5_request, socks_rlen)) {
            failed++;
            close(s);
            continue;
        } 
        if (get_rlen != write(s, req, get_rlen)) {
            failed++;
            close(s);
            continue;
        }
        while(1) {
            if (timerexpired) break; 
            i = read(s, buf, 1500);
            //fprintf(stderr,"%d\n",i);
            if (i < 0) { 
                failed++;
                close(s);
                goto nexttry;
            } else {
                if (i==0) break;
                else bytes+=i;
            }
        }
        if (close(s)) {
            failed++;
            continue;
        }
        speed++;
    }
}

static int b_socket(const char *host, int port)
{
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;
    struct hostent *hp;
    
    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;

    inaddr = inet_addr(host);
    if (inaddr != INADDR_NONE) {
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    } else {
        hp = gethostbyname(host);
        if (hp == NULL) return -1;
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }

    ad.sin_port = htons(port);
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) return sock;
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0) {
        return -1;
    }

    return sock;
}