#pragma once

#include <cstdio>
#include <cstring>

namespace shotstopper {

inline void formatSafeHttpEndpoint(const char *url, char *output,
                                  size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  const char *scheme = url == nullptr ? nullptr : strstr(url, "://");
  if (scheme == nullptr) {
    snprintf(output, capacity, "unknown");
    return;
  }
  const char *authority = scheme + 3;
  const char *authorityEnd = strpbrk(authority, "/?#");
  if (authorityEnd == nullptr) authorityEnd = url + strlen(url);
  const char *credentials = static_cast<const char *>(
      memchr(authority, '@', static_cast<size_t>(authorityEnd - authority)));
  if (credentials != nullptr) authority = credentials + 1;
  const char *path = *authorityEnd == '/' ? authorityEnd : "";
  const char *end = strpbrk(path, "?#");
  if (end == nullptr) end = path + strlen(path);
  snprintf(output, capacity, "%.*s://%.*s%.*s",
           static_cast<int>(scheme - url), url,
           static_cast<int>(authorityEnd - authority), authority,
           static_cast<int>(end - path), path);
}

}  // namespace shotstopper
