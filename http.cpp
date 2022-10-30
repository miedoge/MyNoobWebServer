/* main -> startup -> accept_request -> execute_cgi, */
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <string>

using namespace std;
#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: Amie's http/0.1.0\r\n"  //自定义个人服务器名称.

void *accept_req(void *arg);
void bad_req(int);
void error_msg(const char *msg);
int startup(uint16_t port);
void cat(int a, FILE *);
void cannot_execute(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
void unimplemented(int);

int main(int argc, char *argv[]) {
  int server_sock, client_sock;
  struct sockaddr_in client_addr;
  socklen_t clien_addr_size = sizeof(client_addr);
  pthread_t p1;
  server_sock = startup(htons(atoi(argv[1])));
  cout << "http server_sock is: " << server_sock << endl;
  cout << "port: " << argv[1] << endl;
  while (true) {
    client_sock =
        accept(server_sock, (struct sockaddr *)&client_addr, &clien_addr_size);
    cout << "new connection... ip:" << inet_ntoa(client_addr.sin_addr)
         << "port:" << ntohs(client_addr.sin_port) << endl;
    if (client_sock == -1) {
      error_msg("accept() error");
    }
    if (pthread_create(&p1, NULL, accept_req, (void *)&client_sock) != 0)
      perror("pthread_create");
  }
  close(server_sock);
  return 0;
}

int startup(uint16_t port) {
  int http_server = 0, option;
  struct sockaddr_in name;
  // set http_socket
  http_server = socket(PF_INET, SOCK_STREAM, 0);
  if (http_server == -1) error_msg("startup_socket() error");

  socklen_t optlen;
  optlen = sizeof(option);
  option = 1;
  setsockopt(http_server, SOL_SOCKET, SO_REUSEADDR, (void *)&option, optlen);

  memset(&name, 0, sizeof(name));
  name.sin_port = port;
  name.sin_addr.s_addr = htonl(INADDR_ANY);
  name.sin_family = AF_INET;

  if (bind(http_server, (struct sockaddr *)&name, sizeof(name)) == -1)
    error_msg("http_server bind() error");

  if (port == 0) {  //端口出现错误时 动态分配一个端口
    __socklen_t re_set_port = sizeof(name);
    if (getsockname(http_server, (struct sockaddr *)&name, &re_set_port) == -1)
      error_msg("getsockname() error");
    port = ntohs(name.sin_port);
  }

  if (listen(http_server, 5) == -1) error_msg("http_server listen() error");
  return (http_server);
}

void *accept_req(void *arg) {
  int client_sock = *(int *)arg;  //获取传递过来的客户端套接字.
  char buf[1024];
  char method[255];
  char url[255];
  char path[512];

  int numchars, cgi = 0;  //如果服务器确定这是一个CGI程序，则为真

  size_t i, j;
  struct stat st;
  char *query_str = nullptr;

  numchars = get_line(client_sock, buf, sizeof(buf));

  i = 0;
  j = 0;
  while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
    // get req type.
    method[i] = buf[j];
    i++;
    j++;
  }
  method[i] = '\0';
  // strcasecmp 忽略大小写比较字符串 get GET post POST
  if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
    unimplemented(client_sock);
    return NULL;
  }

  if (strcasecmp(method, "POST") == 0) cgi = 1;

  i = 0;
  while (ISspace(buf[j]) && (j < sizeof(buf))) j++;
  while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
    url[i] = buf[j];
    i++;
    j++;
  }
  url[i] = '\0';
  // post 👆

  // GET请求url可能会带有?,有查询参数
  if (strcasecmp(method, "GET") == 0) {
    query_str = url;
    while ((*query_str != '?') && (*query_str != '\0'))
      query_str++;  // 指针遍历
    /* 如果有?表明是动态请求, 开启cgi */
    if (*query_str == '?') {
      cgi = 1;
      *query_str = '\0';
      query_str++;
    }
  }
  sprintf(path, "httpdocs%s", url);  // path
  if (path[strlen(path) - 1] == '/') {
    strcat(path, "index.html");  //将两个char类型连接。
  }

  if (stat(path, &st) == -1) {
    while ((numchars > 0) && strcmp("\n", buf))
      numchars = get_line(client_sock, buf, sizeof(buf));
    not_found(client_sock);
  } else {
    if ((st.st_mode & S_IFMT) ==
        S_IFDIR)  // S_IFDIR代表目录
                  //如果请求参数为目录, 自动打开index.html
    {
      strcat(path, "/index.html");
    }

    // 文件可执行
    if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) ||
        (st.st_mode & S_IXOTH))
      cgi = 1;
    // S_IXUSR:文件所有者具可执行权限
    // S_IXGRP:用户组具可执行权限
    // S_IXOTH:其他用户具可读取权限

    if (!cgi)
      serve_file(client_sock, path);
    else
      execute_cgi(client_sock, path, method, query_str);
  }
  close(client_sock);
  return NULL;
}

void bad_req(int client) {
  char buf[1024];
  // send error_code 400
  sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "Content-type: text/html\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "<P>Your browser sent a bad request, ");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "such as a POST without a Content-Length.\r\n");
  send(client, buf, sizeof(buf), 0);
}

void cat(int client, FILE *resource) {
  char buf[1024];
  // 从res中读取内容传递到buf.
  fgets(buf, sizeof(buf), resource);
  while (!feof(resource))  //如果文件结束，则返回非0值，否则返回0
  {
    send(client, buf, strlen(buf), 0);
    fgets(buf, sizeof(buf), resource);
  }
}

void cannot_execute(int client) {
  char buf[1024];
  // send 500 服务器内部错误
  sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
  send(client, buf, strlen(buf), 0);
}

void error_msg(const char *msg) {
  perror(msg);  // str + error_reason
  exit(1);
}

// 执行cgi动态解析.
void execute_cgi(int client, const char *path, const char *method,
                 const char *query_str) {
  char buf[1024];
  int cgi_output[2];
  int cgi_input[2];

  pid_t pid;
  int status;
  int i;
  char c;
  int numchars = 1;
  int content_length = -1;

  //默认字符
  buf[0] = 'A';
  buf[1] = '\0';

  // get
  if (strcasecmp(method, "GET") == 0)
    while ((numchars > 0) && strcmp("\n", buf)) {
      numchars = get_line(client, buf, sizeof(buf));
    }
  else {
    numchars = get_line(client, buf, sizeof(buf));
    while ((numchars > 0) && strcmp("\n", buf)) {
      buf[15] = '\0';
      if (strcasecmp(buf, "Content-Length:") == 0)
        content_length = atoi(&(buf[16]));
      numchars = get_line(client, buf, sizeof(buf));
    }

    if (content_length == -1) {
      bad_req(client);
      return;
    }
  }
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  send(client, buf, strlen(buf), 0);
  if (pipe(cgi_output) < 0) {
    cannot_execute(client);
    return;
  }
  if (pipe(cgi_input) < 0) {
    cannot_execute(client);
    return;
  }
  if ((pid = fork()) < 0) {
    cannot_execute(client);
    return;
  }
  if (pid == 0)  // fork()sucess,往下是子进程开始执行CGI脚本.
  {
    char method_env[255];
    char query_env[255];
    char length_env[255];

    dup2(cgi_output[1], 1);
    dup2(cgi_input[0], 0);

    close(cgi_output[0]);  //关闭了cgi_output中的读通道
    close(cgi_input[1]);   //关闭了cgi_input中的写通道

    sprintf(method_env, "REQUEST_METHOD=%s", method);
    putenv(method_env);

    if (strcasecmp(method, "GET") == 0) {  //存储QUERY_STRING,即get?传过来的参数
      sprintf(query_env, "QUERY_STRING=%s", query_str);
      putenv(query_env);
    } else {
      // POST
      //存储CONTENT_LENGTH
      sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
      putenv(length_env);
    }
    execl(path, path, NULL);  //执行CGI脚本
    exit(0);
  } else {
    close(cgi_output[1]);
    close(cgi_input[0]);

    if (strcasecmp(method, "POST") == 0)
      for (i = 0; i < content_length; i++) {
        recv(client, &c, 1, 0);
        write(cgi_input[1], &c, 1);
      }

    // read cgi back data
    while (read(cgi_output[0], &c, 1) > 0) {
      send(client, &c, 1, 0);
    }
    // 运行结束关闭文件描述符
    close(cgi_output[0]);
    close(cgi_input[1]);

    waitpid(pid, &status, 0);
  }
}

// 解析一行http报文.
int get_line(int sock, char *buf, int size) {
  int i = 0, n;
  char c = '\0';

  while ((i < size - 1) && (c != '\n')) {
    n = recv(sock, &c, 1, 0);

    if (n > 0) {
      if (c == '\r') {
        n = recv(sock, &c, 1, MSG_PEEK);
        if ((n > 0) && (c == '\n'))
          recv(sock, &c, 1, 0);
        else
          c = '\n';
      }
      buf[i] = c;
      i++;
    } else
      c = '\n';  // quit to while
  }
  buf[i] = '\0';
  return i;
}

// 404 bad request
void not_found(int client) {
  char buf[1024];
  sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "your request because the resource specified\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "is unavailable or nonexistent.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

//  如果不是CGI文件，也就是静态文件，直接读取文件返回给请求的http客户端即可
void serve_file(int client, const char *filename) {
  FILE *resource = nullptr;
  int numchars = 1;
  char buf[1024];
  buf[0] = 'A';
  buf[1] = '\0';
  while ((numchars > 0) && strcmp("\n", buf)) {
    numchars = get_line(client, buf, sizeof(buf));
  }
  // 打开文件
  resource = fopen(filename, "r");
  if (resource == NULL) {
    // 404
    not_found(client);
  } else {
    headers(client, filename);
    cat(client, resource);
  }
  fclose(resource);
}

// 未实现的
void unimplemented(int client) {
  char buf[1024];
  // 发送501说明相应方法没有实现
  sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</TITLE></HEAD>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

void headers(int client, const char *filename) {
  char buf[1024];

  (void)filename; /* could use filename to determine file type */
                  //发送HTTP头
  strcpy(buf, "HTTP/1.0 200 OK\r\n");
  send(client, buf, strlen(buf), 0);
  strcpy(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  strcpy(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
}
