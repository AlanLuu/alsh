# alsh
A custom UNIX shell written in C

# Features
- Execute commands (e.g. `ls`)
- Execute commands with arguments and flags (e.g. `ls -la /`)
- Execute commands with redirection `<` `>` `>>`
- Execute commands with pipes `|`
- Execute multiple commands separated by `;` on the same line
- View command history with `history` and execute previous commands with `!n`, where `n` is the command number in the history list (e.g. `!3` will execute the third command in the history list)
    - To execute the previous command, use `!!`
    - To execute the command `n` lines back in the history list, use `!-n` (e.g. `!-2` will execute the command 2 lines back in the history list)
    - To clear the history list, use `history -c`

# Build
```
git clone https://github.com/AlanLuu/alsh.git
cd alsh
make
```
After building, to run the shell:
```
./alsh
```