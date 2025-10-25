#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <calcLib.h>
#include "protocol.h"

#ifdef DEBUG
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

constexpr size_t MSG_SIZE = sizeof(calcMessage);
constexpr size_t PROTO_SIZE = sizeof(calcProtocol);
constexpr int TIMEOUT_SEC = 2;
constexpr int MAX_ATTEMPTS = 3;

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
    return b ? a / b : 0;
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
    return b ? a / b : 0.0;
  default:
    return 0.0;
  }
}

// Send data with timeout and retries
int send_with_timeout(int sockfd, const void *send_buf, size_t send_len,
                      void *recv_buf, size_t *recv_len,
                      const sockaddr *dest_addr, socklen_t dest_len)
{
  std::vector<char> tmp_buf(1024);

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
    timeval timeout{TIMEOUT_SEC, 0};

    int sel = select(sockfd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (sel < 0)
    {
      perror("select");
      return -1;
    }
    if (sel == 0)
      continue; // timeout, retry

    ssize_t received = recvfrom(sockfd, tmp_buf.data(), tmp_buf.size(), 0, nullptr, nullptr);
    if (received <= 0)
      continue;

    std::memcpy(recv_buf, tmp_buf.data(), received);
    *recv_len = static_cast<size_t>(received);
    return 0;
  }
  return -1; // no response after retries
}

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " <host:port>\n";
    return 1;
  }

  char *endpoint = argv[1];
  char *colon = strrchr(endpoint, ':');
  if (!colon)
  {
    std::cerr << "Invalid endpoint format. Use host:port\n";
    return 1;
  }

  *colon = '\0';
  const char *host = endpoint;
  const char *port_str = colon + 1;
  int port = std::atoi(port_str);

  std::cout << "Host " << host << ", and port " << port << ".\n";

  // Resolve server address
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *server_addr;
  if (int err = getaddrinfo(host, port_str, &hints, &server_addr); err != 0)
  {
    std::cerr << "getaddrinfo: " << gai_strerror(err) << "\n";
    return 1;
  }

  int sockfd = socket(server_addr->ai_family, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    perror("socket");
    freeaddrinfo(server_addr);
    return 1;
  }

  // Bind to any local address
  addrinfo local_hints{};
  local_hints.ai_family = AF_UNSPEC;
  local_hints.ai_socktype = SOCK_DGRAM;
  local_hints.ai_flags = AI_PASSIVE;

  addrinfo *local_addr;
  if (int err = getaddrinfo(nullptr, "0", &local_hints, &local_addr); err != 0)
  {
    std::cerr << "getaddrinfo local: " << gai_strerror(err) << "\n";
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }
  if (bind(sockfd, local_addr->ai_addr, local_addr->ai_addrlen) < 0)
  {
    perror("bind");
    freeaddrinfo(local_addr);
    freeaddrinfo(server_addr);
    close(sockfd);
    return 1;
  }
  freeaddrinfo(local_addr);

  // Initial calcMessage
  calcMessage request{};
  request.type = htons(22);
  request.message = htonl(0);
  request.protocol = htons(17);
  request.major_version = htons(1);
  request.minor_version = htons(0);

  char send_buf[MSG_SIZE];
  std::memcpy(send_buf, &request, MSG_SIZE);

  char recv_buf[1024];
  size_t recv_size{};
  if (send_with_timeout(sockfd, send_buf, MSG_SIZE, recv_buf, &recv_size, server_addr->ai_addr, server_addr->ai_addrlen) < 0)
  {
    std::cout << "the server did not reply\n";
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  if (recv_size != MSG_SIZE && recv_size != PROTO_SIZE)
  {
    std::cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL\n";
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  // Handle calcMessage from server (NOT OK)
  if (recv_size == MSG_SIZE)
  {
    calcMessage resp;
    std::memcpy(&resp, recv_buf, MSG_SIZE);
    resp.type = ntohs(resp.type);
    resp.message = ntohl(resp.message);
    resp.protocol = ntohs(resp.protocol);
    resp.major_version = ntohs(resp.major_version);
    resp.minor_version = ntohs(resp.minor_version);

    if (resp.protocol != 17 || resp.major_version != 1 || resp.minor_version != 0)
    {
      std::cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL\n";
      close(sockfd);
      freeaddrinfo(server_addr);
      return 1;
    }

    if (resp.type == 2 && resp.message == 2)
      std::cout << "the server sent a 'NOT OK' message\n";
    else
      std::cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL\n";
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  // Handle calcProtocol assignment
  calcProtocol task;
  std::memcpy(&task, recv_buf, PROTO_SIZE);
  task.type = ntohs(task.type);
  task.major_version = ntohs(task.major_version);
  task.minor_version = ntohs(task.minor_version);
  task.id = ntohl(task.id);
  task.arith = ntohl(task.arith);

  int32_t h_in1 = static_cast<int32_t>(ntohl(*reinterpret_cast<uint32_t *>(&task.inValue1)));
  int32_t h_in2 = static_cast<int32_t>(ntohl(*reinterpret_cast<uint32_t *>(&task.inValue2)));
  double h_fl1 = task.flValue1, h_fl2 = task.flValue2;

  if (task.type != 1 || task.major_version != 1 || task.minor_version != 0)
  {
    std::cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL\n";
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  bool is_float_op = task.arith >= 5 && task.arith <= 8;
  int32_t res_int = 0;
  double res_float = 0.0;
  if (is_float_op)
    res_float = compute_float(task.arith, h_fl1, h_fl2);
  else
    res_int = compute_int(task.arith, h_in1, h_in2);

  std::cout << "ASSIGNMENT: " << op_to_string(task.arith) << " ";
  if (is_float_op)
  {
    std::cout << h_fl1 << " " << h_fl2 << "\n";
    DEBUG_PRINT("Calculated the result to %8.8g\n", res_float);
  }
  else
  {
    std::cout << h_in1 << " " << h_in2 << "\n";
    DEBUG_PRINT("Calculated the result to %d\n", res_int);
  }

  // Send calcProtocol response
  calcProtocol response{};
  response.type = htons(2);
  response.major_version = htons(1);
  response.minor_version = htons(0);
  response.id = htonl(task.id);
  response.arith = htonl(task.arith);
  *reinterpret_cast<uint32_t *>(&response.inValue1) = htonl(static_cast<uint32_t>(h_in1));
  *reinterpret_cast<uint32_t *>(&response.inValue2) = htonl(static_cast<uint32_t>(h_in2));
  *reinterpret_cast<uint32_t *>(&response.inResult) = htonl(static_cast<uint32_t>(res_int));
  response.flValue1 = h_fl1;
  response.flValue2 = h_fl2;
  response.flResult = res_float;

  char response_buf[PROTO_SIZE];
  std::memcpy(response_buf, &response, PROTO_SIZE);

  size_t final_recv_size{};
  if (send_with_timeout(sockfd, response_buf, PROTO_SIZE, recv_buf, &final_recv_size, server_addr->ai_addr, server_addr->ai_addrlen) < 0)
  {
    std::cout << "the server did not reply\n";
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  if (final_recv_size != MSG_SIZE)
  {
    std::cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL\n";
    close(sockfd);
    freeaddrinfo(server_addr);
    return 1;
  }

  calcMessage final_resp;
  std::memcpy(&final_resp, recv_buf, MSG_SIZE);
  final_resp.type = ntohs(final_resp.type);
  final_resp.message = ntohl(final_resp.message);

  if (final_resp.type == 2 && final_resp.message == 1)
  { // MSG_OK
    std::cout << "OK (myresult=";
    if (is_float_op)
      std::cout << res_float;
    else
      std::cout << res_int;
    std::cout << ")\n";
  }
  else
  {
    std::cout << "the server sent a 'NOT OK' message\n";
  }

  close(sockfd);
  freeaddrinfo(server_addr);
  return 0;
}
