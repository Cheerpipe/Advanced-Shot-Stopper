#pragma once

#include <type_traits>
#include <utility>

namespace shotstopper {

// Unique, non-allocating ownership for ESP-IDF/FreeRTOS C handles. Deleters
// must be stateless so the wrapper remains exactly one handle wide. Task
// handles deliberately do not use this class: their owner must perform the
// explicit stop/ack/join protocol before releasing task-owned resources.
template <typename Handle, typename Deleter>
class UniqueResource final : private Deleter {
 public:
  static_assert(std::is_empty<Deleter>::value,
                "resource deleters must not add storage");

  UniqueResource() = default;
  explicit UniqueResource(Handle handle) : handle_(handle) {}
  ~UniqueResource() { reset(); }

  UniqueResource(const UniqueResource &) = delete;
  UniqueResource &operator=(const UniqueResource &) = delete;

  UniqueResource(UniqueResource &&other) noexcept
      : handle_(other.release()) {}

  UniqueResource &operator=(UniqueResource &&other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }

  Handle get() const { return handle_; }
  explicit operator bool() const { return handle_ != Handle{}; }

  Handle release() {
    const Handle handle = handle_;
    handle_ = Handle{};
    return handle;
  }

  void reset(Handle replacement = Handle{}) {
    if (handle_ == replacement) return;
    const Handle previous = handle_;
    handle_ = replacement;
    if (previous != Handle{}) {
      (void)static_cast<Deleter &>(*this)(previous);
    }
  }

 private:
  Handle handle_{};
};

}  // namespace shotstopper
