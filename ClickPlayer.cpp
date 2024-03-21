#include <iostream>
#include <fstream>
#include <string>
#include <random>
#include <Windows.h>
#include <csignal>
#include <chrono>

std::vector<float> clickDelays;

std::vector<float> readIntervalsFromFile(const std::string& filePath) {
    std::vector<float> intervals;
    std::ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        std::cerr << "Error opening file: " << filePath << "\n";
        return intervals;
    }
    std::string interval;
    while (std::getline(inputFile, interval, ',')) {
        intervals.push_back(std::stod(interval));
    }
    inputFile.close();
    return intervals;
}

void shuffle(std::vector<float>& vec) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(vec.begin(), vec.end(), gen);
}

float kernelDensityEstimation(const std::vector<float>& samples, float x, float bandwidth) {
    float sum = 0.0;
    for (float sample : samples) {
        sum += exp(-0.5 * pow((x - sample) / bandwidth, 2));
    }
    return sum / (samples.size() * sqrt(2 * 3.14159265359) * bandwidth);
}

float sampleFromKDE(const std::vector<float>& samples, float bandwidth) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<double> dis(0.0, 1.0);
    while (true) {
        double x = dis(gen) * (samples.back() - samples.front()) + samples.front();
        double y = dis(gen);
        if (y < kernelDensityEstimation(samples, x, bandwidth)) {
            return x;
        }
    }
}

void wait(double milliseconds) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::nano> elapsed = end - start;
        if (elapsed.count() >= milliseconds * 1e6)
            break;
    }
}

bool stopPlaying = false;

void signalHandler(int signal) {
    if (signal == SIGINT) {
        stopPlaying = true;
    }
}

bool isCursorVisible() {
    CURSORINFO pci;
    pci.cbSize = sizeof(CURSORINFO);
    GetCursorInfo(&pci);
    if (reinterpret_cast<intptr_t>(pci.hCursor) > 100000)
        return false;

    return true;
}

void playClicks(std::vector<float>& intervals) {
    float bandwidth = 20.0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.5, 7);
    shuffle(intervals);
    std::signal(SIGINT, signalHandler);
    std::cout << "Ready for playback. Press CTRL + C to exit back to menu.";
    while (!stopPlaying) {
        while (GetAsyncKeyState(VK_LBUTTON) && !(GetAsyncKeyState(VK_LSHIFT) & 0x8000) && !isCursorVisible()) {
            HWND window = GetForegroundWindow();
            if (FindWindowA(("LWJGL"), nullptr) == window) {
                float clickInterval = sampleFromKDE(intervals, bandwidth);
                wait(clickInterval);
                SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 0));
                wait(dis(gen));
                SendMessageW(window, WM_LBUTTONUP, MK_LBUTTON, MAKELPARAM(0, 0));
            }
        }
        wait(1);
    }
    std::signal(SIGINT, SIG_DFL);
}

void saveClickDelaysToFile(const std::string& filePath) {
    std::ofstream outputFile(filePath);
    if (!outputFile.is_open()) {
        std::cerr << "Error opening file: " << filePath << "\n";
        return;
    }
    for (size_t i = 0; i < clickDelays.size(); ++i) {
        outputFile << clickDelays[i];
        if (i < clickDelays.size() - 1) {
            outputFile << ",";
        }
    }
    outputFile.close();
}


void clickRecorder() {
    std::cout << "Click anywhere to start recording. Press left shift to save recording.\n";

    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER firstClickTime = { 0 };
    LARGE_INTEGER secondClickTime = { 0 };

    bool recording = false;
    while (!(GetAsyncKeyState(VK_LSHIFT) & 0x8000)) {
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            if (!recording) {
                std::cout << "Recording...\n";
                QueryPerformanceCounter(&firstClickTime);
                recording = true;
            }
            else {
                QueryPerformanceCounter(&secondClickTime);
                float delay = static_cast<float>((secondClickTime.QuadPart - firstClickTime.QuadPart) * 1000.0 / frequency.QuadPart);
                if (delay <= 150.0f) {
                    clickDelays.push_back(delay);
                    std::cout << "Recorded delay: " << delay << "ms\n";
                }
                else {
                    std::cout << "Delay too long (" << delay << "ms). Ignoring.\n";
                }
                firstClickTime = secondClickTime;
            }

            while (GetAsyncKeyState(VK_LBUTTON) & 0x8000) { wait(1); }
        }

        wait(1);
    }

    OPENFILENAME ofn;
    wchar_t szFileName[MAX_PATH] = L"";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.lpstrDefExt = L"txt";
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Save Recording";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetSaveFileName(&ofn) == TRUE) {
        std::wstring wideFileName(szFileName);
        std::string narrowFileName(wideFileName.begin(), wideFileName.end());
        std::ofstream outputFile(narrowFileName);
        if (outputFile.is_open()) {
            for (size_t i = 0; i < clickDelays.size(); ++i) {
                outputFile << clickDelays[i];
                if (i != clickDelays.size() - 1) {
                    outputFile << ",";
                }
            }
            outputFile.close();
            std::cout << "Recording saved to " << narrowFileName << ".\n";
        }
        else {
            std::cerr << "Unable to open file " << narrowFileName << " for writing.\n";
        }
    }
    else {
        DWORD error = CommDlgExtendedError();
        if (error != 0) {
            std::cerr << "Error in save file dialog: " << error << "\n";
        }
        else {
            std::cerr << "Save file dialog canceled.\n";
        }
    }
}

std::string openFileDialog() {
    OPENFILENAME ofn;
    wchar_t szFileName[MAX_PATH] = L"";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.lpstrDefExt = L"txt";
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select Intervals File";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileName(&ofn) == TRUE) {
        std::wstring wideFileName(szFileName);
        std::string narrowFileName(wideFileName.begin(), wideFileName.end());
        return narrowFileName;
    }
    else {
        DWORD error = CommDlgExtendedError();
        if (error != 0) {
            std::cerr << "Error in open file dialog: " << error << "\n";
        }
        else {
            std::cerr << "Open file dialog canceled.\n";
        }
        return "";
    }
}

int main() {
    SetConsoleTitle(TEXT("Owo Clicker"));
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, 0x0B);

    while (true) {
        int choice;
        std::cout << " _____           \n";
        std::cout << "|     |_ _ _ ___ \n";
        std::cout << "|  |  | | | | . |\n";
        std::cout << "|_____|_____|___|\n";
        std::cout << "\n";
        std::cout << "1. Click Player\n";
        std::cout << "2. Click Recorder\n";
        std::cout << "3. Exit\n";
        std::cout << "Enter your choice: ";
        std::cin >> choice;
        std::cin.ignore();
        if (choice == 1) {
            system("cls");
            std::string intervalsFilePath = openFileDialog();
            std::vector<float> intervals = readIntervalsFromFile(intervalsFilePath);
            if (!intervals.empty()) {
                std::cout << "Intervals loaded.\n";
                float totalInterval = 0.0;
                for (float interval : intervals) {
                    totalInterval += interval;
                }
                float averageInterval = totalInterval / intervals.size();
                float cps = 1000.0 / averageInterval;
                std::cout << "Average CPS from file: " << cps << "\n";
                std::cout << "Is this the file you want? (Y/N): ";
                char choice;
                std::cin >> choice;
                if (choice == 'Y' || choice == 'y') {
                    system("cls");
                    playClicks(intervals);
                }
                else {
                    std::cout << "File not selected for playback.\n";
                }
            }
            else
            {
                std::cerr << "No intervals found in file.\n";
            }
            system("cls");
        }
        else if (choice == 2) {
            system("cls");
            clickRecorder();
            system("cls");
        }
        else if (choice == 3) {
            break;
        }
        else {
            std::cout << "Invalid choice.\n";
        }
    }

    return 0;
}