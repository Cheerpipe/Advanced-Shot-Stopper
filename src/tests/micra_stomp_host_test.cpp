#include "machine/ShotStopperMicraStomp.h"
#include <cassert>
#include <string>
#include <vector>

using namespace shotstopper;
struct Received {
  std::vector<std::string> bodies;
  std::vector<uint32_t> attempts;
  static bool accept(void *context, const MicraStompFrame &frame) {
    auto &received = *static_cast<Received *>(context);
    assert(std::string(frame.command) == "MESSAGE");
    assert(std::string(frame.destination) == "/ws/sn/test/dashboard");
    assert(std::string(frame.subscription) == "id:1");
    received.bodies.emplace_back(frame.body, frame.bodyLength);
    received.attempts.push_back(frame.ingressAttempt);
    return true;
  }
};
static std::string frame(const std::string &body, bool length, bool crlf) {
  const char *end = crlf ? "\r\n" : "\n";
  std::string data = std::string("MESSAGE") + end +
      "destination:/ws/sn/test/dashboard" + end + "subscription:id\\c1" + end;
  if (length) data += "content-length:" + std::to_string(body.size()) + end;
  return data + end + body + std::string(1, '\0');
}
int main() {
  // Synthetic STOMP bytes, split at every boundary and one byte at a time.
  for (bool length : {false, true}) for (bool crlf : {false, true}) {
    const auto bytes = frame("{\"widgets\":[]}", length, crlf);
    for (size_t split = 0; split <= bytes.size(); ++split) {
      MicraStompDecoder decoder;
      Received received;
      assert(decoder.feed(bytes.data(), split, 1, Received::accept, &received));
      assert(decoder.feed(bytes.data() + split, bytes.size() - split, 2, Received::accept, &received));
      assert(received.bodies.size() == 1 && received.bodies[0] == "{\"widgets\":[]}");
      assert(!decoder.pendingSince());
    }
    MicraStompDecoder decoder;
    Received received;
    for (char byte : bytes) assert(decoder.feed(&byte, 1, 1, Received::accept, &received));
    assert(received.bodies.size() == 1);
    const auto batch = "\n\r\n" + bytes + bytes + "\n";
    assert(decoder.feed(batch.data(), batch.size(), 2, Received::accept, &received));
    assert(received.bodies.size() == 3);
  }
  MicraStompDecoder decoder;
  Received received;
  const auto oldFrame = frame("{}", true, false);
  assert(decoder.feed(oldFrame.data(), 5, 0, Received::accept, &received, 10));
  const auto joined = oldFrame.substr(5) + oldFrame + oldFrame;
  assert(decoder.feed(joined.data(), joined.size(), 1, Received::accept, &received, 11));
  assert((received.attempts == std::vector<uint32_t>{10, 11, 11}));
  const auto binary = frame(std::string("a\0b", 3), true, false);
  assert(decoder.feed(binary.data(), binary.size(), 1, Received::accept, &received));
  assert(received.bodies.back() == std::string("a\0b", 3));
  for (const auto &invalid : {
      std::string(1025, 'M'),
      frame(std::string(16385, 'x'), false, false),
      std::string("MESSAGE\ncontent-length:16385\n\n"),
      std::string("MESSAGE\ncontent-length:-1\n\n"),
      std::string("MESSAGE\ncontent-length:999999999999999999\n\n"),
      std::string("MESSAGE\ncontent-length:0\ncontent-length:0\n\n"),
      std::string("MESSAGE\nsubscription:a\\z\n\n")}) {
    decoder.reset();
    assert(!decoder.feed(invalid.data(), invalid.size(), 1, Received::accept, &received));
  }
  decoder.reset();
  assert(decoder.feed("MESSAGE\n", 8, 1, Received::accept, &received));
  assert(!decoder.incompleteExpired(10000));
  assert(decoder.incompleteExpired(10001));
  assert(!decoder.feed("\n", 1, 10001, Received::accept, &received));
  decoder.reset();
  assert(decoder.feed("MESSAGE\n", 8, UINT32_MAX - 99, Received::accept, &received));
  assert(decoder.incompleteExpired(10000));
  decoder.reset();
  const auto maximum = frame(std::string(16384, 'x'), true, false);
  assert(decoder.feed(maximum.data(), maximum.size(), 1, Received::accept, &received));
  assert(received.bodies.back().size() == 16384);
}
