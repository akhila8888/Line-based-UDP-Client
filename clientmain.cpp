#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "protocol.h"

#ifdef DEBUG
#define DEBUG_PRINT(x) std::cout << x << std::endl
#else
#define DEBUG_PRINT(x)
#endif

#define MAX_BUF 1024

// Helper function for byte-order conversion
static inline void host_to_network(calcMessage &msg)
{
  msg.type = htons(msg.type);
  msg.message = htons(msg.message);
  msg.protocol = htons(msg.protocol);
  msg.major_version = htons(msg.major_version);
  msg.minor_version = htons(msg.minor_version);
}

static inline void network_to_host(calcMessage &msg)
{
  msg.type = ntohs(msg.type);
  msg.message = ntohs(msg.message);
  msg.protocol = ntohs(msg.protocol);
  msg.major_version = ntohs(msg.major_version);
  msg.minor_version = ntohs(msg.minor_version);
}

static inline void network_to_host(calcProtocol &msg)
{
  msg.type = ntohs(msg.type);
  msg.major_version = ntohs(msg.major_version);
  msg.minor_version = ntohs(msg.minor_version);
  msg.id = ntohl(msg.id);
  msg.arith = ntohl(msg.arith);
  msg.inValue1 = ntohl(msg.inValue1);
  msg.inValue2 = ntohl(msg.inValue2);
  msg.inResult = ntohl(msg.inResult);
  // doubles don’t need byte order conversion for this assignment
}

// Perform calculation from assignment
static void calculate_result(calcProtocol &cp)
{
  switch (cp.arith)
  {
  case 1:
    cp.inResult = cp.inValue1 + cp.inValue2;
    break; // ADD
  case 2:
    cp.inResult = cp.inValue1 - cp.inValue2;
    break; // SUB
  case 3:
    cp.inResult = cp.inValue1 * cp.inValue2;
    break; // MUL
  case 4:
    cp.inResult = cp.inValue1 / cp.inValue2;
    break; // DIV
  case 5:
    cp.flResult = cp.flValue1 + cp.flValue2;
    break;
  case 6:
    cp.flResult = cp.flValue1 - cp.flValue2;
    break;
  case 7:
    cp.flResult = cp.flValue1 * cp.flValue2;
    break;
  case 8:
    cp.flResult = cp.flValue1 / cp.flValue2;
    break;
  default:
    std::cerr << "Unknown operation\n";
    break;
  }
}

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " host:port\n";
    return EXIT_FAILURE;
  }

  std::string arg(argv[1]);
  auto pos = arg.find(':');
  if (pos == std::string::npos)
  {
    std::cerr << "Invalid format. Use host:port\n";
    return EXIT_FAILURE;
  }

  std::string host = arg.substr(0, pos);
  std::string port = arg.substr(pos + 1);

  std::cout << "Host " << host << ", and port " << port << "." << std::endl;

  struct addrinfo hints{}, *res, *rp;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;    // IPv4 or IPv6
  hints.ai_socktype = SOCK_DGRAM; // UDP

  int ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
  if (ret != 0)
  {
    std::cerr << "getaddrinfo: " << gai_strerror(ret) << std::endl;
    return EXIT_FAILURE;
  }

  int sockfd = -1;
  for (rp = res; rp != nullptr; rp = rp->ai_next)
  {
    sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sockfd == -1)
      continue;
    break;
  }

  if (rp == nullptr)
  {
    std::cerr << "Could not create socket" << std::endl;
    freeaddrinfo(res);
    return EXIT_FAILURE;
  }

  DEBUG_PRINT("Socket created successfully.");

  // Timeout (already passed your test but keeping it consistent)
  struct timeval tv{2, 0};
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Send initial calcMessage
  calcMessage cm{};
  cm.type = 22;
  cm.message = 0;
  cm.protocol = 17;
  cm.major_version = 1;
  cm.minor_version = 0;

  calcMessage cm_send = cm;
  host_to_network(cm_send);

  int attempts = 0;
  ssize_t n;
  struct sockaddr_storage src_addr{};
  socklen_t addrlen = sizeof(src_addr);
  char buffer[MAX_BUF];

  while (attempts < 3)
  {
    sendto(sockfd, &cm_send, sizeof(cm_send), 0, rp->ai_addr, rp->ai_addrlen);
    n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                 (struct sockaddr *)&src_addr, &addrlen);
    if (n < 0)
    {
      DEBUG_PRINT("Timeout waiting for response, retrying...");
      attempts++;
      continue;
    }
    break;
  }

  if (attempts == 3)
  {
    std::cerr << "Server did not reply after 3 attempts." << std::endl;
    freeaddrinfo(res);
    close(sockfd);
    return EXIT_FAILURE;
  }

  // Determine which message we received
  if (n == sizeof(calcMessage))
  {
    calcMessage resp{};
    memcpy(&resp, buffer, sizeof(calcMessage));
    network_to_host(resp);

    if (resp.message == 2)
    {
      std::cerr << "Server replied NOT OK (unsupported protocol)." << std::endl;
      freeaddrinfo(res);
      close(sockfd);
      return EXIT_FAILURE;
    }
    else
    {
      std::cerr << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << std::endl;
      freeaddrinfo(res);
      close(sockfd);
      return EXIT_FAILURE;
    }
  }
  else if (n != sizeof(calcProtocol))
  {
    std::cerr << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << std::endl;
    freeaddrinfo(res);
    close(sockfd);
    return EXIT_FAILURE;
  }

  // We received calcProtocol
  calcProtocol cp{};
  memcpy(&cp, buffer, sizeof(calcProtocol));
  network_to_host(cp);

  std::string op;
  switch (cp.arith)
  {
  case 1:
    op = "add";
    break;
  case 2:
    op = "sub";
    break;
  case 3:
    op = "mul";
    break;
  case 4:
    op = "div";
    break;
  case 5:
    op = "fadd";
    break;
  case 6:
    op = "fsub";
    break;
  case 7:
    op = "fmul";
    break;
  case 8:
    op = "fdiv";
    break;
  default:
    op = "unknown";
    break;
  }

  std::cout << "ASSIGNMENT: " << op << " ";
  if (cp.arith < 5)
    std::cout << cp.inValue1 << " " << cp.inValue2 << std::endl;
  else
    std::cout << cp.flValue1 << " " << cp.flValue2 << std::endl;

  calculate_result(cp);
  DEBUG_PRINT("Calculated result to " << (cp.arith < 5 ? cp.inResult : cp.flResult));

  // Convert back to network byte order for sending result
  cp.type = htons(cp.type);
  cp.major_version = htons(cp.major_version);
  cp.minor_version = htons(cp.minor_version);
  cp.id = htonl(cp.id);
  cp.arith = htonl(cp.arith);
  cp.inValue1 = htonl(cp.inValue1);
  cp.inValue2 = htonl(cp.inValue2);
  cp.inResult = htonl(cp.inResult);

  attempts = 0;
  while (attempts < 3)
  {
    sendto(sockfd, &cp, sizeof(cp), 0, rp->ai_addr, rp->ai_addrlen);
    n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                 (struct sockaddr *)&src_addr, &addrlen);
    if (n < 0)
    {
      DEBUG_PRINT("Timeout waiting for OK/NOT OK, retrying...");
      attempts++;
      continue;
    }
    break;
  }

  if (attempts == 3)
  {
    std::cerr << "Server did not reply after sending result." << std::endl;
    freeaddrinfo(res);
    close(sockfd);
    return EXIT_FAILURE;
  }

  if (n != sizeof(calcMessage))
  {
    std::cerr << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << std::endl;
    freeaddrinfo(res);
    close(sockfd);
    return EXIT_FAILURE;
  }

  calcMessage resultMsg{};
  memcpy(&resultMsg, buffer, sizeof(calcMessage));
  network_to_host(resultMsg);

  if (resultMsg.message == 1)
    std::cout << "OK (myresult=" << (cp.arith < 5 ? cp.inResult : cp.flResult) << ")" << std::endl;
  else
    std::cout << "NOT OK (myresult=" << (cp.arith < 5 ? cp.inResult : cp.flResult) << ")" << std::endl;

  freeaddrinfo(res);
  close(sockfd);
  return EXIT_SUCCESS;
}
