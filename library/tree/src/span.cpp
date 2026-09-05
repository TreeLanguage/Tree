#include "span.hpp"

namespace tree {
std::ostream &operator<<(std::ostream &os, const Position &pos) {
  return os << pos.line << ':' << pos.column;
}

bool Span::contains(Position pos) const noexcept {
  return !(pos < begin) && pos < end;
}

Span Span::merge(const Span &other) const noexcept {
  return Span(begin < other.begin ? begin : other.begin,
              end < other.end ? other.end : end);
}

std::ostream &operator<<(std::ostream &os, const Span &span) {
  return os << span.begin << '-' << span.end;
}
} // namespace tree