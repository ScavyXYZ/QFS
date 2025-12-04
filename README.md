# Quick File Search (QFS)

QFS is a multi-threaded command-line utility for searching files by name on your system. It supports case-insensitive search, logical operators (AND/OR), regular expressions, custom thread counts, and saving results to a file.

## Features

- **Multi-threaded search**: Uses all available CPU cores by default
- **Case-insensitive search**: Finds files regardless of letter case
- **Logical operators**: Combine multiple patterns with AND (&&) or OR (||) logic
- **Regular expression support**: Use regex patterns for advanced matching
- **Customizable**: Set the number of threads, starting directory, and output options
- **Interactive and command-line modes**: Run with arguments or answer prompts
- **Save results**: Optionally save found files to a text file

## Usage

### 1. Interactive Mode

Run the program without arguments to enter interactive mode:

```bash
./QFS
```

You will be prompted for:
- File name patterns (simple or regex with logical operators)
- The number of threads to use
- The starting directory (default: current directory)
- Whether to save results to a file
- Whether to print results during the search

### 2. Command-Line Mode

Run the program with arguments for non-interactive use:

```bash
./qfs <pattern> [options]
```

#### Pattern Syntax

**Simple Patterns** (case-insensitive substring search):
- Single pattern: `document`
- AND logic: `hello&&.txt` (files must contain BOTH "hello" AND ".txt")
- OR logic: `hello||.txt` (files must contain EITHER "hello" OR ".txt")

**Regular Expression Patterns** (wrap in `/...'/):
- Single regex: `/.*\.txt/` (all .txt files)
- AND logic: `/test.*&&.+\.exe/` (files matching BOTH patterns)
- OR logic: `/.*\.txt||.*\.md/` (files matching EITHER pattern)

**Important**: In regex patterns, escape special characters properly:
- Use `\.` for literal dot (not any character)
- Use `\\` for literal backslash
- Double backslashes may be needed in command line: `/.*\.txt/`

#### Options

| Option | Description |
|--------|-------------|
| `--threads <num>` | Number of threads to use (1 to max cores, default: all available cores) |
| `--dir <path>` | Starting directory (default: current directory) |
| `--save <0\|1>` | Save results to file (1=yes, 0=no, default: 0) |
| `--verbose <0\|1>` | Print results during search when saving to file (1=yes, 0=no, default: 1) |
| `--help` | Show help message |

#### Examples

**Simple pattern searches:**
```bash
# Find files containing "document"
./qfs "document"

# Find files with both "hello" AND ".exe" in name
./qfs "hello&&.exe"

# Find files with either "hello" OR ".exe" in name
./qfs "hello||.exe"

# Search in specific directory with 4 threads
./qfs "myfile.txt" --threads 4 --dir /home/user
```

**Regular expression searches:**
```bash
# Find all .txt and .md files
./qfs "/.*\.(txt|md)/"

# Find files starting with "XYZ_" and ending with ".bin"
./qfs "/XYZ_.+\.bin/"

# Find files like test1.exe, test42.exe
./qfs "/test[0-9]+\.exe/"

# Find files matching both regex patterns
./qfs "/^[A-Z]&&.*\.log/" --dir /var/log
```

**With options:**
```bash
# Save results to file without printing during search
./qfs "document" --save 1 --verbose 0

# Use 8 threads, search from C:\Users, save results
./qfs "hello&&world" --threads 8 --dir "C:\Users" --save 1
```

## Build

### Requirements

- C++17 or later
- Standard Library with `<filesystem>` support
- CMake 3.8 or later

### Compile

```bash
git clone https://github.com/ScavyXYZ/QFS.git
cd QFS
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Output

- Results are displayed in real-time (unless `--verbose 0` is used with `--save 1`)
- When `--save 1` is used, results are saved to `founded.txt` in the current directory
- Each result shows: `Found <filename> at: <absolute_path>`
- Results are sorted alphabetically at the end of the search

## Notes

- The default starting directory is the current working directory
- The program skips directories with permission errors
- Regex patterns use ECMAScript grammar with case-insensitive matching
- Cannot mix AND (`&&`) and OR (`||`) operators in the same pattern
- Pattern matching is always case-insensitive (both simple and regex modes)
- In interactive mode, press Enter to close after viewing results

## Pattern Matching Modes

### Simple Mode (Default)
- Case-insensitive substring matching
- Example: `hello` matches "Hello.txt", "HELLO_WORLD.doc", "say_hello.pdf"

### Regex Mode (Patterns wrapped in `/...'/  )
- Full regular expression support
- Case-insensitive by default
- Example: `/^test[0-9]+\.exe$/` matches "test1.exe", "test123.exe" but not "mytest1.exe"

### Logical Operators

#### AND (`&&`) - Match ALL patterns
Files must match every pattern in the list.
- Simple: `hello&&world` matches "hello_world.txt", "world_says_hello.doc"
- Regex: `/^test&&.*\.exe/` matches files starting with "test" that end with ".exe"

#### OR (`||`) - Match ANY pattern
Files must match at least one pattern in the list.
- Simple: `hello||world` matches "hello.txt", "world.doc", "hello_world.pdf"
- Regex: `/.*\.txt||.*\.md/` matches any .txt or .md file
## Author

ScavyXYZ
