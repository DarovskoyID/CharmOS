#include "../../user/drivers/virtio_blk/request.hpp"
#include <assert.h>

int main() {
  using namespace abi::block;
  using abi::syscall::Status;
  Request r{};
  r.op = kOpRead;
  r.segment_count = 1;
  r.blocks = 2048;
  r.segments[0] = {0x8000000, 1024 * 1024, 0};
  auto validate = [&] { return virtio_blk::validate_request(r, 10000, false, true); };
  assert(validate() == Status::Ok);
  r.blocks = 2049; assert(validate() == Status::InvalidArgs);
  r.blocks = 2048;
  r.lba = 9999; assert(validate() == Status::OutOfRange);
  r.lba = UINT64_MAX; assert(validate() == Status::OutOfRange);
  r.lba = 0;
  r.segments[0].iova = UINT64_MAX & ~uint64_t{511};
  assert(validate() == Status::InvalidArgs);
  r.segments[0].iova = 0x8000001; assert(validate() == Status::InvalidArgs);
  r.segments[0].iova = 0x8000000;
  r.segments[0].bytes = 0; assert(validate() == Status::InvalidArgs);
  r.segments[0].bytes = 1024 * 1024 - 1; assert(validate() == Status::InvalidArgs);
  r.segments[0].bytes = 4096; assert(validate() == Status::InvalidArgs);
  r.blocks = 8; assert(validate() == Status::Ok); // v1.3 request is still accepted.
  r.segment_count = 7; assert(validate() == Status::InvalidArgs);
  r.segment_count = 0; assert(validate() == Status::InvalidArgs);
  r.segment_count = 1;
  r.op = kOpWrite;
  assert(virtio_blk::validate_request(r, 10000, true, true) == Status::AccessDenied);
  r.op = kOpFlush; assert(validate() == Status::InvalidArgs);
  r.segment_count = 0; r.blocks = 0;
  assert(validate() == Status::Ok);
  assert(virtio_blk::validate_request(r, 10000, false, false) == Status::NotSupported);
  r.op = 99; assert(validate() == Status::InvalidArgs);
}
