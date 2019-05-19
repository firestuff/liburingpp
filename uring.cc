#include "uring.h"

#include <glog/logging.h>

namespace uring {

URing::URing(uint32_t num_entries)
    : ring_fd_(io_uring_queue_init(num_entries, &ring_, 0)),
      entries_(num_entries * 3),
      free_head_(&entries_.at(0)) {
  PCHECK(ring_fd_ >= 0) << "io_uring_queue_init()";

  // Chain the entries together into a list that we'll use as a free list for
  // future allocation.
  Entry *prev = nullptr;
  for (auto &entry : entries_) {
    entry.free_next = prev;
    prev = &entry;
  }
}

URing::~URing() { io_uring_queue_exit(&ring_); }

void URing::Submit() { io_uring_submit(&ring_); }

void URing::Wait() {
  io_uring_cqe *cqe;
  PCHECK(io_uring_wait_cqe(&ring_, &cqe) == 0);
  ProcessCQE(cqe);
}

bool URing::Try() {
  io_uring_cqe *cqe = nullptr;
  PCHECK(io_uring_peek_cqe(&ring_, &cqe) == 0);
  if (cqe) {
    ProcessCQE(cqe);
    return true;
  }
  return false;
}

void URing::nop(const std::function<void(int32_t res)> &callback) {
  auto *entry = GetEntry(callback);
  auto *sqe = GetSQE();
  io_uring_prep_nop(sqe);
  io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(entry));
}

void URing::write(int fd, const void *buf, size_t count,
                  const std::function<void(int32_t res)> &callback) {
  std::vector<iovec> vecs{
      {
          .iov_base = const_cast<void *>(buf),
          .iov_len = count,
      },
  };
  writev(fd, vecs, callback);
}

void URing::writev(int fd, const std::vector<iovec> &vecs,
                   const std::function<void(int32_t res)> &callback) {
  pwritev(fd, vecs, 0, callback);
}

void URing::pwritev(int fd, const std::vector<iovec> &vecs, off_t offset,
                    const std::function<void(int32_t res)> &callback) {
  auto *entry = GetEntry(callback);
  entry->vecs = vecs;

  auto *sqe = GetSQE();
  io_uring_prep_writev(sqe, fd, entry->vecs.data(), entry->vecs.size(), offset);
  io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(entry));
}

void URing::read(int fd, const void *buf, size_t count,
                 const std::function<void(int32_t res)> &callback) {
  std::vector<iovec> vecs{
      {
          .iov_base = const_cast<void *>(buf),
          .iov_len = count,
      },
  };
  readv(fd, vecs, callback);
}

void URing::readv(int fd, const std::vector<iovec> &vecs,
                  const std::function<void(int32_t res)> &callback) {
  preadv(fd, vecs, 0, callback);
}

void URing::preadv(int fd, const std::vector<iovec> &vecs, off_t offset,
                   const std::function<void(int32_t res)> &callback) {
  auto *entry = GetEntry(callback);
  entry->vecs = vecs;

  auto *sqe = GetSQE();
  io_uring_prep_readv(sqe, fd, entry->vecs.data(), entry->vecs.size(), offset);
  io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(entry));
}

URing::Entry *URing::GetEntry(
    const std::function<void(int32_t res)> &callback) {
  CHECK(free_head_);
  auto *entry = free_head_;
  free_head_ = entry->free_next;
  entry->callback = callback;
  return entry;
}

void URing::PutEntry(Entry *entry, int32_t res) {
  entry->callback(res);
  entry->free_next = free_head_;
  free_head_ = entry;
}

io_uring_sqe *URing::GetSQE() {
  auto *sqe = io_uring_get_sqe(&ring_);
  // TODO: automatically Submit() on full submit queue? spin? something else?
  CHECK(sqe);
  return sqe;
}

void URing::ProcessCQE(io_uring_cqe *cqe) {
  Entry *entry = reinterpret_cast<Entry *>(io_uring_cqe_get_data(cqe));
  PutEntry(entry, cqe->res);
  io_uring_cqe_seen(&ring_, cqe);
}

}  // namespace uring
