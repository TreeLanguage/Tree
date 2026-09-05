#include "diagnostic.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int run_file(const std::string &filename) {
  std::ifstream file(filename);

  if (!file) {
    std::cerr << "error: could not open '" << filename << "'\n";
    return 1;
  }

  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

  tree::DiagnosticEngine diag(filename, source);

  try {
    auto tokens = tree::lexer(source, diag);
    auto program = tree::parse(tokens, diag);

    if (diag.has_errors()) {
      diag.print_all(std::cerr);
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <file>\n";
    return 1;
  }

  return run_file(argv[1]);
}