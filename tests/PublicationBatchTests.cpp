#include "graphics/host_gpu/renderer/publicationBatch.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using Libs::Graphics::PublicationBatch;

void Check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "PublicationBatchTests: failed: %s\n", message);
    std::abort();
  }
}

// A plain end-of-pipe label write is observed by polling. The poller cannot get
// ahead of the GPU, so the completion only has to reach the device before the
// command processor yields - it does not need a submission of its own.
void TestLabelWritesBatchUpToTheLimit() {
  PublicationBatch batch(4);
  Check(batch.Limit() == 4, "constructor did not take the limit");
  Check(batch.Pending() == 0 && !batch.Prompt(), "batch did not start empty");

  for (uint32_t i = 1; i < 4; i++) {
    Check(!batch.Record(), "a label write below the limit forced a submission");
    Check(batch.Pending() == i, "pending count did not follow the recordings");
  }
  Check(batch.Record(), "reaching the limit did not force a submission");
}

// Submitting starts a new recording: nothing is pending against it, and the
// prompt requirement belonged to the recording that was just submitted.
void TestResetStartsANewRecording() {
  PublicationBatch batch(2);
  Check(!batch.Record(), "first label write forced a submission");
  batch.RequirePrompt();
  Check(batch.Record(), "a prompt recording batched a completion");

  batch.Reset();
  Check(batch.Pending() == 0 && !batch.Prompt(), "reset did not clear the batch");
  Check(!batch.Record(), "reset did not clear the prompt requirement");
}

// An interrupt or a flip can already have a guest thread parked on it, and
// nothing else will move the recording along. Those must never be batched -
// including every completion recorded after them in the same recording, which
// would otherwise be submitted before them anyway.
void TestPromptCompletionsAreNeverBatched() {
  PublicationBatch batch(64);
  batch.RequirePrompt();
  Check(batch.Prompt(), "RequirePrompt did not mark the recording");
  Check(batch.Record(), "a prompt completion was batched");
  Check(batch.Record(), "a prompt recording batched a later completion");

  PublicationBatch late(64);
  Check(!late.Record(), "a label write below the limit forced a submission");
  late.RequirePrompt();
  Check(late.Record(), "a prompt requirement raised mid-recording was ignored");
}

// A limit of zero would defer forever and a guest poll would never observe the
// completion; a limit of one restores submit-per-completion.
void TestDegenerateLimits() {
  PublicationBatch zero(0);
  Check(zero.Limit() == 1, "a zero limit was not clamped to submit-per-completion");
  Check(zero.Record(), "a zero limit deferred a completion");

  PublicationBatch one(1);
  Check(one.Record(), "a limit of one deferred a completion");

  PublicationBatch defaulted;
  Check(defaulted.Limit() == PublicationBatch::DefaultLimit,
        "the default constructor did not use the default limit");
  defaulted.SetLimit(0);
  Check(defaulted.Limit() == 1, "SetLimit did not clamp a zero limit");
}

} // namespace

int main() {
  TestLabelWritesBatchUpToTheLimit();
  TestResetStartsANewRecording();
  TestPromptCompletionsAreNeverBatched();
  TestDegenerateLimits();
  std::printf("PublicationBatchTests: ok\n");
  return 0;
}
