# Quick File Search (QFS)
QFS is a multi-threaded command-line utility for searching files by name on your system. It supports case-insensitive search, custom thread counts, and saving results to a file.  
## Features
- **Multi-threaded search**: Uses all available CPU cores by default.  
- **Case-insensitive search**: Finds files regardless of letter case.  
- **Customizable**: Set the number of threads, starting directory, and output options.  
- **Interactive and command-line modes**: Run with arguments or answer prompts.  
- **Save results**: Optionally save found files to a text file.
## Usage
### 1. Interactive Mode
Run the program without arguments to enter interactive mode:
```Bash
./QFS
```
You will be prompted for:
- The file name to search for
- The number of threads to use
- The starting directory (default: system root)
- Whether to save results to a file
- Whether to print results during the search
### 2. Command-Line Mode
Run the program with arguments for non-interactive use:
```Bash
./qfs <filename> [options]
```
#### Options

| Option | Description |
|-------------|-------------|
| --target   | File name to search for (required) |
| --threads   | Number of threads to use (default: all available cores)   |
| --dir | Starting directory (default: system root). Use here for current directory |
| --save | Save results to file (1=yes, 0=no, default: 0) |
| --verbose | Print results during search when saving to file (1=yes, 0=no, default: 1) |
| --help |Show help message|

#### Example
```Bash
./qfs myfile.txt --threads 4 --dir /home/user --save 1 --verbose 1
```
## Build
### Requirements
- C++ 17 or later
- Standard Library with <filesystem> support
### Compile
```Bash
git clone https://github.com/ScavyXYZ/QFS.git
cd QFS
mkdir build
cd build
cmake ..
cmake --build . --config release
```
## Notes
- On Windows, the default starting directory is "C:\"
- On Linux/macOS, the default starting directory is "/"
- Results are saved to founded.txt if --save 1 is used
- The program skips directories with permission errors.
