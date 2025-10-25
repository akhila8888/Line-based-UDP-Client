#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <string>
#include "protocol.h"
#include <endian.h>

#ifdef DEBUG
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

#define MSG_SIZE sizeof(struct calcMessage)
#define PROTO_SIZE sizeof(struct calcProtocol)
#define TIMEOUT_SEC 2
#define MAX_ATTEMPTS 3
#define PROTOCOL_ID 17
#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define CLIENT_MSG_TYPE 22
#define SERVER_MSG_TYPE 2
#define SERVER_PROTO_TYPE 1
#define CLIENT_PROTO_TYPE 2
#define MSG_NA 0
#define MSG_OK 1
#define MSG_NOK 2

std::string op_to_string(uint32_t op)
{
  switch (op)
  {
  case 1:
    return "add";
  case 2:
    return "sub";
  case 3:
    return "mul";
  case 4:
    return "div";
  case 5:
    return "fadd";
  case 6:
    return "fsub";
  case 7:
    return "fmul";
  case 8:
    return "fdiv";
  default:
    return "unknown";
  }
}

int32_t compute_int(uint32_t op, int32_t a, int32_t b)
{
  switch (op)
  {
  case 1:
    return a + b;
  case 2:
    return a - b;
  case 3:
    return a * b;
  case 4:
    return (b != 0 ? a / b : 0);
  default:
    return 0;
  }
}

double compute_float(uint32_t op, double a, double b)
{
  switch (op)
  {
  case 5:
    return a + b;
  case 6:
    return a - b;
  case 7:
    return a * b;
  case 8:
    return (b != 0.0 ? a / b : 0.0);
  default:
    return 0.0;
  }
}

int send_with_timeout(int sockfd, const void *send_buf, size_t send_len,
                      void *recv_buf, size_t *recv_len,
                      const struct sockaddr *dest_addr, socklen_t dest_len)
{
  char recv_buffer[1024];
  for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
  {
    if (sendto(sockfd, send_buf, send_len, 0, dest_addr, dest_len) < 0)
    {
      perror("sendto");
      return -1;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    struct timeval timeout = {TIMEOUT_SEC, 0};

    int sel = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
    if (sel < 0)
    {
      perror("select");
      return -1;
    }
    else if (sel == 0)
    {
      continue; // timeout, retry
    }

    ssize_t received = recvfrom(sockfd, recv_buffer, sizeof(recv_buffer), 0, NULL, NULL);
    if (received <= 0)
      continue;

    memcpy(recv_buf, recv_buffer, received);
    *recv_len = (size_t)received;
    return 0;
  }
  return -1;
}

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <host:port>\n", argv[0]);
    return 1;
  }

  char *endpoint = argv[1];
  char *colon = strrchr(endpoint, ':');
  if (!colon)
  {
    fprintf(stderr, "Invalid endpoint format. Use host:port\n");
    return 1;
  }
  *colon = '\0';
  char *host = endpoint;
  char *port_str = colon + 1;
  int port = atoi(port_str);
  printf("Host %s, and port %d.\n", host, port);

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  struct addrinfo *server_addr;
  int err = getaddrinfo(host, port_str, &hints, &server_addr);
  if (err != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
    return 1;
  }

  int sockfd = socket(server_addr->ai_family, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    perror("socket");
    freeaddrinfo(server_addr);
    return 1;
  }

  // Prepare initial calcMessage
  struct calcMessage request;
  request.type = htons(CLIENT_MSG_TYPE);
  request.message = htonl(MSG_NA);
  request.protocol = htons(PROTOCOL_ID);
  request.major_version = htons(MAJOR_VERSION);
  request.minor_version = htons(MINOR_VERSION);

  char send_buf[MSG_SIZE];
  memcpy(send_buf, &request, MSG_SIZE);
  char recv_buf[1024];
  size_t recv_size;

  if (send_with_timeout(sockfd, send_buf, MSG_SIZE, recv_buf, &recv_size,
                        server_addr->ai_addr, server_addr->ai_addrlen) < 0)
  {
    printf("the server did not reply\n");
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  if (recv_size != MSG_SIZE && recv_size != PROTO_SIZE)
  {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  // Server sent calcMessage (NOT OK)
  if (recv_size == MSG_SIZE)
  {
    struct calcMessage resp;
    memcpy(&resp, recv_buf, MSG_SIZE);
    resp.type = ntohs(resp.type);
    resp.message = ntohl(resp.message);
    resp.protocol = ntohs(resp.protocol);
    resp.major_version = ntohs(resp.major_version);
    resp.minor_version = ntohs(resp.minor_version);

    if (resp.type == SERVER_MSG_TYPE && resp.message == MSG_NOK)
    {
      printf("the server sent a 'NOT OK' message\n");
      close(sockfd);
      freeaddrinfo(server_addr);
      return 1;
    }
    else
    {
      printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
      close(sockfd);
      freeaddrinfo(server_addr);
      return 1;
    }
  }

  // Server sent calcProtocol
  struct calcProtocol task;
  memcpy(&task, recv_buf, PROTO_SIZE);
  task.type = ntohs(task.type);
  task.major_version = ntohs(task.major_version);
  task.minor_version = ntohs(task.minor_version);
  task.id = ntohl(task.id);
  task.arith = ntohl(task.arith);

  int32_t h_in1 = (int32_t)ntohl(*(uint32_t *)&task.inValue1);
  int32_t h_in2 = (int32_t)ntohl(*(uint32_t *)&task.inValue2);
  double h_fl1 = task.flValue1;
  double h_fl2 = task.flValue2;

  if (task.type != SERVER_PROTO_TYPE ||
      task.major_version != MAJOR_VERSION ||
      task.minor_version != MINOR_VERSION)
  {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  bool is_float_op = (task.arith >= 5 && task.arith <= 8);
  int32_t res_int = 0;
  double res_float = 0.0;

  if (is_float_op)
    res_float = compute_float(task.arith, h_fl1, h_fl2);
  else
    res_int = compute_int(task.arith, h_in1, h_in2);

  std::string op_str = op_to_string(task.arith);
  printf("ASSIGNMENT: %s ", op_str.c_str());
  if (is_float_op)
    printf("%8.8g %8.8g\n", h_fl1, h_fl2);
  else
    printf("%d %d\n", h_in1, h_in2);

  DEBUG_PRINT("Calculated the result to %s\n", is_float_op ? std::to_string(res_float).c_str() : std::to_string(res_int).c_str());

  // Send response
  struct calcProtocol response;
  response.type = htons(CLIENT_PROTO_TYPE);
  response.major_version = htons(MAJOR_VERSION);
  response.minor_version = htons(MINOR_VERSION);
  response.id = htonl(task.id);
  response.arith = htonl(task.arith);
  *(uint32_t *)&response.inValue1 = htonl((uint32_t)h_in1);
  *(uint32_t *)&response.inValue2 = htonl((uint32_t)h_in2);
  *(uint32_t *)&response.inResult = htonl((uint32_t)res_int);
  response.flValue1 = h_fl1;
  response.flValue2 = h_fl2;
  response.flResult = res_float;

  char response_buf[PROTO_SIZE];
  memcpy(response_buf, &response, PROTO_SIZE);

  size_t final_recv_size;
  if (send_with_timeout(sockfd, response_buf, PROTO_SIZE, recv_buf, &final_recv_size,
                        server_addr->ai_addr, server_addr->ai_addrlen) < 0)
  {
    printf("the server did not reply\n");
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  if (final_recv_size != MSG_SIZE)
  {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  struct calcMessage final_resp;
  memcpy(&final_resp, recv_buf, MSG_SIZE);
  final_resp.type = ntohs(final_resp.type);
  final_resp.message = ntohl(final_resp.message);
  final_resp.protocol = ntohs(final_resp.protocol);
  final_resp.major_version = ntohs(final_resp.major_version);
  final_resp.minor_version = ntohs(final_resp.minor_version);

  if (final_resp.protocol != PROTOCOL_ID ||
      final_resp.major_version != MAJOR_VERSION ||
      final_resp.minor_version != MINOR_VERSION)
  {
    printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  if (final_resp.type == SERVER_MSG_TYPE && final_resp.message == MSG_OK)
  {
    printf("OK (myresult=");
    if (is_float_op)
      printf("%8.8g", res_float);
    else
      printf("%d", res_int);
    printf(")\n");
  }
  else
  {
    printf("the server sent a 'NOT OK' message\n");
  }

  close(sockfd);
  freeaddrinfo(server_addr);
  return 0;
}
