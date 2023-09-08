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
