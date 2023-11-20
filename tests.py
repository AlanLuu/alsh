#!/usr/bin/env python3

import json
import shutil
import subprocess
import sys
from pathlib import Path

SHELL_NAME = "alsh"
TESTS_FILE_NAME = "tests.json"

# Functions that wrap input in ANSI color escape codes
class ANSI:
    @staticmethod
    def green(s):
        return f"\033[32m{s}\033[0m"
    
    @staticmethod
    def red(s):
        return f"\033[31m{s}\033[0m"

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def print_wrap(func):
    def wrapper(s):
        print(func(s))
    return wrapper

def eprint_wrap(func):
    def wrapper(s):
        eprint(func(s))
    return wrapper

def main():
    alsh_path = Path(f"./{SHELL_NAME}")
    if not alsh_path.is_file():
        print(f"Note: {SHELL_NAME} binary not found in current directory")
        print("Trying to compile using \"make\"...")

        if shutil.which("make") is None:
            eprint("Error: could not find \"make\" in PATH")
            sys.exit(1)
        
        make_exit_code = subprocess.run("make", shell=True).returncode
        if make_exit_code != 0:
            eprint(f"Error: failed to compile {SHELL_NAME}")
            sys.exit(1)
    
    tests_file = Path(f"./{TESTS_FILE_NAME}")
    try:
        with tests_file.open() as f:
            test_cases: dict = json.load(f)
    except FileNotFoundError:
        eprint(f"Error: could not find {TESTS_FILE_NAME}")
        sys.exit(1)
    
    print_green = print_wrap(ANSI.green)
    print_red = eprint_wrap(ANSI.red)
    tests_passed = 0
    tests_failed = 0
    for key, val in test_cases.items():
        if key:
            print(f'Testing "{key}"')
        else:
            print("Testing empty command")

        expected_output = subprocess.run(
            key,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        ).stdout.decode() if val is None else val

        test_output = subprocess.run(
            f"echo '{key}' | ./{alsh_path}",
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        ).stdout.decode()

        if expected_output == test_output:
            tests_passed += 1
            expected_output = expected_output.replace("\n", "\\n")
            print(f'Expected output: "{expected_output}"')
            print_green("Test case passed")
        else:
            tests_failed += 1
            expected_output = expected_output.replace("\n", "\\n")
            print(f'Expected output: "{expected_output}"')
            print_red("Test case failed")
            test_output = test_output.replace("\n", "\\n")
            print_red(f'Actual output: "{test_output}"')
        
        print()
    
    total_test_cases = len(test_cases)
    if tests_passed == total_test_cases:
        print_green("All test cases passed")
        print(f"Total test cases: {total_test_cases}")
    elif tests_passed == 0:
        print_red("All test cases failed")
        print(f"Total test cases: {total_test_cases}")
    else:
        print_red("Some test cases failed")
        print(f"Total test cases: {total_test_cases}")
        print(f"Total tests passed: {tests_passed}")
        print(f"Total tests failed: {tests_failed}")
        print(f"Percentage of tests passed: {'{:.2f}'.format(tests_passed / total_test_cases * 100)}%")
        print(f"Percentage of tests failed: {'{:.2f}'.format(tests_failed / total_test_cases * 100)}%")

if __name__ == "__main__":
    main()
