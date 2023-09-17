# alsh
A custom UNIX shell written in C

# Features
- Execute commands (e.g. `ls`)
- Execute commands with arguments and flags (e.g. `ls -la /`)
- Execute commands with redirection `<` `>` `>>`
    - Output from a given file descriptor can be redirected or appended to a file by using `n>` or `n>>` respectively, where `n` is the file descriptor number
- Execute commands with pipes `|`
- Execute multiple commands separated by `;` on the same line
    - Given the statement `cmd1; cmd2`, `cmd1` and `cmd2` are executed sequentially
- Other operators
    - `&&`: Given the statement `cmd1 && cmd2`, `cmd2` is executed if and only if `cmd1` returns an exit status of 0, which indicates success
    - `||`: Given the statement `cmd1 || cmd2`, `cmd2` is executed if and only if `cmd1` returns a non-zero exit status, which indicates failure
- View command history with `history` and execute previous commands with `!n`, where `n` is the command number in the history list (e.g. `!3` will execute the third command in the history list)
    - To execute the previous command, use `!!`
    - To execute the command `n` lines back in the history list, use `!-n` (e.g. `!-2` will execute the command 2 lines back in the history list)
    - To clear the history list, use `history -c`
    - To write the history list to a file, use `history -w`, which will write the list to `~/.alsh_history`
- Replace the current alsh shell's process with a new process by using `exec [command]`
    - Running `exec` without specifying a command will replace the current alsh shell's process with a new instance of another alsh shell
- `repeat (n) <command>` will execute the given command `n` times
    - Multiple `repeat` loops can be chained together (e.g. `repeat (n) repeat (m) <command>` will execute the given command `m * n` times)
- `if (<commandToTest>) <command>` will only execute the given command if `commandToTest` returns an exit status of 0, which indicates success
    - `if (<commandToTest>) <command1> else <command2>` will execute the first command if `commandToTest` returns an exit status of 0, and the second command otherwise
    - `if (<commandToTest>) <command1> else if (<commandToTest2>) <command2> else <command3>` will execute the first command if `commandToTest` returns an exit status of 0, which indicates success, the second command if `commandToTest` returns a non-zero exit status and `commandToTest2` returns an exit status of 0, and the third command otherwise
    - The command to test can be negated by using the `-` operator (e.g. `if (-<commandToTest>) <command>` will only execute the given command if `commandToTest` returns a non-zero exit status, which indicates failure)
        - Negated commands can also be negated themselves, so `if (--<commandToTest>) <command>` is equivalent to `if (<commandToTest>) <command>`
        - An odd number of `-` operators will negate the command, and an even number of `-` operators will not negate the command

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

# License
alsh is distributed under the terms of the [MIT License](https://github.com/AlanLuu/alsh/blob/main/LICENSE).
