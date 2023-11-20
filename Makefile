install_path=/usr/local/bin
main_program_name=alsh
test_script_name=tests.py
flags=-Wall -Wextra -pedantic-errors -Wshadow -Wformat=2 -Wconversion -Wunused-parameter -O2

$(main_program_name): $(main_program_name).c utils/*
	cc $(flags) -o $(main_program_name) $(main_program_name).c utils/*.c

install: $(main_program_name)
	strip $(main_program_name)
	install $(main_program_name) $(install_path)

debug: $(main_program_name).c utils/*
	cc -g -o $(main_program_name) $(main_program_name).c utils/*.c

run: $(main_program_name)
	./$(main_program_name)

clean:
	rm -f $(main_program_name)

uninstall:
	rm -f $(install_path)/$(main_program_name)

test:
	./$(test_script_name)
