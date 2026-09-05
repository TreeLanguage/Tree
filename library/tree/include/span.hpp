#pragma once

#include <compare>
#include <ostream>

namespace tree {

struct Position {
  int line;
  int column;

  explicit constexpr Position(int line = 0, int column = 0) noexcept
      : line(line), column(column) {}

  constexpr bool operator==(const Position &) const noexcept = default;
  constexpr std::strong_ordering
  operator<=>(const Position &) const noexcept = default;
};

std::ostream &operator<<(std::ostream &os, const Position &pos);

struct Span {
  Position begin;
  Position end;

  explicit constexpr Span(Position begin = Position{},
                          Position end = Position{}) noexcept
      : begin(begin), end(end) {}

  constexpr Span(int start_line, int start_col, int end_line,
                 int end_col) noexcept
      : begin(start_line, start_col), end(end_line, end_col) {}

  constexpr bool operator==(const Span &) const noexcept = default;

  bool contains(Position pos) const noexcept;
  Span merge(const Span &other) const noexcept;
};

std::ostream &operator<<(std::ostream &os, const Span &span);

} // namespace tree