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

std::mutex coutMutex;
std::mutex resultsMutex;
std::mutex threadsMutex;
std::condition_variable threadsCV;
std::atomic<int> activeThreads(0);
std::atomic<int> maxThreads(std::thread::hardware_concurrency());

std::vector<std::string> searchResults;
std::vector<std::thread> threads;
std::atomic<bool> printDuringSearch(true);

void launchSearch(const fs::path& directory, const std::string& filename);
void searchInDirectory(const fs::path& directory, const std::string& filename);
void printUsage(const char* programName);
bool validateArguments(int argc, char* argv[], std::string& targetFilename, std::string& startingDir, bool& saveToFile, bool& verboseOutput);

bool validateArguments(int argc, char* argv[], std::string& targetFilename, std::string& startingDir, bool& saveToFile, bool& verboseOutput) {
    for (int i = 1; i < argc; ) {
        std::string arg = argv[i];

        if (arg == "--target") {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::cerr << "Error: --target requires a filename argument\n";
                return false;
            }
            targetFilename = argv[++i];
            i++;
        }
        else if (arg == "--threads") {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::cerr << "Error: --threads requires a numeric argument\n";
                return false;
            }
            try {
                int num = std::stoi(argv[++i]);
                if (num <= 0 || num > std::thread::hardware_concurrency()) {
                    std::cerr << "Error: Thread count must be between 1 and " << std::thread::hardware_concurrency() << "\n";
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
            std::string dirArg = argv[++i];
            if (dirArg == "here") {
                startingDir = fs::current_path().string();
            }
            else {
                startingDir = dirArg;
            }
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
            printDuringSearch = (val == "1");
            i++;
        }
        else if (arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            printUsage(argv[0]);
            return false;
        }
    }

    if (targetFilename.empty()) {
        std::cerr << "Error: Target filename not specified!\n";
        printUsage(argv[0]);
        return false;
    }

    return true;
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " --target <filename> [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --target <filename>    File to search for (required)\n";
    std::cout << "  --threads <num>        Number of threads to use (1-" << std::thread::hardware_concurrency() << ", default: all cores)\n";
    std::cout << "  --dir <directory>      Starting directory (default: system root)\n";
    std::cout << "           or 'here'     Search in current working directory\n";
    std::cout << "  --save <0|1>           Save results to file (1=yes, 0=no, default: 0)\n";
    std::cout << "  --verbose <0|1>        Print results during search when saving to file (default: 1)\n";
    std::cout << "  --help                 Show this help message\n";
}

void searchInDirectory(const fs::path& directory, const std::string& filename) {
    try {
        if (!fs::exists(directory) || !fs::is_directory(directory))
            return;

        auto toLower = [](std::string str) {
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
            return str;
            };

        for (const auto& entry : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied)) {
            try {
                if (entry.is_directory()) {
                    launchSearch(entry.path(), filename);
                }
                else if (entry.is_regular_file() &&
                    (toLower(entry.path().filename().string()).find(toLower(filename)) != std::string::npos)) {
                    std::string result = "Found " + entry.path().filename().string() + " at: " + entry.path().string();

                    std::lock_guard<std::mutex> lock(resultsMutex);
                    searchResults.push_back(result);

                    if (printDuringSearch) {
                        std::lock_guard<std::mutex> coutLock(coutMutex);
                        std::cout << result << std::endl;
                    }
                }
            }
            catch (...) {
                // Ignore permission errors
            }
        }
    }
    catch (...) {
        // Ignore general errors
    }
}

void launchSearch(const fs::path& directory, const std::string& filename) {
    if (activeThreads >= maxThreads) {
        searchInDirectory(directory, filename);
        return;
    }

    std::lock_guard<std::mutex> lock(threadsMutex);
    activeThreads++;
    threads.emplace_back([directory, filename]() {
        searchInDirectory(directory, filename);
        activeThreads--;
        threadsCV.notify_one();
        });
}

int main(int argc, char* argv[]) {
    std::string targetFilename;
    std::string startingDir;
    bool saveToFile = false;
    bool verboseOutput = true;

    // Processing command line arguments
    if (argc == 1) {
        std::cout << " Quick File Search (QSF)\n\n";

        // Requesting a target file
        while (true) {
            std::cout << "Enter the file name to search for (case insensitive): ";
            std::getline(std::cin, targetFilename);

            if (!targetFilename.empty()) {
                break;
            }
            std::cout << "File name cannot be empty.\n";
        }
        std::cout << "\nYour system has " << maxThreads << " logical cores available.\n";

        // Requesting the number of threads
        std::string input;
        int number;

        while (true) {
            std::cout << "Enter how many cores to use for search (1-" << maxThreads << "): ";
            std::getline(std::cin, input);

            std::stringstream ss(input);
            if (ss >> number && ss.eof()) {
                if (number >= 1 && number <= maxThreads)
                    break;
                std::cout << "Invalid number\n";
            }
            else {
                std::cout << "Isn`t number.\n";
            }
        }
        maxThreads = number;

        // Requesting a startup directory
        std::cout << "Enter the starting directory (Default - root): ";
        std::getline(std::cin, startingDir);

        if (startingDir == "here") {
            startingDir = fs::current_path().string();
        }
        else if (startingDir.empty()) {
#if defined(_WIN32) || defined(_WIN64)
            startingDir = "C:\\";
#else
            startingDir = "/";
#endif
        }

        // Request to save to file
        std::cout << "Save search results to file? (y/n, default: n): ";
        std::getline(std::cin, input);
        if (input == "y" || input == "Y") {
            saveToFile = true;

            // Request to display results during a search
            std::cout << "Print results during search? (y/n, default: y): ";
            std::getline(std::cin, input);
            if (input == "N" || input == "n") {
                printDuringSearch = false;
            }
        }
    }
    else {
        if (!validateArguments(argc, argv, targetFilename, startingDir, saveToFile, verboseOutput)) {
            return 1;
        }
        printDuringSearch = verboseOutput;
    }

    // Setting default values if not specified
    if (startingDir.empty()) {
#if defined(_WIN32) || defined(_WIN64)
        startingDir = "C:\\";
#else
        startingDir = "/";
#endif
    }

    if (!fs::exists(startingDir)) {
        std::cerr << "Error: Starting directory does not exist!\n";
        return 1;
    }

    if (!fs::is_directory(startingDir)) {
        std::cerr << "Error: Starting path is not a directory!\n";
        return 1;
    }

    std::cout << "\nStarting search for '" << targetFilename << "' using " << maxThreads << " threads...\n";
    std::cout << "Starting from directory: " << startingDir << "\n";
    if (saveToFile) {
        std::cout << "Results will be saved to 'founded.txt'\n";
        if (printDuringSearch) {
            std::cout << "Results will be printed during search\n";
        }
        else {
            std::cout << "Results will NOT be printed during search\n";
        }
    }
    std::cout << "Search in progress... Please wait.\n";

    searchInDirectory(startingDir, targetFilename);

    {
        std::unique_lock<std::mutex> lock(threadsMutex);
        threadsCV.wait(lock, [] { return activeThreads == 0; });
    }

    // Cleaning of threads
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads.clear();

    std::sort(searchResults.begin(), searchResults.end());

    if (!searchResults.empty()) {
        if (saveToFile) {
            std::cout << "\nSaving...\n";
            std::ofstream outputFile("founded.txt");
            if (outputFile.is_open()) {
                for (const auto& result : searchResults) {
                    outputFile << result << "\n";
                }
                outputFile.close();
                std::cout << "\n=================================================\n";
                std::cout << " Search complete! Results saved to 'founded.txt'\n";
                std::cout << " Found " << searchResults.size() << " results\n";
                std::cout << "=================================================\n";
            }
            else {
                std::cerr << "Error: Failed to create results file 'founded.txt'!\n";
            }
        }
        else {
            std::cout << "\n=================================================\n";
            std::cout << " Search complete! Found " << searchResults.size() << " results\n";
            std::cout << "=================================================\n";
        }
    }
    else {
        std::cout << "Nothing found\n";
    }
    std::cout << "Press enter to close...";
    std::cin.ignore();
    return 0;

}
