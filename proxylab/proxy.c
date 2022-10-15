#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_hdr = "Proxy-Connection: close\r\n";

void doit(int connfd);
void parse_uri(const char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *args);

int readcount = 0;
sem_t mutex, w;

typedef struct {
    int flag;
    char *uri;
    char *msg;
    int size;
    int cnt;
}Cache, *pCache;

pCache cache;

void init_cache();
int reader(int connfd, char *uri);
void writer(char *buf, char *uri, int len);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    signal(SIGPIPE, SIG_IGN);

    init_cache();

    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        fprintf(stdout, "Accepted connection from (%s, %s)\n", hostname, port);

        pthread_create(&tid, NULL, thread, (void *)connfd);
    }

    return 0;
}

void *thread(void *args) {
    pthread_detach(pthread_self());
    int connfd = args;
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int connfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t connrio;

    Rio_readinitb(&connrio, connfd);

    Rio_readlineb(&connrio, buf, MAXLINE);
    printf("%s", buf);

    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET") != 0) {
        clienterror(connfd, method, "501", "Not IMplemented", "Proxy does not implement this method");
        return;
    }

    if (reader(connfd, uri) == 1) {
        return;
    }

    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    
    parse_uri(uri, hostname, port, path);

    int serverfd;
    rio_t serverrio;

    if ((serverfd = Open_clientfd(hostname, port)) < 0) {
        fprintf(stdout, "Connect failed\n");
        return;
    }

    Rio_readinitb(&serverrio, serverfd);

    char req[MAXLINE];

    sprintf(req, "GET %s HTTP/1.0\r\n", path);
    while (Rio_readlineb(&connrio, buf, MAXLINE) != 0) {
        if (!strcmp(buf, "\r\n")) {
            break;
        }
        if (strstr(buf, "Host:") != NULL) {
            continue;
        }
        if (strstr(buf, "User-Agent:") != NULL) {
            continue;
        }
        if (strstr(buf, "Connection:") != NULL) {
            continue;
        }
        if (strstr(buf, "Proxy-Connection:") != NULL) {
            continue;
        }

        printf("%s", buf);

        sprintf(req, "%s%s", req, buf);
    }

    sprintf(req, "%sHost: %s:%s\r\n", req, hostname, port);
    sprintf(req, "%s%s%s%s", req, user_agent_hdr, conn_hdr, proxy_hdr);
    sprintf(req, "%s\r\n", req);

    printf("%s", req);

    Rio_writen(serverfd, req, strlen(req));

    int len;

    char *msg = (char *) malloc(MAX_OBJECT_SIZE * sizeof(char));
    int msglen = 0;

    while ((len = Rio_readlineb(&serverrio, buf, MAXLINE)) != 0) {
        Rio_writen(connfd, buf, len);
        if (msglen + len < MAX_OBJECT_SIZE) {
            strncpy(msg + msglen, buf, len);
        }
        msglen += len;
    }

    if (msglen < MAX_OBJECT_SIZE) {
        writer(msg, uri, msglen);
    }

    Close(serverfd);
    return;
}

void parse_uri(const char *uri, char *hostname, char *port, char *path) {
    char buf[MAXLINE];
    strcpy(buf, uri);

    char *pos1 = strstr(buf, "//");
    if (pos1 != NULL) {
        pos1 = pos1 + 2;
    } else {
        pos1 = buf;
    }

    char *pos2 = strchr(pos1, ':');
    char *pos3 = strchr(pos1, '/');

    if (pos3 != NULL) {
        sscanf(pos3, "%s", path);
        *pos3 = '\0';
    } else {
        strcpy(path, "");
    }

    if (pos2 != NULL) {
        *pos2 = ' ';
        sscanf(pos1, "%s %s", hostname, port);
    } else {
        sscanf(pos1, "%s", hostname);
        strcpy(port, "80");
    }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)  {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void init_cache() {
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
    cache = (pCache) malloc(10 * sizeof(Cache));

    for (int i = 0; i < 10; ++i) {
        cache[i].flag = cache[i].size = 0;
        cache[i].msg = (char *) malloc(MAX_OBJECT_SIZE * sizeof(char));
        cache[i].uri = (char *) malloc(256 * sizeof(char));
    }
}

int reader(int connfd, char *uri) {
    int ret = 0;

    P(&mutex);
    readcount++;
    if (readcount == 1) {
        P(&w);
    }
    V(&mutex);

    for (int i = 0; i < 10; ++i) {
        if (cache[i].flag == 1 && !strcmp(uri, cache[i].uri)) {
            Rio_writen(connfd, cache[i].msg, cache[i].size);
            cache[i].cnt = 0;
            ret = 1;
            break;
        }
    }

    for (int i = 0; i < 10; ++i) {
        cache[i].cnt++;
    }

    P(&mutex);
    readcount--;
    if (readcount == 0) {
        V(&w);
    }
    V(&mutex);

    return ret;
}

void writer(char *buf, char *uri, int len) {

    int index = 0;
    int maxcnt = 0;

    P(&w);

    for (int i = 0; i < 10; ++i) {
        if (cache[i].flag == 0) {
            index = i;
            break;
        }

        if (cache[i].cnt > maxcnt) {
            index = i;
            maxcnt = cache[i].cnt;
        }
    }

    cache[index].flag = 1;
    cache[index].size = len;
    cache[index].cnt = 0;
    strcpy(cache[index].msg, buf);
    strcpy(cache[index].uri, uri);

    V(&w);
}