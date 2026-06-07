//===-- StandardCommands.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LoadStandardCommands instantiates every concrete CommandObject defined in
// lldbCommands and registers it with a CommandInterpreter. The hook is wired
// up by SystemInitializerFull (in liblldb) via
// CommandInterpreter::SetStandardCommandsLoader, which keeps lldbInterpreter
// free of any dependency on lldbCommands -- tools like lldb-server that ship
// only the framework and the gdb-remote plugins can drop ~9 MB of
// CommandObject* code from their link line by simply not registering this
// loader.
//
//===----------------------------------------------------------------------===//

#include "Commands/CommandObjectApropos.h"
#include "Commands/CommandObjectBreakpoint.h"
#include "Commands/CommandObjectCommands.h"
#include "Commands/CommandObjectDWIMPrint.h"
#include "Commands/CommandObjectDiagnostics.h"
#include "Commands/CommandObjectDisassemble.h"
#include "Commands/CommandObjectExpression.h"
#include "Commands/CommandObjectFrame.h"
#include "Commands/CommandObjectGUI.h"
#include "Commands/CommandObjectHelp.h"
#include "Commands/CommandObjectLanguage.h"
#include "Commands/CommandObjectLog.h"
#include "Commands/CommandObjectMemory.h"
#include "Commands/CommandObjectPlatform.h"
#include "Commands/CommandObjectPlugin.h"
#include "Commands/CommandObjectProcess.h"
#include "Commands/CommandObjectProtocolServer.h"
#include "Commands/CommandObjectQuit.h"
#include "Commands/CommandObjectRegexCommand.h"
#include "Commands/CommandObjectRegister.h"
#include "Commands/CommandObjectScripting.h"
#include "Commands/CommandObjectSession.h"
#include "Commands/CommandObjectSettings.h"
#include "Commands/CommandObjectSource.h"
#include "Commands/CommandObjectStats.h"
#include "Commands/CommandObjectTarget.h"
#include "Commands/CommandObjectThread.h"
#include "Commands/CommandObjectTrace.h"
#include "Commands/CommandObjectType.h"
#include "Commands/CommandObjectVersion.h"
#include "Commands/CommandObjectWatchpoint.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Timer.h"

namespace lldb_private {

#define REGISTER_COMMAND_OBJECT(NAME, CLASS)                                   \
  interpreter.AddCommand(NAME, std::make_shared<CLASS>(interpreter),           \
                         /*can_replace=*/false)

void LoadStandardCommands(CommandInterpreter &interpreter) {
  LLDB_SCOPED_TIMER();

  REGISTER_COMMAND_OBJECT("apropos", CommandObjectApropos);
  REGISTER_COMMAND_OBJECT("breakpoint", CommandObjectMultiwordBreakpoint);
  REGISTER_COMMAND_OBJECT("command", CommandObjectMultiwordCommands);
  REGISTER_COMMAND_OBJECT("diagnostics", CommandObjectDiagnostics);
  REGISTER_COMMAND_OBJECT("disassemble", CommandObjectDisassemble);
  REGISTER_COMMAND_OBJECT("dwim-print", CommandObjectDWIMPrint);
  REGISTER_COMMAND_OBJECT("expression", CommandObjectExpression);
  REGISTER_COMMAND_OBJECT("frame", CommandObjectMultiwordFrame);
  REGISTER_COMMAND_OBJECT("gui", CommandObjectGUI);
  REGISTER_COMMAND_OBJECT("help", CommandObjectHelp);
  REGISTER_COMMAND_OBJECT("log", CommandObjectLog);
  REGISTER_COMMAND_OBJECT("memory", CommandObjectMemory);
  REGISTER_COMMAND_OBJECT("platform", CommandObjectPlatform);
  REGISTER_COMMAND_OBJECT("plugin", CommandObjectPlugin);
  REGISTER_COMMAND_OBJECT("process", CommandObjectMultiwordProcess);
  REGISTER_COMMAND_OBJECT("protocol-server", CommandObjectProtocolServer);
  REGISTER_COMMAND_OBJECT("quit", CommandObjectQuit);
  REGISTER_COMMAND_OBJECT("register", CommandObjectRegister);
  REGISTER_COMMAND_OBJECT("scripting", CommandObjectMultiwordScripting);
  REGISTER_COMMAND_OBJECT("settings", CommandObjectMultiwordSettings);
  REGISTER_COMMAND_OBJECT("session", CommandObjectSession);
  REGISTER_COMMAND_OBJECT("source", CommandObjectMultiwordSource);
  REGISTER_COMMAND_OBJECT("statistics", CommandObjectStats);
  REGISTER_COMMAND_OBJECT("target", CommandObjectMultiwordTarget);
  REGISTER_COMMAND_OBJECT("thread", CommandObjectMultiwordThread);
  REGISTER_COMMAND_OBJECT("trace", CommandObjectTrace);
  REGISTER_COMMAND_OBJECT("type", CommandObjectType);
  REGISTER_COMMAND_OBJECT("version", CommandObjectVersion);
  REGISTER_COMMAND_OBJECT("watchpoint", CommandObjectMultiwordWatchpoint);
  REGISTER_COMMAND_OBJECT("language", CommandObjectLanguage);

  // clang-format off
  const char *break_regexes[][2] = {
      {"^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*$",
       "breakpoint set --file '%1' --line %2 --column %3"},
      {"^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*$",
       "breakpoint set --file '%1' --line %2"},
      {"^/([^/]+)/$", "breakpoint set --source-pattern-regexp '%1'"},
      {"^([[:digit:]]+)[[:space:]]*$", "breakpoint set --line %1"},
      {"^\\*?(0x[[:xdigit:]]+)[[:space:]]*$", "breakpoint set --address %1"},
      {"^[\"']?([-+]?\\[.*\\])[\"']?[[:space:]]*$",
       "breakpoint set --name '%1'"},
      {"^(-.*)$", "breakpoint set %1"},
      {"^(.*[^[:space:]])`(.*[^[:space:]])[[:space:]]*$",
       "breakpoint set --name '%2' --shlib '%1'"},
      {"^\\&(.*[^[:space:]])[[:space:]]*$",
       "breakpoint set --name '%1' --skip-prologue=0"},
      {"^[\"']?(.*[^[:space:]\"'])[\"']?[[:space:]]*$",
       "breakpoint set --name '%1'"}};
  // clang-format on

  size_t num_regexes = std::size(break_regexes);

  std::unique_ptr<CommandObjectRegexCommand> break_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-break",
          "Set a breakpoint using one of several shorthand formats, or list "
          "the existing breakpoints if no arguments are provided.",
          "\n"
          "_regexp-break <filename>:<linenum>:<colnum>\n"
          "              main.c:12:21          // Break at line 12 and column "
          "21 of main.c\n\n"
          "_regexp-break <filename>:<linenum>\n"
          "              main.c:12             // Break at line 12 of "
          "main.c\n\n"
          "_regexp-break <linenum>\n"
          "              12                    // Break at line 12 of current "
          "file\n\n"
          "_regexp-break 0x<address>\n"
          "              0x1234000             // Break at address "
          "0x1234000\n\n"
          "_regexp-break <name>\n"
          "              main                  // Break in 'main' after the "
          "prologue\n\n"
          "_regexp-break &<name>\n"
          "              &main                 // Break at first instruction "
          "in 'main'\n\n"
          "_regexp-break <module>`<name>\n"
          "              libc.so`malloc        // Break in 'malloc' from "
          "'libc.so'\n\n"
          "_regexp-break /<source-regex>/\n"
          "              /break here/          // Break on source lines in "
          "current file\n"
          "                                    // containing text 'break "
          "here'.\n"
          "_regexp-break\n"
          "                                    // List the existing "
          "breakpoints\n",
          lldb::eSymbolCompletion | lldb::eSourceFileCompletion, false));

  if (break_regex_cmd_up) {
    bool success = true;
    for (size_t i = 0; i < num_regexes; i++) {
      success = break_regex_cmd_up->AddRegexCommand(break_regexes[i][0],
                                                    break_regexes[i][1]);
      if (!success)
        break;
    }
    success =
        break_regex_cmd_up->AddRegexCommand("^$", "breakpoint list --full");

    if (success) {
      lldb::CommandObjectSP break_regex_cmd_sp(break_regex_cmd_up.release());
      interpreter.AddCommand(break_regex_cmd_sp->GetCommandName(),
                             break_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  // clang-format off
  // FIXME: It would be simpler to just use the linespec's directly here, but
  // the `b` alias allows "foo.c   :   12   :   45" but the linespec parser
  // is more rigorous, and doesn't strip spaces, so the two are not equivalent.
  const char *break_add_regexes[][2] = {
      {"^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*$",
       "breakpoint add file --file '%1' --line %2 --column %3"},
      {"^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*$",
       "breakpoint add file --file '%1' --line %2"},
      {"^/([^/]+)/$", "breakpoint add pattern -- %1"},
      {"^([[:digit:]]+)[[:space:]]*$",
      "breakpoint add file --line %1"},
      {"^\\*?(0x[[:xdigit:]]+)[[:space:]]*$",
      "breakpoint add address %1"},
      {"^[\"']?([-+]?\\[.*\\])[\"']?[[:space:]]*$",
       "breakpoint add name '%1'"},
      {"^(-.*)$",
      "breakpoint add name '%1'"},
      {"^(.*[^[:space:]])`(.*[^[:space:]])[[:space:]]*$",
       "breakpoint add name '%2' --shlib '%1'"},
      {"^\\&(.*[^[:space:]])[[:space:]]*$",
       "breakpoint add name '%1' --skip-prologue=0"},
      {"^[\"']?(.*[^[:space:]\"'])[\"']?[[:space:]]*$",
       "breakpoint add name '%1'"}};
  // clang-format on

  size_t num_add_regexes = std::size(break_add_regexes);

  std::unique_ptr<CommandObjectRegexCommand> break_add_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-break-add",
          "Set a breakpoint using one of several shorthand formats, or list "
          "the existing breakpoints if no arguments are provided.",
          "\n"
          "_regexp-break-add <filename>:<linenum>:<colnum>\n"
          "              main.c:12:21          // Break at line 12 and column "
          "21 of main.c\n\n"
          "_regexp-break-add <filename>:<linenum>\n"
          "              main.c:12             // Break at line 12 of "
          "main.c\n\n"
          "_regexp-break-add <linenum>\n"
          "              12                    // Break at line 12 of current "
          "file\n\n"
          "_regexp-break-add 0x<address>\n"
          "              0x1234000             // Break at address "
          "0x1234000\n\n"
          "_regexp-break-add <name>\n"
          "              main                  // Break in 'main' after the "
          "prologue\n\n"
          "_regexp-break-add &<name>\n"
          "              &main                 // Break at first instruction "
          "in 'main'\n\n"
          "_regexp-break-add <module>`<name>\n"
          "              libc.so`malloc        // Break in 'malloc' from "
          "'libc.so'\n\n"
          "_regexp-break-add /<source-regex>/\n"
          "              /break here/          // Break on source lines in "
          "current file\n"
          "                                    // containing text 'break "
          "here'.\n"
          "_regexp-break-add\n"
          "                                    // List the existing "
          "breakpoints\n",
          lldb::eSymbolCompletion | lldb::eSourceFileCompletion, false));

  if (break_add_regex_cmd_up) {
    bool success = true;
    for (size_t i = 0; i < num_add_regexes; i++) {
      success = break_add_regex_cmd_up->AddRegexCommand(
          break_add_regexes[i][0], break_add_regexes[i][1]);
      if (!success)
        break;
    }
    success =
        break_add_regex_cmd_up->AddRegexCommand("^$", "breakpoint list --full");

    if (success) {
      lldb::CommandObjectSP break_add_regex_cmd_sp(
          break_add_regex_cmd_up.release());
      interpreter.AddCommand(break_add_regex_cmd_sp->GetCommandName(),
                             break_add_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> tbreak_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-tbreak",
          "Set a one-shot breakpoint using one of several shorthand formats.",
          "\n"
          "_regexp-break <filename>:<linenum>:<colnum>\n"
          "              main.c:12:21          // Break at line 12 and column "
          "21 of main.c\n\n"
          "_regexp-break <filename>:<linenum>\n"
          "              main.c:12             // Break at line 12 of "
          "main.c\n\n"
          "_regexp-break <linenum>\n"
          "              12                    // Break at line 12 of current "
          "file\n\n"
          "_regexp-break 0x<address>\n"
          "              0x1234000             // Break at address "
          "0x1234000\n\n"
          "_regexp-break <name>\n"
          "              main                  // Break in 'main' after the "
          "prologue\n\n"
          "_regexp-break &<name>\n"
          "              &main                 // Break at first instruction "
          "in 'main'\n\n"
          "_regexp-break <module>`<name>\n"
          "              libc.so`malloc        // Break in 'malloc' from "
          "'libc.so'\n\n"
          "_regexp-break /<source-regex>/\n"
          "              /break here/          // Break on source lines in "
          "current file\n"
          "                                    // containing text 'break "
          "here'.\n",
          lldb::eSymbolCompletion | lldb::eSourceFileCompletion, false));

  if (tbreak_regex_cmd_up) {
    bool success = true;
    for (size_t i = 0; i < num_regexes; i++) {
      std::string command = break_regexes[i][1];
      command += " -o 1";
      success =
          tbreak_regex_cmd_up->AddRegexCommand(break_regexes[i][0], command);
      if (!success)
        break;
    }
    success =
        tbreak_regex_cmd_up->AddRegexCommand("^$", "breakpoint list --full");

    if (success) {
      lldb::CommandObjectSP tbreak_regex_cmd_sp(tbreak_regex_cmd_up.release());
      interpreter.AddCommand(tbreak_regex_cmd_sp->GetCommandName(),
                             tbreak_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> attach_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-attach", "Attach to process by ID or name.",
          "_regexp-attach <pid> | <process-name>", 0, false));
  if (attach_regex_cmd_up) {
    if (attach_regex_cmd_up->AddRegexCommand("^([0-9]+)[[:space:]]*$",
                                             "process attach --pid %1") &&
        attach_regex_cmd_up->AddRegexCommand(
            "^(-.*|.* -.*)$", "process attach %1") && // Any options that are
                                                      // specified get passed to
                                                      // 'process attach'
        attach_regex_cmd_up->AddRegexCommand("^(.+)$",
                                             "process attach --name '%1'") &&
        attach_regex_cmd_up->AddRegexCommand("^$", "process attach")) {
      lldb::CommandObjectSP attach_regex_cmd_sp(attach_regex_cmd_up.release());
      interpreter.AddCommand(attach_regex_cmd_sp->GetCommandName(),
                             attach_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> down_regex_cmd_up(
      new CommandObjectRegexCommand(interpreter, "_regexp-down",
                                    "Select a newer stack frame.  Defaults to "
                                    "moving one frame, a numeric argument can "
                                    "specify an arbitrary number.",
                                    "_regexp-down [<count>]", 0, false));
  if (down_regex_cmd_up) {
    if (down_regex_cmd_up->AddRegexCommand("^$", "frame select -r -1") &&
        down_regex_cmd_up->AddRegexCommand("^([0-9]+)$",
                                           "frame select -r -%1")) {
      lldb::CommandObjectSP down_regex_cmd_sp(down_regex_cmd_up.release());
      interpreter.AddCommand(down_regex_cmd_sp->GetCommandName(),
                             down_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> up_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-up",
          "Select an older stack frame.  Defaults to moving one "
          "frame, a numeric argument can specify an arbitrary number.",
          "_regexp-up [<count>]", 0, false));
  if (up_regex_cmd_up) {
    if (up_regex_cmd_up->AddRegexCommand("^$", "frame select -r 1") &&
        up_regex_cmd_up->AddRegexCommand("^([0-9]+)$", "frame select -r %1")) {
      lldb::CommandObjectSP up_regex_cmd_sp(up_regex_cmd_up.release());
      interpreter.AddCommand(up_regex_cmd_sp->GetCommandName(),
                             up_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> display_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-display",
          "Evaluate an expression at every stop (see 'help target stop-hook'.)",
          "_regexp-display expression", 0, false));
  if (display_regex_cmd_up) {
    if (display_regex_cmd_up->AddRegexCommand(
            "^(.+)$", "target stop-hook add -o \"expr -- %1\"")) {
      lldb::CommandObjectSP display_regex_cmd_sp(
          display_regex_cmd_up.release());
      interpreter.AddCommand(display_regex_cmd_sp->GetCommandName(),
                             display_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> undisplay_regex_cmd_up(
      new CommandObjectRegexCommand(interpreter, "_regexp-undisplay",
                                    "Stop displaying expression at every "
                                    "stop (specified by stop-hook index.)",
                                    "_regexp-undisplay stop-hook-number", 0,
                                    false));
  if (undisplay_regex_cmd_up) {
    if (undisplay_regex_cmd_up->AddRegexCommand("^([0-9]+)$",
                                                "target stop-hook delete %1")) {
      lldb::CommandObjectSP undisplay_regex_cmd_sp(
          undisplay_regex_cmd_up.release());
      interpreter.AddCommand(undisplay_regex_cmd_sp->GetCommandName(),
                             undisplay_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> connect_gdb_remote_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "gdb-remote",
          "Connect to a process via remote GDB server.\n"
          "If no host is specified, localhost is assumed.\n"
          "gdb-remote is an abbreviation for 'process connect --plugin "
          "gdb-remote connect://<hostname>:<port>'\n",
          "gdb-remote [<hostname>:]<portnum>", 0, false));
  if (connect_gdb_remote_cmd_up) {
    if (connect_gdb_remote_cmd_up->AddRegexCommand(
            "^([^:]+|\\[[0-9a-fA-F:]+.*\\]):([0-9]+)$",
            "process connect --plugin gdb-remote connect://%1:%2") &&
        connect_gdb_remote_cmd_up->AddRegexCommand(
            "^([[:digit:]]+)$",
            "process connect --plugin gdb-remote connect://localhost:%1")) {
      lldb::CommandObjectSP command_sp(connect_gdb_remote_cmd_up.release());
      interpreter.AddCommand(command_sp->GetCommandName(), command_sp,
                             /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> connect_kdp_remote_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "kdp-remote",
          "Connect to a process via remote KDP server.\n"
          "If no UDP port is specified, port 41139 is assumed.\n"
          "kdp-remote is an abbreviation for 'process connect --plugin "
          "kdp-remote udp://<hostname>:<port>'\n",
          "kdp-remote <hostname>[:<portnum>]", 0, false));
  if (connect_kdp_remote_cmd_up) {
    if (connect_kdp_remote_cmd_up->AddRegexCommand(
            "^([^:]+:[[:digit:]]+)$",
            "process connect --plugin kdp-remote udp://%1") &&
        connect_kdp_remote_cmd_up->AddRegexCommand(
            "^(.+)$", "process connect --plugin kdp-remote udp://%1:41139")) {
      lldb::CommandObjectSP command_sp(connect_kdp_remote_cmd_up.release());
      interpreter.AddCommand(command_sp->GetCommandName(), command_sp,
                             /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> bt_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-bt",
          "Show backtrace of the current thread's call stack. Any numeric "
          "argument displays at most that many frames. The argument 'all' "
          "displays all threads. Use 'settings set frame-format' to customize "
          "the printing of individual frames and 'settings set thread-format' "
          "to customize the thread header. Frame recognizers may filter the "
          "list. Use 'thread backtrace -u (--unfiltered)' to see them all.",
          "bt [<digit> | all]", 0, false));
  if (bt_regex_cmd_up) {
    // accept but don't document "bt -c <number>" -- before bt was a regex
    // command if you wanted to backtrace three frames you would do "bt -c 3"
    // but the intention is to have this emulate the gdb "bt" command and so
    // now "bt 3" is the preferred form, in line with gdb.
    if (bt_regex_cmd_up->AddRegexCommand("^([[:digit:]]+)[[:space:]]*$",
                                         "thread backtrace -c %1") &&
        bt_regex_cmd_up->AddRegexCommand("^(-[^[:space:]].*)$",
                                         "thread backtrace %1") &&
        bt_regex_cmd_up->AddRegexCommand("^all[[:space:]]*$",
                                         "thread backtrace all") &&
        bt_regex_cmd_up->AddRegexCommand("^[[:space:]]*$",
                                         "thread backtrace")) {
      lldb::CommandObjectSP command_sp(bt_regex_cmd_up.release());
      interpreter.AddCommand(command_sp->GetCommandName(), command_sp,
                             /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> list_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-list",
          "List relevant source code using one of several shorthand formats.",
          "\n"
          "_regexp-list <file>:<line>   // List around specific file/line\n"
          "_regexp-list <line>          // List current file around specified "
          "line\n"
          "_regexp-list <function-name> // List specified function\n"
          "_regexp-list 0x<address>     // List around specified address\n"
          "_regexp-list -[<count>]      // List previous <count> lines\n"
          "_regexp-list                 // List subsequent lines",
          lldb::eSourceFileCompletion, false));
  if (list_regex_cmd_up) {
    if (list_regex_cmd_up->AddRegexCommand("^([0-9]+)[[:space:]]*$",
                                           "source list --line %1") &&
        list_regex_cmd_up->AddRegexCommand(
            "^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:"
            "]]*$",
            "source list --file '%1' --line %2") &&
        list_regex_cmd_up->AddRegexCommand(
            "^\\*?(0x[[:xdigit:]]+)[[:space:]]*$",
            "source list --address %1") &&
        list_regex_cmd_up->AddRegexCommand("^-[[:space:]]*$",
                                           "source list --reverse") &&
        list_regex_cmd_up->AddRegexCommand(
            "^-([[:digit:]]+)[[:space:]]*$",
            "source list --reverse --count %1") &&
        list_regex_cmd_up->AddRegexCommand("^(.+)$",
                                           "source list --name \"%1\"") &&
        list_regex_cmd_up->AddRegexCommand("^$", "source list")) {
      lldb::CommandObjectSP list_regex_cmd_sp(list_regex_cmd_up.release());
      interpreter.AddCommand(list_regex_cmd_sp->GetCommandName(),
                             list_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> env_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-env",
          "Shorthand for viewing and setting environment variables.",
          "\n"
          "_regexp-env                  // Show environment\n"
          "_regexp-env <name>=<value>   // Set an environment variable",
          0, false));
  if (env_regex_cmd_up) {
    if (env_regex_cmd_up->AddRegexCommand("^$",
                                          "settings show target.env-vars") &&
        env_regex_cmd_up->AddRegexCommand("^([A-Za-z_][A-Za-z_0-9]*=.*)$",
                                          "settings set target.env-vars %1")) {
      lldb::CommandObjectSP env_regex_cmd_sp(env_regex_cmd_up.release());
      interpreter.AddCommand(env_regex_cmd_sp->GetCommandName(),
                             env_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> jump_regex_cmd_up(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-jump",
          "Set the program counter to a new address.",
          "\n"
          "_regexp-jump <line>\n"
          "_regexp-jump +<line-offset> | -<line-offset>\n"
          "_regexp-jump <file>:<line>\n"
          "_regexp-jump *<addr>\n",
          0, false));
  if (jump_regex_cmd_up) {
    if (jump_regex_cmd_up->AddRegexCommand("^\\*(.*)$",
                                           "thread jump --addr %1") &&
        jump_regex_cmd_up->AddRegexCommand("^([0-9]+)$",
                                           "thread jump --line %1") &&
        jump_regex_cmd_up->AddRegexCommand("^([^:]+):([0-9]+)$",
                                           "thread jump --file %1 --line %2") &&
        jump_regex_cmd_up->AddRegexCommand("^([+\\-][0-9]+)$",
                                           "thread jump --by %1")) {
      lldb::CommandObjectSP jump_regex_cmd_sp(jump_regex_cmd_up.release());
      interpreter.AddCommand(jump_regex_cmd_sp->GetCommandName(),
                             jump_regex_cmd_sp, /*can_replace=*/false);
    }
  }

  std::shared_ptr<CommandObjectRegexCommand> step_regex_cmd_sp(
      new CommandObjectRegexCommand(
          interpreter, "_regexp-step",
          "Single step, optionally to a specific function.",
          "\n"
          "_regexp-step                 // Single step\n"
          "_regexp-step <function-name> // Step into the named function\n",
          0, false));
  if (step_regex_cmd_sp) {
    if (step_regex_cmd_sp->AddRegexCommand("^[[:space:]]*$",
                                           "thread step-in") &&
        step_regex_cmd_sp->AddRegexCommand("^[[:space:]]*(-.+)$",
                                           "thread step-in %1") &&
        step_regex_cmd_sp->AddRegexCommand(
            "^[[:space:]]*(.+)[[:space:]]*$",
            "thread step-in --end-linenumber block --step-in-target %1")) {
      interpreter.AddCommand(step_regex_cmd_sp->GetCommandName(),
                             step_regex_cmd_sp, /*can_replace=*/false);
    }
  }
}

#undef REGISTER_COMMAND_OBJECT

} // namespace lldb_private
