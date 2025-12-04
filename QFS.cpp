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
#include <regex>

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

// Search modes and regex flag
enum class SearchMode {
    OR,     // Match any pattern (default)
    AND,    // Match all patterns
    SINGLE  // Single pattern
};

enum class PatternType {
    SIMPLE, // Simple substring search (case-insensitive)
    REGEX   // Regular expression
};

// Forward declarations
void launchSearch(const fs::path& directory, const std::vector<std::string>& filenamePatterns,
    SearchMode mode, PatternType patternType);
void searchInDirectory(const fs::path& directory, const std::vector<std::string>& filenamePatterns,
    SearchMode mode, PatternType patternType);
void printUsage(const char* programName);
bool validateArguments(int argc, char* argv[], std::vector<std::string>& targetPatterns,
    std::string& startingDir, bool& saveToFile, bool& verboseOutput,
    SearchMode& searchMode, PatternType& patternType);
bool parseSearchPatterns(const std::string& input, std::vector<std::string>& patterns,
    SearchMode& mode, PatternType& patternType);
bool matchesPatterns(const std::string& filename, const std::vector<std::string>& patterns,
    SearchMode mode, PatternType patternType);

/**
 * Splits string by delimiter and returns vector of tokens
 */
std::vector<std::string> splitString(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);

    while (end != std::string::npos) {
        std::string token = str.substr(start, end - start);
        if (!token.empty()) {
            tokens.push_back(token);
        }
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }

    // Add the last token
    std::string lastToken = str.substr(start);
    if (!lastToken.empty()) {
        tokens.push_back(lastToken);
    }

    return tokens;
}

/**
 * Parses search patterns and determines search mode and pattern type
 */
bool parseSearchPatterns(const std::string& input, std::vector<std::string>& patterns,
    SearchMode& mode, PatternType& patternType) {
    std::string trimmedInput = input;

    // Remove leading/trailing whitespace
    trimmedInput.erase(0, trimmedInput.find_first_not_of(" \t"));
    trimmedInput.erase(trimmedInput.find_last_not_of(" \t") + 1);

    if (trimmedInput.empty()) {
        return false;
    }

    // Check if pattern is regex (starts and ends with /)
    if (trimmedInput.size() >= 2 && trimmedInput[0] == '/' && trimmedInput.back() == '/') {
        patternType = PatternType::REGEX;
        // Remove the slashes
        trimmedInput = trimmedInput.substr(1, trimmedInput.size() - 2);

        // Check for logical operators in regex mode
        size_t andPos = trimmedInput.find("&&");
        size_t orPos = trimmedInput.find("||");

        if (andPos != std::string::npos && orPos != std::string::npos) {
            std::cerr << "Error: Cannot use both && and || in regex pattern\n";
            return false;
        }

        if (andPos != std::string::npos) {
            mode = SearchMode::AND;
            patterns = splitString(trimmedInput, "&&");
        }
        else if (orPos != std::string::npos) {
            mode = SearchMode::OR;
            patterns = splitString(trimmedInput, "||");
        }
        else {
            mode = SearchMode::SINGLE;
            patterns.push_back(trimmedInput);
        }
    }
    else {
        // Simple pattern mode
        patternType = PatternType::SIMPLE;

        // Check for logical operators
        size_t andPos = trimmedInput.find("&&");
        size_t orPos = trimmedInput.find("||");

        if (andPos != std::string::npos && orPos != std::string::npos) {
            std::cerr << "Error: Cannot use both && and || in the same search pattern\n";
            return false;
        }

        if (andPos != std::string::npos) {
            mode = SearchMode::AND;
            patterns = splitString(trimmedInput, "&&");
        }
        else if (orPos != std::string::npos) {
            mode = SearchMode::OR;
            patterns = splitString(trimmedInput, "||");
        }
        else {
            mode = SearchMode::SINGLE;
            patterns.push_back(trimmedInput);
        }
    }

    // Remove whitespace from each pattern
    for (auto& pattern : patterns) {
        pattern.erase(0, pattern.find_first_not_of(" \t"));
        pattern.erase(pattern.find_last_not_of(" \t") + 1);
    }

    // Remove empty patterns
    patterns.erase(std::remove_if(patterns.begin(), patterns.end(),
        [](const std::string& s) { return s.empty(); }), patterns.end());

    if (patterns.empty()) {
        return false;
    }

    return true;
}

/**
 * Validates command line arguments and extracts parameters
 */
bool validateArguments(int argc, char* argv[], std::vector<std::string>& targetPatterns,
    std::string& startingDir, bool& saveToFile, bool& verboseOutput,
    SearchMode& searchMode, PatternType& patternType) {

    if (argc < 2) {
        return false; // Interactive mode
    }

    // Parse target patterns with logical operators
    std::string firstArg = argv[1];
    if (!parseSearchPatterns(firstArg, targetPatterns, searchMode, patternType)) {
        std::cerr << "Error: Invalid search pattern\n";
        return false;
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
            std::cerr << "Error: Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return false;
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
    std::cout << "Usage: " << programName << " <pattern> [options]\n";
    std::cout << "   or: " << programName << " (for interactive mode)\n\n";
    std::cout << "Patterns can include:\n";
    std::cout << "  Simple patterns:            hello&&.exe (case-insensitive substring search)\n";
    std::cout << "  Regular expressions (regex): /hello.*\\.exe/ (wrap regex in /.../)\n";
    std::cout << "Important: In regex patterns, escape special characters properly:\n";
    std::cout << "  - \\. for literal dot (not any character)\n";
    std::cout << "  - \\\\ for literal backslash\n";
    std::cout << "  - Use double backslashes in command line: /.*\\.txt/\n";
    std::cout << "Logical operators:\n";
    std::cout << "  pattern1&&pattern2    Find files matching ALL patterns (AND)\n";
    std::cout << "  pattern1||pattern2    Find files matching ANY pattern (OR)\n";
    std::cout << "  pattern               Find files matching single pattern\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " \"hello&&.exe\"          Find files with 'hello' AND '.exe' in name\n";
    std::cout << "  " << programName << " \"hello||.exe\"          Find files with 'hello' OR '.exe' in name\n";
    std::cout << "  " << programName << " \"/.*\\.(txt|md)/\"      Find all .txt and .md files (regex)\n";
    std::cout << "  " << programName << " \"/XYZ_.+\\.bin/\"      Find files starting with XYZ_ and ending with .bin\n";
    std::cout << "  " << programName << " \"/test[0-9]+\\.exe/\"  Find files like test1.exe, test42.exe (regex)\n";
    std::cout << "  " << programName << " \"document\" --dir C:\\Users\n\n";
    std::cout << "Options:\n";
    std::cout << "  --threads <num>        Number of threads to use (1-"
        << std::thread::hardware_concurrency() << ", default: all cores)\n";
    std::cout << "  --dir <directory>      Starting directory (default: current directory)\n";
    std::cout << "  --save <0|1>           Save results to file (1=yes, 0=no, default: 0)\n";
    std::cout << "  --verbose <0|1>        Print results during search when saving to file (default: 1)\n";
    std::cout << "  --help                 Show this help message\n";
}

/**
 * Converts string to lowercase for case-insensitive comparison (simple mode)
 */
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

/**
 * Checks if filename matches patterns based on search mode and pattern type
 */
bool matchesPatterns(const std::string& filename, const std::vector<std::string>& patterns,
    SearchMode mode, PatternType patternType) {

    if (patternType == PatternType::SIMPLE) {
        // Simple substring search (case-insensitive)
        std::string lowerFilename = toLower(filename);

        if (mode == SearchMode::OR) {
            // Match ANY pattern (OR logic)
            for (const auto& pattern : patterns) {
                std::string lowerPattern = toLower(pattern);
                if (lowerFilename.find(lowerPattern) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }
        else if (mode == SearchMode::AND) {
            // Match ALL patterns (AND logic)
            for (const auto& pattern : patterns) {
                std::string lowerPattern = toLower(pattern);
                if (lowerFilename.find(lowerPattern) == std::string::npos) {
                    return false;
                }
            }
            return true;
        }
        else {
            // SINGLE mode - match the only pattern
            std::string lowerPattern = toLower(patterns[0]);
            return lowerFilename.find(lowerPattern) != std::string::npos;
        }
    }
    else {
        // REGEX mode
        if (mode == SearchMode::OR) {
            // Match ANY pattern (OR logic)
            for (const auto& pattern : patterns) {
                try {
                    std::regex re(pattern, std::regex::icase | std::regex::ECMAScript); // Case-insensitive
                    if (std::regex_match(filename, re)) {
                        return true;
                    }
                }
                catch (const std::regex_error& e) {
                    std::lock_guard<std::mutex> coutLock(coutMutex);
                    std::cerr << "Regex error for pattern '" << pattern << "': " << e.what() << "\n";
                    return false;
                }
            }
            return false;
        }
        else if (mode == SearchMode::AND) {
            // Match ALL patterns (AND logic)
            for (const auto& pattern : patterns) {
                try {
                    std::regex re(pattern, std::regex::icase | std::regex::ECMAScript); // Case-insensitive
                    if (!std::regex_match(filename, re)) {
                        return false;
                    }
                }
                catch (const std::regex_error& e) {
                    std::lock_guard<std::mutex> coutLock(coutMutex);
                    std::cerr << "Regex error for pattern '" << pattern << "': " << e.what() << "\n";
                    return false;
                }
            }
            return true;
        }
        else {
            // SINGLE mode - match the only pattern
            try {
                std::regex re(patterns[0], std::regex::icase | std::regex::ECMAScript); // Case-insensitive
                return std::regex_match(filename, re);
            }
            catch (const std::regex_error& e) {
                std::lock_guard<std::mutex> coutLock(coutMutex);
                std::cerr << "Regex error for pattern '" << patterns[0] << "': " << e.what() << "\n";
                return false;
            }
        }
    }
}

/**
 * Searches for files in a directory and its subdirectories
 */
void searchInDirectory(const fs::path& directory, const std::vector<std::string>& filenamePatterns,
    SearchMode mode, PatternType patternType) {
    try {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            return;
        }

        for (const auto& entry : fs::directory_iterator(directory,
            fs::directory_options::skip_permission_denied)) {
            try {
                if (entry.is_directory()) {
                    launchSearch(entry.path(), filenamePatterns, mode, patternType);
                }
                else if (entry.is_regular_file()) {
                    std::string entryFilename = entry.path().filename().string();
                    if (matchesPatterns(entryFilename, filenamePatterns, mode, patternType)) {
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
void launchSearch(const fs::path& directory, const std::vector<std::string>& filenamePatterns,
    SearchMode mode, PatternType patternType) {
    if (activeThreads >= maxThreads) {
        // Execute directly if thread limit reached
        searchInDirectory(directory, filenamePatterns, mode, patternType);
        return;
    }

    // Launch new thread for directory search
    std::lock_guard<std::mutex> lock(threadsMutex);
    activeThreads++;

    threads.emplace_back([directory, filenamePatterns, mode, patternType]() {
        searchInDirectory(directory, filenamePatterns, mode, patternType);
        activeThreads--;
        threadsCV.notify_one();  // Notify main thread about completion
        });
}

/**
 * Gets user input for interactive mode
 */
void getInteractiveInput(std::vector<std::string>& targetPatterns, std::string& startingDir,
    bool& saveToFile, int& threadCount, SearchMode& searchMode, PatternType& patternType) {
    std::cout << " Quick File Search (QSF)\n\n";

    // Get target patterns
    std::string input;
    while (targetPatterns.empty()) {
        std::cout << "Enter file name patterns to search for:\n";
        std::cout << "  Simple patterns: 'hello&&.txt' (case-insensitive substring)\n";
        std::cout << "  Regex patterns: '/.*\\.(txt|pdf)/' (wrap regex in /.../)\n";
        std::cout << "Note: In regex, use \\. for literal dot, \\\\ for backslash\n";
        std::cout << "Logical operators:\n";
        std::cout << "  - pattern1&&pattern2  (AND - match ALL patterns)\n";
        std::cout << "  - pattern1||pattern2  (OR - match ANY pattern)\n";
        std::cout << "  - pattern             (single pattern)\n";
        std::cout << "Enter patterns: ";
        std::getline(std::cin, input);

        if (!parseSearchPatterns(input, targetPatterns, searchMode, patternType)) {
            std::cout << "Invalid input. Please try again.\n";
        }
    }

    // Display search mode
    std::string modeStr;
    if (searchMode == SearchMode::AND) {
        modeStr = "AND (match all patterns)";
    }
    else if (searchMode == SearchMode::OR) {
        modeStr = "OR (match any pattern)";
    }
    else {
        modeStr = "SINGLE (match one pattern)";
    }

    std::string typeStr = (patternType == PatternType::REGEX) ? "REGEX" : "SIMPLE";

    std::cout << "\nSearch mode: " << modeStr << "\n";
    std::cout << "Pattern type: " << typeStr << "\n";
    std::cout << "Your system has " << maxThreads << " logical cores available.\n";

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
    SearchMode searchMode = SearchMode::OR;
    PatternType patternType = PatternType::SIMPLE;

    // Process command line arguments or get interactive input
    if (interactiveMode) {
        int threadCount;
        getInteractiveInput(targetPatterns, startingDir, saveToFile, threadCount, searchMode, patternType);
        maxThreads = threadCount;
    }
    else {
        if (!validateArguments(argc, argv, targetPatterns, startingDir, saveToFile, verboseOutput, searchMode, patternType)) {
            return 1;
        }
        printDuringSearch = verboseOutput;
    }

    // Setup and validate starting directory
    if (!setupStartingDirectory(startingDir)) {
        return 1;
    }

    std::cout << "Pattern type: " << (patternType == PatternType::REGEX ? "REGEX" : "SIMPLE (case-insensitive)") << "\n";
    std::cout << "Patterns: ";
    for (size_t i = 0; i < targetPatterns.size(); ++i) {
        if (patternType == PatternType::REGEX) {
            std::cout << "'/" << targetPatterns[i] << "/'";
        }
        else {
            std::cout << "'" << targetPatterns[i] << "'";
        }
        if (i < targetPatterns.size() - 1) {
            if (searchMode == SearchMode::AND) {
                std::cout << " && ";
            }
            else if (searchMode == SearchMode::OR) {
                std::cout << " || ";
            }
        }
    }
    std::cout << "\nUsing " << maxThreads << " threads...\n";
    std::cout << "Starting from directory: " << startingDir << "\n";
    if (saveToFile) {
        std::cout << "Results will be saved to 'founded.txt'\n";
        std::cout << "Results will be " << (printDuringSearch ? "" : "NOT ")
            << "printed during search\n";
    }
    std::cout << "Search in progress... Please wait.\n";

    // Begin search
    searchInDirectory(startingDir, targetPatterns, searchMode, patternType);

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
