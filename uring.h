#pragma once

#include <liburing.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace uring {

// Not thread safe. Instantiate one per thread instead.
class URing {
 public:
  URing(uint32_t num_entries);
  ~URing();

  // TODO: write/writev don't work for files because they always use offset 0
  void write(int fd, const void *buf, size_t count,
             const std::function<void(int32_t res)> &callback);
  void writev(int fd, const std::vector<iovec> &vecs,
              const std::function<void(int32_t res)> &callback);
  void pwritev(int fd, const std::vector<iovec> &vecs, off_t offset,
               const std::function<void(int32_t res)> &callback);

  // TODO: read/readv don't work for files because they always use offset 0
  void read(int fd, const void *buf, size_t count,
            const std::function<void(int32_t res)> &callback);
  void readv(int fd, const std::vector<iovec> &vecs,
             const std::function<void(int32_t res)> &callback);
  void preadv(int fd, const std::vector<iovec> &vecs, off_t offset,
              const std::function<void(int32_t res)> &callback);

  // Submit all operations queued since the last Submit(). They must all be in
  // a valid state when this is called.
  void Submit();

  // Wait for one operation to complete and synchronously call its callback.
  void Wait();

  // If an operation is complete, synchronously call its callback and return
  // true.
  bool Try();

 private:
  io_uring ring_;
  int ring_fd_;

  struct Entry {
    Entry *free_next;
    std::vector<iovec> vecs;
    std::function<void(int32_t)> callback;
  };

  std::vector<Entry> entries_;
  Entry *free_head_ = nullptr;

  Entry *GetEntry(const std::function<void(int32_t res)> &callback);
  void PutEntry(Entry *entry, int32_t res);

  io_uring_sqe *GetSQE();

  void ProcessCQE(io_uring_cqe *cqe);
};

}  // namespace uring
