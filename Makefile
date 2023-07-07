install_path=/usr/local/bin
main_program_name=alsh
flags=-Wall -Wextra -pedantic-errors -Wshadow -Wformat=2 -Wconversion -Wunused-parameter

$(main_program_name): $(main_program_name).c
	cc $(flags) -o $(main_program_name) $(main_program_name).c

install: $(main_program_name)
	strip $(main_program_name)
	install $(main_program_name) $(install_path)

debug: $(main_program_name).c
	cc -g -o $(main_program_name) $(main_program_name).c

run: $(main_program_name)
	./$(main_program_name)

clean:
	rm -f $(main_program_name)
