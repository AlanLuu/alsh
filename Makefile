main_program_name=alsh

$(main_program_name): $(main_program_name).c
	cc -Wall -Werror -o $(main_program_name) $(main_program_name).c

debug: $(main_program_name).c
	cc -Wall -Werror -g -o $(main_program_name) $(main_program_name).c

clean:
	rm -f $(main_program_name)