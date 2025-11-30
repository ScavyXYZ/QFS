#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <condition_variable>

namespace fs = std::filesystem;

// Global synchronization primitives
std::mutex coutMutex;                    // Protects std::cout
std::mutex resultsMutex;                 // Protects searchResults vector
std::mutex threadsMutex;                 // Protects threads vector
std::condition_variable threadsCV;       // Signals when threads complete

// Thread management
std::atomic<int> activeThreads(0);       // Count of currently running threads
std::atomic<int> maxThreads(std::thread::hardware_concurrency()); // Maximum allowed threads

// Search results and control
std::vector<std::string> searchResults;  // Stores found file paths
std::vector<std::thread> threads;        // Active search threads
std::atomic<bool> printDuringSearch(true); // Controls real-time output

// Forward declarations
void launchSearch(const fs::path& directory, const std::vector<std::string>& filenamePatterns);
void searchInDirectory(const fs::path& directory, const std::vector<std::string>& filenamePatterns);
void printUsage(const char* programName);
bool validateArguments(int argc, char* argv[], std::vector<std::string>& targetPatterns,
    std::string& startingDir, bool& saveToFile, bool& verboseOutput);

/**
 * Splits string by delimiter and returns vector of tokens
 */
std::vector<std::string> splitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);

    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    return tokens;
}

/**
 * Validates command line arguments and extracts parameters
 */
bool validateArguments(int argc, char* argv[], std::vector<std::string>& targetPatterns,
    std::string& startingDir, bool& saveToFile, bool& verboseOutput) {

    if (argc < 2) {
        return false; // Interactive mode
    }

    // Parse target patterns (can be multiple arguments or comma-separated)
    std::string firstArg = argv[1];
    if (firstArg.find(',') != std::string::npos) {
        // Comma-separated patterns in one argument
        targetPatterns = splitString(firstArg, ',');
    }
    else {
        // Single pattern or start of options
        if (firstArg.substr(0, 2) != "--") {
            targetPatterns.push_back(firstArg);
        }
    }

    // Parse options
    for (int i = 2; i < argc; ) {
        std::string arg = argv[i];

        if (arg == "--threads") {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::cerr << "Error: --threads requires a numeric argument\n";
                return false;
            }
            try {
                int num = std::stoi(argv[++i]);
                if (num <= 0 || num > std::thread::hardware_concurrency()) {
                    std::cerr << "Error: Thread count must be between 1 and "
                        << std::thread::hardware_concurrency() << "\n";
                    return false;
                }
                maxThreads = num;
            }
            catch (...) {
                std::cerr << "Error: Invalid number for --threads\n";
                return false;
            }
            i++;
        }
        else if (arg == "--dir") {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::cerr << "Error: --dir requires a directory path argument\n";
                return false;
            }
            startingDir = argv[++i];
            i++;
        }
        else if (arg == "--save") {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::cerr << "Error: --save requires a 0 or 1 argument\n";
                return false;
            }
            std::string val = argv[++i];
            if (val != "0" && val != "1") {
                std::cerr << "Error: --save must be 0 or 1\n";
                return false;
            }
            saveToFile = (val == "1");
            i++;
        }
        else if (arg == "--verbose") {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::cerr << "Error: --verbose requires a 0 or 1 argument\n";
                return false;
            }
            std::string val = argv[++i];
            if (val != "0" && val != "1") {
                std::cerr << "Error: --verbose must be 0 or 1\n";
                return false;
            }
            verboseOutput = (val == "1");
            i++;
        }
        else if (arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        else {
            // Treat as additional filename pattern
            if (arg.substr(0, 2) != "--") {
                targetPatterns.push_back(arg);
            }
            else {
                std::cerr << "Error: Unknown option '" << arg << "'\n";
                printUsage(argv[0]);
                return false;
            }
            i++;
        }
    }

    if (targetPatterns.empty()) {
        std::cerr << "Error: No target filename patterns specified!\n";
        printUsage(argv[0]);
        return false;
    }

    return true;
}

/**
 * Displays program usage information
 */
void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <pattern1> [pattern2] ... [options]\n";
    std::cout << "   or: " << programName << " \"pattern1,pattern2,...\" [options]\n";
    std::cout << "   or: " << programName << " (for interactive mode)\n\n";
    std::cout << "Patterns: File name patterns to search for (case insensitive)\n";
    std::cout << "          Multiple patterns can be specified as separate arguments\n";
    std::cout << "          or comma-separated in one argument\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " \".mp3\" \".exe\" --dir C:\\Users\n";
    std::cout << "  " << programName << " \"target1,.mp3,.exe\" --threads 4\n";
    std::cout << "  " << programName << " document report --save 1\n\n";
    std::cout << "Options:\n";
    std::cout << "  --threads <num>        Number of threads to use (1-"
        << std::thread::hardware_concurrency() << ", default: all cores)\n";
    std::cout << "  --dir <directory>      Starting directory (default: current directory)\n";
    std::cout << "  --save <0|1>           Save results to file (1=yes, 0=no, default: 0)\n";
    std::cout << "  --verbose <0|1>        Print results during search when saving to file (default: 1)\n";
    std::cout << "  --help                 Show this help message\n";
}

/**
 * Converts string to lowercase for case-insensitive comparison
 */
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

/**
 * Checks if filename matches any of the patterns
 */
bool matchesAnyPattern(const std::string& filename, const std::vector<std::string>& patterns) {
    std::string lowerFilename = toLower(filename);

    for (const auto& pattern : patterns) {
        std::string lowerPattern = toLower(pattern);
        if (lowerFilename.find(lowerPattern) != std::string::npos) {
            return true;
        }
    }

    return false;
}

/**
 * Searches for files in a directory and its subdirectories
 */
void searchInDirectory(const fs::path& directory, const std::vector<std::string>& filenamePatterns) {
    try {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            return;
        }

        for (const auto& entry : fs::directory_iterator(directory,
            fs::directory_options::skip_permission_denied)) {
            try {
                if (entry.is_directory()) {
                    launchSearch(entry.path(), filenamePatterns);
                }
                else if (entry.is_regular_file()) {
                    std::string entryFilename = entry.path().filename().string();
                    if (matchesAnyPattern(entryFilename, filenamePatterns)) {
                        std::string absolutePath = fs::absolute(entry.path()).string();
                        std::string result = "Found " + entryFilename + " at: " + absolutePath;

                        // Add to results and optionally print
                        {
                            std::lock_guard<std::mutex> lock(resultsMutex);
                            searchResults.push_back(result);
                        }

                        if (printDuringSearch) {
                            std::lock_guard<std::mutex> coutLock(coutMutex);
                            std::cout << result << std::endl;
                        }
                    }
                }
            }
            catch (...) {
                // Ignore permission errors and continue searching
            }
        }
    }
    catch (...) {
        // Ignore general filesystem errors
    }
}

/**
 * Launches a new search thread or performs search directly if thread limit reached
 */
void launchSearch(const fs::path& directory, const std::vector<std::string>& filenamePatterns) {
    if (activeThreads >= maxThreads) {
        // Execute directly if thread limit reached
        searchInDirectory(directory, filenamePatterns);
        return;
    }

    // Launch new thread for directory search
    std::lock_guard<std::mutex> lock(threadsMutex);
    activeThreads++;

    threads.emplace_back([directory, filenamePatterns]() {
        searchInDirectory(directory, filenamePatterns);
        activeThreads--;
        threadsCV.notify_one();  // Notify main thread about completion
        });
}

/**
 * Gets user input for interactive mode
 */
void getInteractiveInput(std::vector<std::string>& targetPatterns, std::string& startingDir,
    bool& saveToFile, int& threadCount) {
    std::cout << " Quick File Search (QSF)\n\n";

    // Get target patterns
    std::string input;
    while (targetPatterns.empty()) {
        std::cout << "Enter file name patterns to search for (case insensitive)\n";
        std::cout << "You can enter multiple patterns separated by commas: ";
        std::getline(std::cin, input);

        if (input.empty()) {
            std::cout << "At least one pattern is required.\n";
            continue;
        }

        targetPatterns = splitString(input, ',');

        // Remove any leading/trailing whitespace from patterns
        for (auto& pattern : targetPatterns) {
            pattern.erase(0, pattern.find_first_not_of(" \t"));
            pattern.erase(pattern.find_last_not_of(" \t") + 1);
        }

        // Remove empty patterns
        targetPatterns.erase(
            std::remove_if(targetPatterns.begin(), targetPatterns.end(),
                [](const std::string& s) { return s.empty(); }),
            targetPatterns.end()
        );

        if (targetPatterns.empty()) {
            std::cout << "No valid patterns entered.\n";
        }
    }

    std::cout << "\nYour system has " << maxThreads << " logical cores available.\n";

    // Get thread count
    while (true) {
        std::cout << "Enter how many cores to use for search (1-" << maxThreads << "): ";
        std::getline(std::cin, input);

        std::stringstream ss(input);
        if (ss >> threadCount && ss.eof() && threadCount >= 1 && threadCount <= maxThreads) {
            break;
        }
        std::cout << "Invalid number. Please enter a number between 1 and " << maxThreads << ".\n";
    }

    // Get starting directory
    std::cout << "Enter the starting directory (empty for current directory): ";
    std::getline(std::cin, startingDir);

    // Get save preferences
    std::cout << "Save search results to file? (y/n, default: n): ";
    std::getline(std::cin, input);
    saveToFile = (input == "y" || input == "Y");

    if (saveToFile) {
        std::cout << "Print results during search? (y/n, default: y): ";
        std::getline(std::cin, input);
        printDuringSearch = !(input == "N" || input == "n");
    }
}

/**
 * Validates and normalizes the starting directory path
 */
bool setupStartingDirectory(std::string& startingDir) {
    if (startingDir.empty()) {
        startingDir = fs::current_path().string();
        return true;
    }

    try {
        fs::path dirPath(startingDir);
        if (dirPath.is_relative()) {
            dirPath = fs::absolute(dirPath);
        }
        startingDir = dirPath.string();

        if (!fs::exists(startingDir)) {
            std::cerr << "Error: Starting directory does not exist!\n";
            return false;
        }

        if (!fs::is_directory(startingDir)) {
            std::cerr << "Error: Starting path is not a directory!\n";
            return false;
        }

        return true;
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Error: Invalid directory path - " << e.what() << "\n";
        return false;
    }
}

/**
 * Saves search results to file
 */
bool saveResultsToFile() {
    std::ofstream outputFile("founded.txt");
    if (!outputFile.is_open()) {
        std::cerr << "Error: Failed to create results file 'founded.txt'!\n";
        return false;
    }

    for (const auto& result : searchResults) {
        outputFile << result << "\n";
    }
    return true;
}

/**
 * Displays search summary
 */
void displayResults(bool saveToFile, bool interactiveMode) {
    if (searchResults.empty()) {
        std::cout << "Nothing found\n";
        return;
    }

    if (saveToFile) {
        std::cout << "\nSaving...\n";
        if (saveResultsToFile()) {
            std::cout << "\n=================================================\n";
            std::cout << " Search complete! Found " << searchResults.size() << " results\n";
            std::cout << " Results saved to 'founded.txt'\n";
        }
    }
    else {
        std::cout << "\n=================================================\n";
        std::cout << " Search complete! Found " << searchResults.size() << " results\n";
    }

    if (interactiveMode) {
        std::cout << "Press enter to close...";
        std::cin.ignore();
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> targetPatterns;
    std::string startingDir;
    bool saveToFile = false;
    bool verboseOutput = true;
    bool interactiveMode = (argc == 1);

    // Process command line arguments or get interactive input
    if (interactiveMode) {
        int threadCount;
        getInteractiveInput(targetPatterns, startingDir, saveToFile, threadCount);
        maxThreads = threadCount;
    }
    else {
        if (!validateArguments(argc, argv, targetPatterns, startingDir, saveToFile, verboseOutput)) {
            return 1;
        }
        printDuringSearch = verboseOutput;
    }

    // Setup and validate starting directory
    if (!setupStartingDirectory(startingDir)) {
        return 1;
    }

    // Display search configuration
    std::cout << "\nStarting search for patterns: ";
    for (size_t i = 0; i < targetPatterns.size(); ++i) {
        std::cout << "'" << targetPatterns[i] << "'";
        if (i < targetPatterns.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << " using " << maxThreads << " threads...\n";
    std::cout << "Starting from directory: " << startingDir << "\n";
    if (saveToFile) {
        std::cout << "Results will be saved to 'founded.txt'\n";
        std::cout << "Results will be " << (printDuringSearch ? "" : "NOT ")
            << "printed during search\n";
    }
    std::cout << "Search in progress... Please wait.\n";

    // Begin search
    searchInDirectory(startingDir, targetPatterns);

    // Wait for all threads to complete
    {
        std::unique_lock<std::mutex> lock(threadsMutex);
        threadsCV.wait(lock, [] { return activeThreads == 0; });
    }

    // Clean up threads
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads.clear();

    // Sort and display results
    std::sort(searchResults.begin(), searchResults.end());
    displayResults(saveToFile, interactiveMode);

    return 0;
}
