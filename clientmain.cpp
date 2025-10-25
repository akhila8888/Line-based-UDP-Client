#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "protocol.h" // Provided in the repo
#include "calcLib.h"  // Provided support library

#define TIMEOUT_SEC 2
#define MAX_RETRIES 3

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: ./client <host:port>" << std::endl;
    return 1;
  }

  // ---------------------------
  // Parse host:port
  // ---------------------------
  std::string arg = argv[1];
  size_t pos = arg.find(':');
  if (pos == std::string::npos)
  {
    std::cerr << "Invalid input. Format: <host:port>" << std::endl;
    return 1;
  }

  std::string host = arg.substr(0, pos);
  std::string port = arg.substr(pos + 1);

  std::cout << "Host " << host << ", and port " << port << "." << std::endl;

  // ---------------------------
  // Resolve address (IPv4 or IPv6)
  // ---------------------------
  struct addrinfo hints{}, *res, *p;
  hints.ai_family = AF_UNSPEC;    // Allow both IPv4 and IPv6
  hints.ai_socktype = SOCK_DGRAM; // UDP

  int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
  if (status != 0)
  {
    std::cerr << "getaddrinfo: " << gai_strerror(status) << std::endl;
    return 1;
  }

  int sockfd = -1;
  struct addrinfo *dest = nullptr;
  for (p = res; p != nullptr; p = p->ai_next)
  {
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd == -1)
      continue;
    dest = p;
    break;
  }

  if (sockfd < 0 || !dest)
  {
    std::cerr << "Failed to create socket" << std::endl;
    freeaddrinfo(res);
    return 1;
  }

  // ---------------------------
  // Set 2-second receive timeout
  // ---------------------------
  struct timeval tv;
  tv.tv_sec = TIMEOUT_SEC;
  tv.tv_usec = 0;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

#ifdef DEBUG
  char hostip[INET6_ADDRSTRLEN];
  void *addrptr = nullptr;
  if (dest->ai_family == AF_INET)
    addrptr = &((struct sockaddr_in *)dest->ai_addr)->sin_addr;
  else
    addrptr = &((struct sockaddr_in6 *)dest->ai_addr)->sin6_addr;
  inet_ntop(dest->ai_family, addrptr, hostip, sizeof(hostip));
  std::cout << "Resolved " << host << " to " << hostip << std::endl;
#endif

  // ---------------------------
  // Build initial calcMessage
  // ---------------------------
  calcMessage msg{};
  msg.type = htons(22);
  msg.message = htons(0);
  msg.protocol = htons(17);
  msg.major_version = htons(1);
  msg.minor_version = htons(0);

  // ---------------------------
  // Send initial calcMessage (with retries)
  // ---------------------------
  int retries = 0;
  bool gotReply = false;

  while (retries < MAX_RETRIES && !gotReply)
  {
    ssize_t sent = sendto(sockfd, &msg, sizeof(msg), 0, dest->ai_addr, dest->ai_addrlen);
    if (sent < 0)
    {
      perror("sendto");
      freeaddrinfo(res);
      close(sockfd);
      return 1;
    }

#ifdef DEBUG
    std::cout << "Sent calcMessage (" << sent << " bytes)" << std::endl;
#endif

    calcProtocol proto{};
    ssize_t n = recvfrom(sockfd, &proto, sizeof(proto), 0, nullptr, nullptr);
    if (n < 0)
    {
      retries++;
#ifdef DEBUG
      std::cout << "Timeout, retry " << retries << "/" << MAX_RETRIES << std::endl;
#endif
    }
    else if ((size_t)n != sizeof(proto))
    {
      std::cerr << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << std::endl;
      freeaddrinfo(res);
      close(sockfd);
      return 1;
    }
    else
    {
      gotReply = true;
#ifdef DEBUG
      std::cout << "Received calcProtocol (" << n << " bytes)" << std::endl;
#endif

      // Convert from network to host byte order
      proto.type = ntohs(proto.type);
      proto.major_version = ntohs(proto.major_version);
      proto.minor_version = ntohs(proto.minor_version);
      proto.id = ntohl(proto.id);
      proto.arith = ntohl(proto.arith);

      proto.inValue1 = ntohl(proto.inValue1);
      proto.inValue2 = ntohl(proto.inValue2);
      proto.inResult = ntohl(proto.inResult);

      // ---------------------------
      // Interpret the assignment
      // ---------------------------
      double result = 0.0;
      std::string op;
      bool isFloat = false;

      if (proto.arith >= 1 && proto.arith <= 4)
      { // Integer operations
        switch (proto.arith)
        {
        case 1:
          op = "add";
          result = proto.inValue1 + proto.inValue2;
          break;
        case 2:
          op = "sub";
          result = proto.inValue1 - proto.inValue2;
          break;
        case 3:
          op = "mul";
          result = proto.inValue1 * proto.inValue2;
          break;
        case 4:
          op = "div";
          result = (proto.inValue2 != 0) ? (double)proto.inValue1 / proto.inValue2 : 0.0;
          break;
        }
        isFloat = false;
        proto.inResult = htonl((int32_t)result);
      }
      else
      { // Floating operations
        switch (proto.arith)
        {
        case 5:
          op = "fadd";
          result = proto.flValue1 + proto.flValue2;
          break;
        case 6:
          op = "fsub";
          result = proto.flValue1 - proto.flValue2;
          break;
        case 7:
          op = "fmul";
          result = proto.flValue1 * proto.flValue2;
          break;
        case 8:
          op = "fdiv";
          result = proto.flValue2 != 0 ? proto.flValue1 / proto.flValue2 : 0.0;
          break;
        }
        isFloat = true;
        proto.flResult = result;
      }

      std::cout << "ASSIGNMENT: " << op << " "
                << (isFloat ? proto.flValue1 : (double)proto.inValue1) << " "
                << (isFloat ? proto.flValue2 : (double)proto.inValue2) << std::endl;

#ifdef DEBUG
      std::cout << "Calculated the result to " << result << std::endl;
#endif

      // Convert back to network byte order for sending
      proto.type = htons(proto.type);
      proto.major_version = htons(proto.major_version);
      proto.minor_version = htons(proto.minor_version);
      proto.id = htonl(proto.id);
      proto.arith = htonl(proto.arith);

      // ---------------------------
      // Send result to server (with retries)
      // ---------------------------
      int resRetries = 0;
      bool resAck = false;
      calcMessage replyMsg{};

      while (resRetries < MAX_RETRIES && !resAck)
      {
        sendto(sockfd, &proto, sizeof(proto), 0, dest->ai_addr, dest->ai_addrlen);
#ifdef DEBUG
        std::cout << "Sent result packet" << std::endl;
#endif

        ssize_t rn = recvfrom(sockfd, &replyMsg, sizeof(replyMsg), 0, nullptr, nullptr);
        if (rn < 0)
        {
          resRetries++;
#ifdef DEBUG
          std::cout << "Timeout waiting for OK/NOT OK, retry "
                    << resRetries << "/" << MAX_RETRIES << std::endl;
#endif
        }
        else if ((size_t)rn != sizeof(replyMsg))
        {
          std::cerr << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << std::endl;
          freeaddrinfo(res);
          close(sockfd);
          return 1;
        }
        else
        {
          replyMsg.type = ntohs(replyMsg.type);
          replyMsg.message = ntohs(replyMsg.message);
          replyMsg.major_version = ntohs(replyMsg.major_version);
          replyMsg.minor_version = ntohs(replyMsg.minor_version);

          if (replyMsg.type == 2 && replyMsg.message == 2)
          {
            std::cout << "NOT OK (myresult=" << result << ")" << std::endl;
          }
          else
          {
            std::cout << "OK (myresult=" << result << ")" << std::endl;
          }
          resAck = true;
        }
      }

      if (!resAck)
      {
        std::cerr << "Server did not reply after 3 attempts." << std::endl;
      }
    }
  }

  if (!gotReply)
  {
    std::cerr << "Server did not reply after 3 attempts." << std::endl;
  }

  freeaddrinfo(res);
  close(sockfd);
  return 0;
}
