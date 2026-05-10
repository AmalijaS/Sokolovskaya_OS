/**
 * Lab 2: Threads creation (Windows).
 * The program creates an array of integers (size and elements from console),
 * runs two threads (min_max and average) that process the array with
 * specified delays, waits for them to finish, replaces all minimum and
 * maximum elements with the average value, and prints the result.
 *
 * Compile: cl /EHsc /Fe:lab2.exe main.cpp
 */

#include <windows.h>
#include <iostream>
#include <vector>
#include <limits>
#include <stdexcept>
#include <memory>

 // Structure to pass data to threads and return results
struct ThreadData {
    const std::vector<int>* arr; // pointer to the shared array (read-only)
    int minVal;
    int maxVal;
    double average;
};

// Thread function for min_max
DWORD WINAPI MinMaxThread(LPVOID lpParam) {
    auto* data = static_cast<ThreadData*>(lpParam);
    const auto& arr = *(data->arr);

    if (arr.empty()) {
        std::cerr << "Error: empty array in min_max thread.\n";
        return 1;
    }

    int min = arr[0];
    int max = arr[0];

    for (size_t i = 1; i < arr.size(); ++i) {
        if (arr[i] < min) {
            min = arr[i];
        }
        if (arr[i] > max) {
            max = arr[i];
        }
        Sleep(7); // sleep after each comparison
    }

    data->minVal = min;
    data->maxVal = max;

    std::cout << "Min element: " << min << "\n";
    std::cout << "Max element: " << max << "\n";

    return 0;
}

// Thread function for average
DWORD WINAPI AverageThread(LPVOID lpParam) {
    auto* data = static_cast<ThreadData*>(lpParam);
    const auto& arr = *(data->arr);

    if (arr.empty()) {
        std::cerr << "Error: empty array in average thread.\n";
        return 1;
    }

    long long sum = 0; // use long long to avoid overflow for large sums
    for (size_t i = 0; i < arr.size(); ++i) {
        sum += arr[i];
        Sleep(12); // sleep after each addition
    }

    data->average = static_cast<double>(sum) / arr.size();

    std::cout << "Average value: " << data->average << "\n";

    return 0;
}

// Custom deleter for HANDLE to use with unique_ptr
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

int main() {
    try {
        // 1. Input array size and elements
        size_t size = 0;
        std::cout << "Enter array size: ";
        std::cin >> size;
        if (size == 0) {
            std::cerr << "Error: array size must be positive.\n";
            return 1;
        }

        std::vector<int> arr(size);
        std::cout << "Enter " << size << " integers:\n";
        for (size_t i = 0; i < size; ++i) {
            if (!(std::cin >> arr[i])) {
                std::cerr << "Error: invalid input for element " << i << ".\n";
                return 1;
            }
        }

        // 2. Prepare data structure for threads
        ThreadData data;
        data.arr = &arr;
        data.minVal = 0;
        data.maxVal = 0;
        data.average = 0.0;

        // 3. Create threads
        UniqueHandle hMinMax(CreateThread(
            nullptr,                // default security attributes
            0,                      // default stack size
            MinMaxThread,           // thread function
            &data,                  // parameter
            0,                      // creation flags (0 = run immediately)
            nullptr                 // thread identifier (not needed)
        ));

        if (!hMinMax) {
            DWORD error = GetLastError();
            std::cerr << "Error: CreateThread for min_max failed, code " << error << "\n";
            return 1;
        }

        UniqueHandle hAverage(CreateThread(
            nullptr,
            0,
            AverageThread,
            &data,
            0,
            nullptr
        ));

        if (!hAverage) {
            DWORD error = GetLastError();
            std::cerr << "Error: CreateThread for average failed, code " << error << "\n";
            return 1;
        }

        // 4. Wait for both threads to finish
        HANDLE handles[2] = { hMinMax.get(), hAverage.get() };
        DWORD waitResult = WaitForMultipleObjects(
            2,          // number of handles
            handles,    // array of handles
            TRUE,       // wait for all
            INFINITE    // wait indefinitely
        );

        if (waitResult == WAIT_FAILED) {
            DWORD error = GetLastError();
            std::cerr << "Error: WaitForMultipleObjects failed, code " << error << "\n";
            return 1;
        }

        // 5. Replace min and max elements with the average value
        // If all elements are equal, min and max are the same, replacement works correctly.
        for (size_t i = 0; i < arr.size(); ++i) {
            if (arr[i] == data.minVal || arr[i] == data.maxVal) {
                arr[i] = static_cast<int>(data.average);
            }
        }

        // 6. Output the modified array
        std::cout << "\nModified array (min and max replaced by average):\n";
        for (size_t i = 0; i < arr.size(); ++i) {
            std::cout << arr[i] << " ";
        }
        std::cout << "\n";

    }
    catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}