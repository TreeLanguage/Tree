#pragma once

#include <compare>
#include <ostream>

namespace tree {
struct Position {
  int line;
  int column;

  explicit constexpr Position(int new_line = 0, int new_column = 0) noexcept
      : line(new_line), column(new_column) {}

  constexpr bool operator==(const Position &) const noexcept = default;
  constexpr std::strong_ordering
  operator<=>(const Position &) const noexcept = default;
};

std::ostream &operator<<(std::ostream &os, const Position &pos);

struct Span {
  Position begin;
  Position end;

  explicit constexpr Span(Position new_begin = Position{},
                          Position new_end = Position{}) noexcept
      : begin(new_begin), end(new_end) {}

  constexpr Span(int start_line, int start_col, int end_line,
                 int end_col) noexcept
      : begin(start_line, start_col), end(end_line, end_col) {}

  constexpr bool operator==(const Span &) const noexcept = default;

  bool contains(Position pos) const noexcept;
  Span merge(const Span &other) const noexcept;
};

std::ostream &operator<<(std::ostream &os, const Span &span);
} // namespace tree
