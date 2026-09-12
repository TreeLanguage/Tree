#include "cli.hpp"
#include "diagnostic.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"
#include "tree/config.hpp"
#include <CLI/CLI.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Command = int (*)(CLI::App &);

struct CommandEntry {
  std::string_view name;
  Command function{};
};

class TreeFormatter : public CLI::Formatter {
public:
  TreeFormatter() = default;

  std::string make_help(const CLI::App *app, std::string /*unused*/,
                        CLI::AppFormatMode /*mode*/) const override {
    std::ostringstream out;

    out << "usage: " << app->get_name() << " <command>\n\n";
    out << app->get_description() << "\n\n";

    out << "Commands:\n"
        << "  help       show this help message\n"
        << "  version    show version information\n"
        << "  lsp        start the language server\n"
        << "  <file>     compile a Tree source file\n";

    return out.str();
  }
};

std::string read_file(const std::filesystem::path &path) {
  const std::ifstream file(path, std::ios::binary);

  if (!file) {
    throw std::runtime_error("unable to open '" + path.string() + "'");
  }

  std::ostringstream stream;
  stream << file.rdbuf();

  return stream.str();
}

int compile_file(const std::filesystem::path &path) {
  const std::string source = read_file(path);

  tree::DiagnosticEngine diagnostics(path.string(), source);

  const auto tokens = tree::lexer(source, diagnostics);
  [[maybe_unused]] const auto ast = tree::parse(tokens, diagnostics);

  if (diagnostics.has_errors()) {
    diagnostics.print_all(std::cerr);
    return 1;
  }

  return 0;
}

int run_help(CLI::App &app) {
  std::cout << app.help();
  return 0;
}

int run_version(CLI::App & /*unused*/) {
  std::cout << "tree " << tree::config::VERSION << '\n';
  return 0;
}

int run_lsp(CLI::App & /*unused*/) { return 0; }

constexpr std::array<CommandEntry, 3> COMMANDS = {{
    {.name = "help", .function = run_help},
    {.name = "version", .function = run_version},
    {.name = "lsp", .function = run_lsp},
}};

} // namespace

namespace tree {

int run(int argc, char **argv) {
  CLI::App app{"The Tree Programming Language"};

  app.formatter(std::make_shared<TreeFormatter>());

  for (const auto &[name, function] : COMMANDS) {
    auto *command = app.add_subcommand(std::string{name});
    command->callback([&app, function] { function(app); });
  }

  std::filesystem::path input;

  app.add_option("file", input, "Tree source file")->check(CLI::ExistingFile);

  CLI11_PARSE(app, argc, argv);

  if (!input.empty()) {
    return compile_file(input);
  }

  std::cerr << app.help();
  return 1;
}

} // namespace tree
