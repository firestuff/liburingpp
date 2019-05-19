#include "uring.h"

#include <glog/logging.h>
#include <sys/socket.h>

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  uring::URing uring(1);

  int sv[2];
  PCHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

  std::string foo("foo\n");

  bool fired = false;
  uring.write(sv[0], foo.data(), foo.size(), [&](int32_t res) {
    fired = true;
    LOG(INFO) << "write() res=" << res;
    CHECK_EQ(res, 4);
  });
  uring.Submit();
  uring.Wait();
  CHECK(fired);

  char buf[10];
  fired = false;
  uring.read(sv[1], buf, 10, [&](int32_t res) {
    fired = true;
    LOG(INFO) << "read() res=" << res;
    CHECK_EQ(res, 4);
    CHECK_EQ(memcmp(buf, foo.data(), foo.size()), 0);
  });
  uring.Submit();
  uring.Wait();
  CHECK(fired);

  CHECK_EQ(uring.Try(), false);
}
