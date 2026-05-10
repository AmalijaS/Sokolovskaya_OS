/**
 * Lab 3: Synchronization of threads using critical sections and events.
 * The program creates an array of integers (size entered from console),
 * initializes it with zeros, starts several marker threads, and coordinates
 * them using events and a critical section. The main thread manages the
 * markers, requesting termination of a selected marker when all are blocked.
 *
 * Compile: cl /EHsc /Fe:lab3.exe lab3.cpp
 */

#include <windows.h>
#include <iostream>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>

 //---------------------------------------------------------------------
 // RAII wrappers
 //---------------------------------------------------------------------

 // Custom deleter for HANDLE to use with unique_ptr
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

// RAII wrapper for CRITICAL_SECTION
class CriticalSectionGuard {
public:
    CriticalSectionGuard(CRITICAL_SECTION& cs) : m_cs(cs) {
        EnterCriticalSection(&m_cs);
    }
    ~CriticalSectionGuard() {
        LeaveCriticalSection(&m_cs);
    }
    // Non-copyable, non-movable
    CriticalSectionGuard(const CriticalSectionGuard&) = delete;
    CriticalSectionGuard& operator=(const CriticalSectionGuard&) = delete;
private:
    CRITICAL_SECTION& m_cs;
};

//---------------------------------------------------------------------
// Constants
//---------------------------------------------------------------------
const DWORD MARKER_SLEEP_MS = 5;   // sleep time inside marker as per task

//---------------------------------------------------------------------
// Data structure passed to each marker thread
//---------------------------------------------------------------------
struct MarkerData {
    int id;                        // order number of the marker
    int* array;                    // pointer to shared array
    int arraySize;                 // size of the array
    CRITICAL_SECTION* cs;          // pointer to critical section

    // Events
    HANDLE startEvent;             // signal to start work (manual-reset)
    HANDLE continueEvent;          // signal to continue (manual-reset)
    HANDLE stopEvent;              // signal to stop this specific marker (manual-reset)
    HANDLE cannotContinueEvent;    // signal to main that marker cannot proceed (manual-reset)

    std::vector<int> markedIndices; // indices that this marker has set to its id
};

//---------------------------------------------------------------------
// Marker thread function
//---------------------------------------------------------------------
DWORD WINAPI MarkerThread(LPVOID lpParam) {
    // The marker takes ownership of the MarkerData (allocated by main)
    std::unique_ptr<MarkerData> data(static_cast<MarkerData*>(lpParam));
    if (!data) return 1;

    // 1. Wait for the start signal from main
    WaitForSingleObject(data->startEvent, INFINITE);

    // 2. Seed the random generator with the marker's id (task requirement)
    srand(data->id);

    // 3. Main work loop
    while (true) {
        // 3.1 Generate random number and compute index
        int index = rand() % data->arraySize;

        // Enter critical section to safely examine/modify the array
        {
            CriticalSectionGuard guard(*data->cs);

            // 3.3 If the element is zero, mark it
            if (data->array[index] == 0) {
                Sleep(MARKER_SLEEP_MS);                  // 3.3.1 sleep 5 ms
                data->array[index] = data->id;           // 3.3.2 set marker's id
                Sleep(MARKER_SLEEP_MS);                  // 3.3.3 sleep 5 ms
                data->markedIndices.push_back(index);    // remember for later clearing
                // leave CS, continue the loop (3.3.4)
            }
            else {
                // 3.4 Element is not zero – cannot continue
                // Leave the CS before signaling main and waiting for reply
                // (guard will release CS here when block ends)
            }
        }

        // If we found a non-zero element, the if branch was skipped; handle it now
        // (We are outside the critical section)
        if (data->array[index] != 0) {  // re-check to know we need to signal
            // 3.4.1 Output information
            std::cout << "Marker " << data->id
                << ": marked count = " << data->markedIndices.size()
                << ", failed index = " << index << std::endl;

            // 3.4.2 Signal main that this marker cannot proceed
            SetEvent(data->cannotContinueEvent);

            // 3.4.3 Wait for a signal from main: stop or continue
            HANDLE waitHandles[2] = { data->stopEvent, data->continueEvent };
            DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

            if (result == WAIT_OBJECT_0) {
                // 4. Received stop signal: clear all marked elements and exit
                {
                    CriticalSectionGuard guard(*data->cs);
                    for (int idx : data->markedIndices) {
                        data->array[idx] = 0;
                    }
                }
                return 0;  // 4.2 finish work
            }
            else if (result == WAIT_OBJECT_0 + 1) {
                // 5. Received continue signal: continue the loop (back to step 3)
                // The continueEvent is manual-reset; main will reset it after all
                // markers have been awakened. So we just proceed.
            }
            else {
                // Unexpected error
                std::cerr << "Marker " << data->id << ": wait failed." << std::endl;
                return 1;
            }
        }
        // If the element was zero, we simply continue the loop immediately
    }
}

//---------------------------------------------------------------------
// Main function
//---------------------------------------------------------------------
int main() {
    try {
        // 1. Input array size
        int arraySize = 0;
        std::cout << "Enter array size: ";
        std::cin >> arraySize;
        if (arraySize <= 0) {
            std::cerr << "Error: array size must be positive.\n";
            return 1;
        }

        // Allocate and initialize array with zeros
        std::vector<int> arr(arraySize, 0);

        // 2. Input number of marker threads
        int numMarkers = 0;
        std::cout << "Enter number of marker threads: ";
        std::cin >> numMarkers;
        if (numMarkers <= 0) {
            std::cerr << "Error: number of markers must be positive.\n";
            return 1;
        }

        // 3. Create synchronization objects
        CRITICAL_SECTION cs;
        InitializeCriticalSection(&cs);

        // Manual-reset events
        UniqueHandle startEvent(CreateEventA(nullptr, TRUE, FALSE, nullptr)); // initially non-signaled
        if (!startEvent) throw std::runtime_error("Failed to create startEvent");

        UniqueHandle continueEvent(CreateEventA(nullptr, TRUE, FALSE, nullptr)); // initially non-signaled
        if (!continueEvent) throw std::runtime_error("Failed to create continueEvent");

        // 4. Prepare data structures for marker threads
        std::vector<std::unique_ptr<MarkerData>> markerDataList;
        std::vector<UniqueHandle> threadHandles; // handles of marker threads
        std::vector<HANDLE> allCannotContinueEvents; // to wait on

        for (int i = 0; i < numMarkers; ++i) {
            auto data = std::make_unique<MarkerData>();
            data->id = i + 1;  // order numbers start from 1
            data->array = arr.data();
            data->arraySize = arraySize;
            data->cs = &cs;

            // Create per-marker events (manual-reset, initially non-signaled)
            data->startEvent = startEvent.get(); // shared
            data->continueEvent = continueEvent.get(); // shared

            UniqueHandle stopEvent(CreateEventA(nullptr, TRUE, FALSE, nullptr));
            if (!stopEvent) throw std::runtime_error("Failed to create stopEvent");
            data->stopEvent = stopEvent.get();

            UniqueHandle cannotContinueEvent(CreateEventA(nullptr, TRUE, FALSE, nullptr));
            if (!cannotContinueEvent) throw std::runtime_error("Failed to create cannotContinueEvent");
            data->cannotContinueEvent = cannotContinueEvent.get();

            // Create the thread (passing raw pointer; ownership will be transferred)
            HANDLE hThread = CreateThread(nullptr, 0, MarkerThread, data.get(), 0, nullptr);
            if (!hThread) {
                DWORD err = GetLastError();
                throw std::runtime_error("CreateThread failed with error " + std::to_string(err));
            }

            // Store the thread handle and the events
            threadHandles.push_back(UniqueHandle(hThread));
            allCannotContinueEvents.push_back(data->cannotContinueEvent);
            markerDataList.push_back(std::move(data));

            // Ownership of events is kept inside MarkerData; the UniqueHandle's
            // for stopEvent and cannotContinueEvent are moved into the MarkerData?
            // We created them as local UniqueHandle, but we need to store them
            // persistently. We must release ownership from the UniqueHandle
            // and give it to the MarkerData. So we do:
            stopEvent.release();   // we transferred the raw handle to data->stopEvent
            cannotContinueEvent.release();
        }

        // 5. Signal all markers to start
        SetEvent(startEvent.get());

        // 6. Main loop
        std::vector<MarkerData*> activeMarkers; // pointers to active markers
        for (auto& d : markerDataList) {
            activeMarkers.push_back(d.get());
        }
        std::vector<HANDLE> activeThreadHandles; // handles of active threads
        for (auto& h : threadHandles) {
            activeThreadHandles.push_back(h.get());
        }

        while (!activeMarkers.empty()) {
            // 6.1 Wait for all active markers to signal that they cannot continue
            std::vector<HANDLE> waitEvents;
            for (auto* m : activeMarkers) {
                waitEvents.push_back(m->cannotContinueEvent);
            }
            DWORD waitResult = WaitForMultipleObjects(
                static_cast<DWORD>(waitEvents.size()),
                waitEvents.data(),
                TRUE,
                INFINITE);
            if (waitResult == WAIT_FAILED) {
                DWORD err = GetLastError();
                std::cerr << "WaitForMultipleObjects failed: " << err << "\n";
                return 1;
            }

            // 6.2 Print the array (protected by critical section)
            {
                CriticalSectionGuard guard(cs);
                std::cout << "Array contents: ";
                for (int val : arr) {
                    std::cout << val << " ";
                }
                std::cout << std::endl;
            }

            // 6.3 Ask which marker to stop
            int stopId = 0;
            std::cout << "Enter the number of the marker to finish: ";
            std::cin >> stopId;
            if (stopId < 1 || stopId > numMarkers) {
                std::cerr << "Invalid marker number.\n";
                continue; // or handle differently
            }

            // Find the MarkerData for this id
            auto itMarker = std::find_if(activeMarkers.begin(), activeMarkers.end(),
                [stopId](const MarkerData* m) { return m->id == stopId; });
            if (itMarker == activeMarkers.end()) {
                std::cerr << "Marker " << stopId << " is not active.\n";
                continue;
            }

            MarkerData* target = *itMarker;

            // 6.4 Signal the chosen marker to stop
            SetEvent(target->stopEvent);

            // 6.5 Wait for that marker's thread to finish
            // Find the corresponding thread handle (active threads are in same order)
            size_t index = std::distance(activeMarkers.begin(), itMarker);
            HANDLE hTargetThread = activeThreadHandles[index];
            WaitForSingleObject(hTargetThread, INFINITE);

            // Close the thread handle (it's already been waited on)
            // We can remove it from activeThreadHandles after this.

            // 6.6 Print the array after the marker has cleared its elements
            {
                CriticalSectionGuard guard(cs);
                std::cout << "Array after marker " << stopId << " finished: ";
                for (int val : arr) {
                    std::cout << val << " ";
                }
                std::cout << std::endl;
            }

            // 6.7 Signal the remaining active markers to continue
            // First, reset the continueEvent (so it doesn't stay signaled)
            ResetEvent(continueEvent.get());
            // Then reset each marker's cannotContinueEvent (since they are about to enter new cycle)
            for (auto* m : activeMarkers) {
                if (m != target) {
                    ResetEvent(m->cannotContinueEvent);
                }
            }
            // Now signal continue to all remaining active markers
            SetEvent(continueEvent.get());

            // Remove the finished marker from active lists
            activeMarkers.erase(itMarker);
            activeThreadHandles.erase(activeThreadHandles.begin() + index);
            // The MarkerData object is still owned by markerDataList, we don't need to delete it yet,
            // but the thread function released its copy (unique_ptr). We can optionally clean up.
        }

        // 7. All markers have finished; final message
        std::cout << "All marker threads have finished. Exiting.\n";

        // Cleanup critical section
        DeleteCriticalSection(&cs);

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}