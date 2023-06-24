# alsh
A custom UNIX shell written in C

# Features
- Execute commands (e.g. `ls`)
- Execute commands with arguments and flags (e.g. `ls -la /`)
- Execute commands with redirection `<` `>` `>>`
- Execute commands with pipes `|`
- Execute multiple commands separated by `;` on the same line
- View command history with `history` and execute previous commands with `!n`, where `n` is the command number in the history list (e.g. `!3` will execute the third command in the history list)

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