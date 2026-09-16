#include "graphics/guest_gpu/command_processor/pendingCompletions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using Libs::Graphics::PendingCompletions;

void Check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "PendingCompletionsTests: failed: %s\n", message);
    std::abort();
  }
}

constexpr uint64_t Label = 0x2000'0100;

// While a completion is pending, the value the device will write to that
// address is known, so a wait for it can be answered from submission order.
void TestRecordAndFind() {
  PendingCompletions pending;
  uint64_t value = 0;
  Check(!pending.Find(Label, 4, value), "an empty set matched an address");

  pending.Record(Label, 4, 0x11223344u, 7);
  Check(pending.Size() == 1 && !pending.Empty(), "Record did not store the completion");
  Check(pending.Find(Label, 4, value) && value == 0x11223344u,
        "Find did not return the recorded value");

  // The guest is describing other bytes: a 64-bit wait is not answered by a
  // 32-bit completion at the same address, or the reverse.
  Check(!pending.Find(Label, 8, value), "a width mismatch matched");
  Check(!pending.Find(Label + 4, 4, value), "a neighbouring address matched");

  // The last recorded write is what the device will leave behind.
  pending.Record(Label, 4, 0x55667788u, 9);
  Check(pending.Size() == 1 && pending.Find(Label, 4, value) && value == 0x55667788u,
        "a second completion for the same address did not replace the first");
}

// After the tick retires the value is on its way into guest memory through the
// ordinary publication. Keeping the entry would let a later wait match a
// completion that has already happened instead of one still ahead of it.
void TestPruneRetiredTicks() {
  PendingCompletions pending;
  pending.Record(Label, 4, 1, 10);
  pending.Record(Label + 0x100, 4, 2, 20);

  uint64_t value = 0;
  pending.Prune(9);
  Check(pending.Size() == 2, "pruning below every tick dropped a live completion");

  pending.Prune(10);
  Check(!pending.Find(Label, 4, value), "a retired completion survived pruning");
  Check(pending.Find(Label + 0x100, 4, value) && value == 2,
        "pruning dropped a completion the device had not reached");

  pending.Prune(20);
  Check(pending.Empty(), "pruning past every tick left entries behind");
}

// A direct command-stream write takes the address back: the stream is managing
// those bytes itself, so the queued completion no longer describes them.
void TestDropOnDirectWrite() {
  PendingCompletions pending;
  pending.Record(Label, 4, 1, 5);
  pending.Record(Label + 8, 8, 2, 5);

  uint64_t value = 0;
  pending.Drop(Label + 0x1000, 4);
  Check(pending.Size() == 2, "an unrelated write dropped completions");

  // Overlap at either end, and a range covering only part of an entry, all
  // count: the bytes are no longer the completion's to describe.
  pending.Drop(Label + 2, 1);
  Check(!pending.Find(Label, 4, value), "a write inside a completion did not drop it");
  Check(pending.Find(Label + 8, 8, value), "a write dropped a completion it did not touch");

  pending.Drop(Label + 15, 1);
  Check(!pending.Find(Label + 8, 8, value), "a write at the last byte did not drop it");

  pending.Record(Label, 4, 3, 6);
  pending.Drop(Label, 0);
  Check(pending.Size() == 1, "an empty range dropped a completion");
}

void TestDegenerateRecords() {
  PendingCompletions pending;
  uint64_t value = 0;
  pending.Record(0, 4, 1, 1);
  pending.Record(Label, 0, 1, 1);
  Check(pending.Empty(), "a null address or zero width was stored");

  pending.Record(Label, 4, 1, 1);
  pending.Clear();
  Check(pending.Empty() && !pending.Find(Label, 4, value), "Clear left entries behind");
}

} // namespace

int main() {
  TestRecordAndFind();
  TestPruneRetiredTicks();
  TestDropOnDirectWrite();
  TestDegenerateRecords();
  std::printf("PendingCompletionsTests: ok\n");
  return 0;
}
