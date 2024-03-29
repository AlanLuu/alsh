# alsh
A custom UNIX shell written in C

# Features
- Execute commands (e.g. `ls`)
- Execute commands with arguments and flags (e.g. `ls -la /`)
- Execute commands with redirection `<` `>` `>>`
    - Output from a given file descriptor can be redirected or appended to a file by using `n>` or `n>>` respectively, where `n` is the file descriptor number
- Execute commands with pipes `|`
- Execute commands in the background by appending `&` at the end of the command
- Execute multiple commands separated by `;` on the same line
    - Given the statement `cmd1; cmd2`, `cmd1` and `cmd2` are executed sequentially
- Evaluate math expressions by surrounding them with `()`, such as `(1 + 1)` and `(1 + 2 * (3 + 4))`
    - Supports `+`, `-`, `*`, `/`, and grouping expressions using `(` and `)`
- Other operators
    - `&&`: Given the statement `cmd1 && cmd2`, `cmd2` is executed if and only if `cmd1` returns an exit status of 0, which indicates success
    - `||`: Given the statement `cmd1 || cmd2`, `cmd2` is executed if and only if `cmd1` returns a non-zero exit status, which indicates failure
- View command history with `history` and execute previous commands with `!n`, where `n` is the command number in the history list (e.g. `!3` will execute the third command in the history list)
    - To execute the previous command, use `!!`
    - To execute the command `n` lines back in the history list, use `!-n` (e.g. `!-2` will execute the command 2 lines back in the history list)
    - To clear the history list, use `history -c`
    - To write the history list to a file, use `history -w`, which will write the list to `~/.alsh_history`
- Create custom aliases for commands by using `alias alias_name=command`, where `alias_name` is the alias name and `command` is the command to execute when the alias is typed as a command
    - Multiple aliases can be stored at once by using `alias alias_name_1=command_1 alias_name_2=command_2 alias_name_3=command_3 ...`
    - To list all stored aliases for the current shell session, simply type `alias` without any arguments
- Create environment variables and shell variables by using the `export` and `let` keywords respectively
    - `export env_var_name=env_var_value`
    - `let var_name=var_value`
    - Multiple variables can be created at once by using `<export|let> var_name_1=var_value_1 var_name_2=var_value_2 var_name_3=var_value_3 ...`
    - To use the value of a variable in a command, prefix it with `$`, like `echo $var_name`
- Replace the current alsh shell's process with a new process by using `exec [command]`
    - Running `exec` without specifying a command will replace the current alsh shell's process with a new instance of another alsh shell
- `repeat (n) <command>` will execute the given command `n` times
    - Multiple `repeat` loops can be chained together (e.g. `repeat (n) repeat (m) <command>` will execute the given command `m * n` times)
- Execute commands from a file in the current alsh shell session by using `source <fileName>`
- `if (<commandToTest>) <command>` will only execute the given command if `commandToTest` returns an exit status of 0, which indicates success
    - `if (<commandToTest>) <command1> else <command2>` will execute the first command if `commandToTest` returns an exit status of 0, and the second command otherwise
    - `if (<commandToTest>) <command1> else if (<commandToTest2>) <command2> else <command3>` will execute the first command if `commandToTest` returns an exit status of 0, which indicates success, the second command if `commandToTest` returns a non-zero exit status and `commandToTest2` returns an exit status of 0, and the third command otherwise
    - The command to test can be negated by using the `-` operator (e.g. `if (-<commandToTest>) <command>` will only execute the given command if `commandToTest` returns a non-zero exit status, which indicates failure)
        - Negated commands can also be negated themselves, so `if (--<commandToTest>) <command>` is equivalent to `if (<commandToTest>) <command>`
        - An odd number of `-` operators will negate the command, and an even number of `-` operators will not negate the command
- `while (<commandToTest>) <command>` will repeatedly execute the given command as long as `commandToTest` returns an exit status of 0
- If the `[` command is available on the system, then it doesn't need to be surrounded with parentheses in `if` and `while` statements
    - Example: `if [ 1 -eq 1 ] <command>`
- Compare numerical values by using `chk <num1> <cond> <num2>`, where `num1` and `num2` are the first and second numerical values to compare respectively, and `cond` is the test condition to use on `num1` and `num2`
    - Valid test conditions for `cond` are the following: `eq`, `ne`, `lt`, `le`, `gt`, `ge`, which stand for equals, not equals, less than, less than or equal to, greater than, and greater than or equal to respectively
    - This can be useful if the `[` command is not available on the system
- If `.alshrc` is present in the home directory, then it will be executed at the start of any interactive alsh shell session

# Installation
```
git clone https://github.com/AlanLuu/alsh.git
cd alsh
make
sudo make install
```
This will install alsh as `alsh` in `/usr/local/bin`.

To uninstall alsh, run the following command in the alsh directory:
```
sudo make uninstall
```

# Testing
Test cases are defined in `tests.json`.
- Cases are defined in the format `"test_command": "output"|null`, where `test_command` is the command to be tested and `output` is the expected output of the command.
    - If `null` is specified in place of `output`, then the test command will be executed using the system shell and the output of that will be used as the expected output.

To run tests, run the following command (requires Python):
```
make test
```

# License
alsh is distributed under the terms of the [MIT License](https://github.com/AlanLuu/alsh/blob/main/LICENSE).
