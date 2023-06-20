main_program_name=alsh
flags=-Wall -Wextra -pedantic-errors -Wshadow -Wformat=2 -Wconversion -Wunused-parameter -fsanitize=address,undefined

$(main_program_name): $(main_program_name).c
	cc $(flags) -o $(main_program_name) $(main_program_name).c

debug: $(main_program_name).c
	cc $(flags) -g -o $(main_program_name) $(main_program_name).c
	gdb $(main_program_name)

run: $(main_program_name)
	./$(main_program_name)

clean:
	rm -f $(main_program_name)