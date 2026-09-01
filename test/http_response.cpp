#include <mist/http_parser.h>

#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
  std::string receiveAvailable(int fd) {
    const int oldFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK);
    std::string result;
    char buffer[4096];
    while (true) {
      const ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
      if (got > 0) {
        result.append(buffer, got);
        continue;
      }
      break;
    }
    fcntl(fd, F_SETFL, oldFlags);
    return result;
  }

  bool contains(const std::string & wire, const std::string & needle, const char *description) {
    if (wire.find(needle) != std::string::npos) { return true; }
    std::cerr << "missing " << description << ": " << needle << "\nresponse was:\n" << wire << std::endl;
    return false;
  }

  bool excludes(const std::string & wire, const std::string & needle, const char *description) {
    if (wire.find(needle) == std::string::npos) { return true; }
    std::cerr << "unexpected " << description << ": " << needle << "\nresponse was:\n" << wire << std::endl;
    return false;
  }

  bool buffered11Reuse() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser request;
    request.protocol = "HTTP/1.1";
    HTTP::Parser response;

    response.StartResponse(request, connection, true);
    response.Chunkify("first", connection);
    response.Chunkify("", connection);
    response.StartResponse(request, connection, true);
    response.Chunkify("second", connection);
    response.Chunkify("", connection);

    const std::string wire = receiveAvailable(sockets[1]);
    const bool ok = connection.connected() && contains(wire, "Content-Length: 5", "buffered length") &&
      excludes(wire, "Connection: close", "close header") &&
      contains(wire, "firstHTTP/1.1 200 OK", "second response on same connection") &&
      contains(wire, "second", "second response body");
    connection.close();
    close(sockets[1]);
    return ok;
  }

  bool chunked11Reuse() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser request;
    request.protocol = "HTTP/1.1";
    HTTP::Parser response;

    response.StartResponse(request, connection, false);
    response.Chunkify("first", connection);
    response.Chunkify("", connection);
    response.StartResponse(request, connection, false);
    response.Chunkify("second", connection);
    response.Chunkify("", connection);

    const std::string wire = receiveAvailable(sockets[1]);
    const bool ok = connection.connected() && contains(wire, "Transfer-Encoding: chunked", "chunked header") &&
      contains(wire, "0\r\n\r\nHTTP/1.1 200 OK", "second chunked response") &&
      excludes(wire, "Connection: close", "close header");
    connection.close();
    close(sockets[1]);
    return ok;
  }

  bool chunked11ReuseWithSharedParser() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser parser;

    std::string firstRequest = "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n";
    if (!parser.Read(firstRequest)) {
      connection.close();
      close(sockets[1]);
      return false;
    }
    parser.StartResponse(parser, connection, false);
    parser.Chunkify("first", connection);
    parser.Chunkify("", connection);

    std::string secondRequest = "GET /second HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const bool parsedSecond = parser.Read(secondRequest);
    const bool ok = parsedSecond && parser.getUrl() == "/second";
    if (!ok) { std::cerr << "shared parser did not accept the next request after a chunked response" << std::endl; }
    connection.close();
    close(sockets[1]);
    return ok;
  }

  bool closesAsRequested(const std::string & protocol, const std::string & requestConnection, bool buffered, const char *description) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser request;
    request.protocol = protocol;
    if (requestConnection.size()) { request.SetHeader("Connection", requestConnection); }
    HTTP::Parser response;
    response.StartResponse(request, connection, buffered);
    response.Chunkify("body", connection);
    response.Chunkify("", connection);

    const std::string wire = receiveAvailable(sockets[1]);
    const bool ok = !connection.connected() && contains(wire, "Connection: close", description);
    close(sockets[1]);
    return ok;
  }

  bool buffered10KeepAlive() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser request;
    request.protocol = "HTTP/1.0";
    request.SetHeader("Connection", "keep-alive");
    HTTP::Parser response;
    response.StartResponse(request, connection, true);
    response.Chunkify("body", connection);
    response.Chunkify("", connection);

    const std::string wire = receiveAvailable(sockets[1]);
    const bool ok = connection.connected() && contains(wire, "Connection: keep-alive", "HTTP/1.0 keep-alive") &&
      contains(wire, "Content-Length: 4", "HTTP/1.0 buffered length");
    connection.close();
    close(sockets[1]);
    return ok;
  }

  bool noContentIsSelfDelimited() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser request;
    request.protocol = "HTTP/1.1";
    request.method = "OPTIONS";
    HTTP::Parser response;
    response.StartResponse("204", "No Content", request, connection, false);

    const std::string wire = receiveAvailable(sockets[1]);
    const bool ok = connection.connected() && contains(wire, "HTTP/1.1 204 No Content", "204 status") &&
      excludes(wire, "Transfer-Encoding", "transfer encoding on 204") &&
      excludes(wire, "Content-Length", "content length on 204");
    connection.close();
    close(sockets[1]);
    return ok;
  }

  bool bufferedStatusIsPreserved() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser request;
    request.protocol = "HTTP/1.1";
    HTTP::Parser response;
    response.StartResponse("404", "Not Found", request, connection, true);
    response.Chunkify("missing", connection);
    response.Chunkify("", connection);
    const std::string wire = receiveAvailable(sockets[1]);
    const bool ok = contains(wire, "HTTP/1.1 404 Not Found", "buffered status") &&
      contains(wire, "Content-Length: 7", "buffered error length");
    connection.close();
    close(sockets[1]);
    return ok;
  }

  bool bodylessResponse(const std::string & method, const std::string & code, bool buffered) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { return false; }
    Socket::Connection connection(sockets[0]);
    HTTP::Parser request;
    request.protocol = "HTTP/1.1";
    request.method = method;
    HTTP::Parser response;
    response.StartResponse(code, "Bodyless", request, connection, buffered);
    response.Chunkify("must-not-appear", connection);
    response.Chunkify("", connection);
    const std::string wire = receiveAvailable(sockets[1]);
    const bool ok = connection.connected() && excludes(wire, "must-not-appear", "body on bodyless response") &&
      excludes(wire, "Transfer-Encoding", "unneeded transfer encoding");
    connection.close();
    close(sockets[1]);
    return ok;
  }

  // BuildResponse uses zero content-length as a signal that the content length is unknown and will follow using
  // Chunkify. Verify we don't actually transmit a Content-Length in this case.
  bool buildResponseNoZeroLength() {
    HTTP::Parser response;
    response.protocol = "HTTP/1.1";
    response.SetHeader("Content-Length", 0);
    return excludes(response.BuildResponse("200", "OK"), "Content-Length: 0", "zero content length");
  }
} // namespace

int main() {
  size_t testCount = 0;
  // TAP header line listing test count
  std::cout << "TAP version 14" << std::endl << "1..16" << std::endl;

#define CHECK_RESPONSE_TEST(expression)                                      \
  if (!(expression)) {                                                       \
    std::cout << "not ok " << ++testCount << " - " #expression << std::endl; \
  } else {                                                                   \
    std::cout << "ok " << ++testCount << " - " #expression << std::endl;     \
  }

  CHECK_RESPONSE_TEST(buffered11Reuse());
  CHECK_RESPONSE_TEST(chunked11Reuse());
  CHECK_RESPONSE_TEST(chunked11ReuseWithSharedParser());
  CHECK_RESPONSE_TEST(closesAsRequested("HTTP/1.1", "close", true, "HTTP/1.1 close header"));
  CHECK_RESPONSE_TEST(closesAsRequested("HTTP/1.1", "close", false, "HTTP/1.1 chunk request close header"));
  CHECK_RESPONSE_TEST(closesAsRequested("HTTP/1.0", "", true, "HTTP/1.0 close header"));
  CHECK_RESPONSE_TEST(buffered10KeepAlive());
  CHECK_RESPONSE_TEST(closesAsRequested("HTTP/1.1", "CLOSE", true, "case-insensitive close header"));
  CHECK_RESPONSE_TEST(noContentIsSelfDelimited());
  CHECK_RESPONSE_TEST(bufferedStatusIsPreserved());
  CHECK_RESPONSE_TEST(bodylessResponse("HEAD", "200", false));
  CHECK_RESPONSE_TEST(bodylessResponse("HEAD", "200", true));
  CHECK_RESPONSE_TEST(bodylessResponse("GET", "103", false));
  CHECK_RESPONSE_TEST(bodylessResponse("GET", "204", true));
  CHECK_RESPONSE_TEST(bodylessResponse("GET", "304", false));
  CHECK_RESPONSE_TEST(buildResponseNoZeroLength());
  return 0;
}
