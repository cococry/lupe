#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOBDEF static inline
#define NOB_IMPLEMENTATION
#define NOB_EXPERIMENTAL_DELETE_OLD
#define NOB_WARN_DEPRECATED
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#define command(arg, commands, name, signature, description) \
  command_loc(__FILE__, __LINE__, (arg), (commands), (name), (signature), (description))

#define BUILD_FOLDER "./build/"
#define BINARY_NAME "lupe"
#define BUILD_BINARY BUILD_FOLDER BINARY_NAME
#define DEFAULT_INSTALL_PREFIX "/usr/local"

typedef struct {
  const char *name;
  const char *signature;
  const char *description;
} Command;

typedef struct {
  Command *items;
  size_t count;
  size_t capacity;

  bool picked;
  const char *picked_name;
  const char *picked_at_file;
  int picked_at_line;
} Commands;

void commands_reset(Commands *commands)
{
  commands->count = 0;
  commands->picked = false;
}

void print_available_commands(Commands commands)
{
  size_t max_name_width = 0;
  size_t max_sign_width = 0;

  da_foreach(Command, command, &commands) {
    size_t name_width = strlen(command->name);
    size_t sign_width = strlen(command->signature);

    if (name_width > max_name_width) max_name_width = name_width;
    if (sign_width > max_sign_width) max_sign_width = sign_width;
  }

  nob_log(INFO, "Available commands:");

  da_foreach(Command, command, &commands) {
    nob_log(INFO,
            "    %-*s %-*s - %s",
            (int)max_name_width,
            command->name,
            (int)max_sign_width,
            command->signature,
            command->description);
  }
}

bool command_loc(const char *file,
                 int line,
                 const char *arg,
                 Commands *commands,
                 const char *name,
                 const char *signature,
                 const char *description)
{
  if (commands->picked) {
    fprintf(stderr,
            "%s:%d: ASSERTION FAILED: the branch for command `%s` fell through.\n",
            commands->picked_at_file,
            commands->picked_at_line,
            commands->picked_name);

    fprintf(stderr,
            "%s:%d: NOTE: the execution proceeded to here, but the command was already picked.\n",
            file,
            line);

    abort();
  }

  Command command = {
    .name = name,
    .signature = signature,
    .description = description,
  };

  da_append(commands, command);

  commands->picked_name    = name;
  commands->picked_at_line = line;
  commands->picked_at_file = file;
  commands->picked         = strcmp(arg, name) == 0;

  return commands->picked;
}

bool build(void)
{
  if (!mkdir_if_not_exists(BUILD_FOLDER)) return false;

  Cmd cmd = {0};

  nob_cc(&cmd);
  nob_cc_flags(&cmd);
  nob_cc_output(&cmd, BUILD_BINARY);
  nob_cmd_append(&cmd, "-lGL", "-lX11");
  nob_cc_inputs(&cmd, "src/lupe.c");

  if (!cmd_run(&cmd)) return false;

  return true;
}

bool install(const char *prefix)
{
  if (prefix == NULL) {
    prefix = DEFAULT_INSTALL_PREFIX;
  }

  if (!build()) return false;

  const char *bin_dir = temp_sprintf("%s/bin", prefix);
  const char *target = temp_sprintf("%s/%s", bin_dir, BINARY_NAME);

  if (!mkdir_if_not_exists(bin_dir)) return false;

  Cmd cmd = {0};
  cmd_append(&cmd, "cp", BUILD_BINARY, target);

  if (!cmd_run(&cmd)) return false;

  nob_log(INFO, "Installed %s to %s", BINARY_NAME, target);

  return true;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif // _WIN32

  GO_REBUILD_URSELF_PLUS(argc, argv, "third_party/nob.h");

  set_log_handler(cancer_log_handler);

  const char *program_name = shift(argv, argc);
  (void)program_name;

  const char *command_name = "build";
  if (argc > 0) command_name = shift(argv, argc);

  Commands commands = {0};

  commands_reset(&commands);

  if (command(command_name, &commands, "build", "", "Build the lupe binary")) {
    if (!build()) return 1;
    return 0;
  }

  if (command(command_name, &commands, "install", "[prefix]", "Build and install the lupe binary")) {
    const char *prefix = DEFAULT_INSTALL_PREFIX;

    if (argc > 0) {
      prefix = shift(argv, argc);
    }

    if (argc > 0) {
      nob_log(ERROR, "Too many arguments for command `install`");
      nob_log(INFO, "Usage: %s install [prefix]", program_name);
      return 1;
    }

    if (!install(prefix)) return 1;

    return 0;
  }

  if (command(command_name, &commands, "help", "", "Print this help message")) {
    print_available_commands(commands);
    return 0;
  }

  print_available_commands(commands);
  nob_log(ERROR, "Unknown command %s", command_name);

  return 1;
}
