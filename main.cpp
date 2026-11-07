#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <array>
#include <queue>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ctime>
#include <unistd.h>   // For close(), read(), write()
#include <iomanip> // For hex
#include <optional> // C++17 feature for safe return values
#include <thread>
#include <chrono>

#include "TextFileQueue.h"
#include "SerialPort.h"

using namespace std;
using namespace filesystem;
using namespace std::chrono;


int checkInput(queue<string> &inputQueue, map<string, string> &settings);

string exec(auto cmd);

bool saveSettings(const string &filename, const map<string, string> &settings);

map<string, string> loadSettings(const string &filename, bool showError = true);

bool deleteSettingsFile(const string &filename);

string replace_all(string str, const string &from, const string &to);

//void printSetting(const map<string, string>& settings, const string& key);

vector<string> tokenize(string s, const string &delim);

bool is_number(const string &s);

string findValueInCSV(const string &filename, const string &key);

bool csvFileExists(const string &filename, const string &workingDirectory);

bool createFileFromTemplate(const string &filename, const string &workingDirectory, const vector<string> &content);

bool createDirectory(const string &directoryName);

bool deleteFile(const string &filePath);

void logMessage(const string &message, const map<string, string> &settings); // Function to log messages to a file

string readValue(const string &filename, const map<string, string> &settings);//static void saveValue(const string& value, const string& filename, const map<string, string>& settings);// Function to save vales to a file

int printLabel(const string &printText, const map<string, string> &settings, int quantity = 1, bool isTempLabel = false); // This function reads a value from the specified file and returns it.

bool isPackageInstalled(const string &packageName);

bool checkDependencies();

enum class BaudRate : unsigned int {
    R38400 = 0000017, // B38400
    R57600 = 0010001, // B57600
    R115200 = 0010002 // B115200
};

optional<BaudRate> validateBaudRate(const string& input);// Helper function to validate and convert a string input

class TextFileQueue;

//Debug states
#define DEBUG 0
#define VERBOSE 1
#define NORMAL 2
#define ERROR 3
#define SILENT 4
static auto LEVEL = VERBOSE - 1;

//Default settings
static map<string, string> loadDefaults() {
    LEVEL < NORMAL ? cout << "Loading defaults" << endl : cout;
    map<string, string> mySettings;
    mySettings["settingsFile"] = "settings.txt";
    mySettings["printer"] = "none";
    mySettings["mode"] = "pass";
    mySettings["debugLevel"] = to_string(VERBOSE);
    mySettings["glabelsFile"] = "label.glabels";
    mySettings["tempFile"] = "tempLabel.glabels";
    mySettings["equipmentFile"] = "equipment.csv";
    mySettings["newLabelFile"] = "newLabels.txt";
    mySettings["printQuality"] = "Graphics";
    mySettings["printMedia"] = "Custom.1x1in";
    mySettings["fit-to-page"] = "true";
    mySettings["workingDirectory"] = "./labels";
    mySettings["tempDirectory"] = "/tmp/labels";
    mySettings["activeYear"] = "2026";
    mySettings["currentLocation"] = "none";
    mySettings["serialInput1Device"] = "none";
    mySettings["serialInput1Baud"] = "R115200";
    mySettings["serialInput2Device"] = "none";
    mySettings["serialInput2Baud"] = "R115200";
    return mySettings;
}

// Template CSV content (embedded)
const vector<string> templateContent = {
    "equipment,area",
    "879237,Basement",
    "879241,Dock",
    "879242,Stacker",
    "879258,Storage"
};

//Profiling variables
static auto start = high_resolution_clock::now();
static auto finish   = high_resolution_clock::now();
//static auto duration = duration_cast<milliseconds>(finish - start);


int main() {
    LEVEL < VERBOSE ? cout << "Starting process" << endl : cout;

    //Initialize default settings
    map<string, string> mySettings = loadDefaults();

    // Load the settings back from the file
    {
        map<string, string> loadedSettings = loadSettings("settings.txt", false);
        LEVEL < ERROR
            ? cout << "Current settings file: " << endl << "  " << mySettings.find("settingsFile")->second << endl
            : cout;

        if (!loadedSettings.empty()) {
            // Check if the map is empty (meaning loading failed)
            LEVEL < NORMAL ? cout << "Loading settings" : cout;
            LEVEL < VERBOSE ? cout << ": " : cout;
            LEVEL < NORMAL ? cout << endl : cout;
            for (const auto &pair: loadedSettings) {
                LEVEL < VERBOSE ? cout << "  " << pair.first << ": " << pair.second << endl : cout;
                mySettings[pair.first] = pair.second;
            }
        } else {
            LEVEL < ERROR
                ? cout << "Failed to load settings" << endl << "  Attempting to create settings file" << endl
                : cout;

            //Setting did not load, try to create settings file
            if (saveSettings("settings.txt", mySettings)) {
                LEVEL < ERROR ? cout << "  Settings saved successfully!" << endl : cout;
            } else {
                cerr << "Failed to save settings." << endl;
                LEVEL < ERROR ? cout << "Failed to save settings." << endl : cerr;
            }
        }
    }

    //Set the debug level
    LEVEL = stoi(mySettings.find("debugLevel")->second) - 1;

    //Initialize the messaging queue
    queue<string> inputQueue;

    //Clear any buffered used inputs and enable user input buffer
    cin.clear();
    istream::sync_with_stdio(false);

    //Check for dependencies
    if (checkDependencies()) {
        return 1;
    }

    //Check that working directory exists
    string workingDirectory = mySettings.find("workingDirectory")->second;
    if (!exists(path(workingDirectory))) {
        if (!createDirectory(workingDirectory)) {
            cerr << "Failed to create working directory." << endl;
            LEVEL < ERROR ? cout << "Failed to create working directory." << endl : cout;
            return 1;
        }
    }

    //Check that working directory exists
    string tempDirectory = mySettings.find("tempDirectory")->second;
    if (!exists(path(tempDirectory))) {
        if (!createDirectory(tempDirectory)) {
            cerr << "Failed to create temp directory." << endl;
            LEVEL < ERROR ? cout << "Failed to create temp directory." << endl : cout;
            return 1;
        }
    }

    //Initialize the new label queue, check if the file exists, create it if not
    LEVEL < VERBOSE ? cout << "Initializing new label queue." << endl : cout;
    string newLabelFile = workingDirectory + "/" + mySettings.find("newLabelFile")->second;
    if (!std::ifstream(newLabelFile, std::ios::app).good()) {
        ofstream outFile(newLabelFile, std::ios::app);
        if (outFile.is_open()) {
            outFile << "Creating empty new label file." << endl;
            outFile.close();
        } else {
            cerr << "Unable to create file new label file." << endl;
            return 1; // Indicate an error
        }
    }
    TextFileQueue newLabelQueue(newLabelFile);

    //Initialize random number generator
    LEVEL < VERBOSE ? cout << "Initializing the random number generator." << endl : cout;
    srand(time(nullptr)); // Seed the random number generator

    //Main loop to allow running as a service
    for (;;) {
        //Process input queue
        if (inputQueue.empty() != true) {
            //Read the next message in the queue
            LEVEL < VERBOSE ? cout << "There are " << inputQueue.size() << " messages" << endl : cout;
            static string message;
            message = inputQueue.front();
            inputQueue.pop();
            LEVEL < VERBOSE ? cout << "Read message: " << message << endl : cout;

            //Handle the message
            {
                if (message == "exit" || message == "quit") {
                    LEVEL < VERBOSE ? cout << "Exiting" << endl : cout;
                    break;
                }
                if (message == "reboot") {
                    LEVEL < VERBOSE ? cout << "Rebooting" << endl : cout;
                    auto command = "";
                    command = "sudo reboot";
                    LEVEL < SILENT ? cout << exec(command) << endl : cout;
                    break;
                }
                if (message == "lsusb") {
                    //Show system usb devices
                    LEVEL < VERBOSE ? cout << "Displaying USB devices" << endl : cout;

                    auto command = "";
                    command = R"(lsusb | awk -v ln=1 '{print ln++  ": "  $0 }')";
                    LEVEL < SILENT ? cout << exec(command) << endl : cout;
                } else if (message == "printer") {
                    //List system printers
                    LEVEL < VERBOSE ? cout << "Displaying printers" << endl : cout;

                    auto command = "";
                    //command = "lpstat -p -d | awk -v ln=1 '{print ln++  \": \"  $0 }'";
                    command = R"(lpstat -a | cut -d " " -f1 | awk -v ln=1 '{print ln++  ": "  $0 }')";
                    string ret = exec(command);
                    ret = replace_all(ret, "\n", "\n  ");
                    LEVEL < SILENT ? cout << "  " << ret << endl : cout;
                } else if (message.substr(0, 4) == "mode" || message.substr(0, 5) == "mode=") {
                    if (message.length() > 4) {
                        LEVEL < VERBOSE ? cout << "Setting mode" << endl : cout;
                        if (message == "mode=pass") {
                            LEVEL < VERBOSE ? cout << " Set 'pass'" << endl : cout;
                            mySettings["mode"] = "pass";
                        } else if (message == "mode=fail") {
                            LEVEL < VERBOSE ? cout << " Set 'fail'" << endl : cout;
                            mySettings["mode"] = "fail";
                        } else {
                            LEVEL < NORMAL ? cout << " Invalid mode, no change." << endl : cout;
                        }
                    }
                    LEVEL < NORMAL ? cout << " mode: " << mySettings.find("mode")->second << endl : cout;
                } else if (message.substr(0, 4) == "year" || message.substr(0, 5) == "year=") {
                    if (message.length() > 4) {
                        LEVEL < VERBOSE ? cout << "Setting year" << endl : cout;
                        string temp = message.substr(5);
                        mySettings["activeYear"] = message.substr(5);
                    }
                    LEVEL < NORMAL ? cout << " year: " << mySettings.find("activeYear")->second << endl : cout;
                } else if (message.substr(0, 5) == "media" || message.substr(0, 6) == "media=") {
                    if (message.length() > 5) {
                        LEVEL < VERBOSE ? cout << "Setting printer media" << endl : cout;
                        mySettings["printMedia"] = message.substr(6);
                    }
                    LEVEL < NORMAL ? cout << " media: " << mySettings.find("printMedia")->second << endl : cout;
                } else if (message.substr(0, 10) == "labelsFile" || message.substr(0, 11) == "labelsFile=") {
                    if (message.length() > 10) {
                        LEVEL < VERBOSE ? cout << "Setting glabels filename for normal labels" << endl : cout;
                        mySettings["glabelsFile"] = message.substr(11);
                    }
                    LEVEL < NORMAL ? cout << " labelsFile: " << mySettings.find("glabelsFile")->second << endl : cout;
                } else if (message.substr(0, 9) == "tempFile=") {
                    LEVEL < VERBOSE ? cout << "Setting glabels filename for temp labels" << endl : cout;
                    mySettings["tempFile"] = message.substr(9);
                } else if (message.substr(0, 13) == "equipmentFile=") {
                    LEVEL < VERBOSE ? cout << "Setting equipment file name" << endl : cout;
                    mySettings["equipmentFile"] = message.substr(13);
                } else if (message.substr(0, 16) == "workingDirectory" || message.substr(0, 17) ==
                           "workingDirectory=") {
                    if (message.length() > 16) {
                        LEVEL < VERBOSE ? cout << "Setting working directory" << endl : cout;
                        mySettings["workingDirectory"] = message.substr(17);
                    }
                    LEVEL < NORMAL
                        ? cout << "  workingDirectory: " << mySettings.find("workingDirectory")->second << endl
                        : cout;
                } else if (message.substr(0, 13) == "tempDirectory" || message.substr(0, 14) == "tempDirectory=") {
                    if (message.length() > 13) {
                        LEVEL < VERBOSE ? cout << "Setting temp directory" << endl : cout;
                        mySettings["tempDirectory"] = message.substr(14);
                    }
                    LEVEL < NORMAL
                        ? cout << "  tempDirectory: " << mySettings.find("tempDirectory")->second << endl
                        : cout;
                } else if (message.substr(0, 18) == "serialInput1Device" || message.substr(0, 19) == "serialInput1Device=") {
                    if (message.length() > 18) {
                        LEVEL < VERBOSE ? cout << "Setting serial input 1 device path" << endl : cout;
                        mySettings["serialInput1Device"] = message.substr(19);
                    }
                    LEVEL < NORMAL ? cout << "  serialInput1Device: " << mySettings.find("serialInput1Device")->second << endl : cout;
                } else if (message.substr(0, 18) == "serialInput2Device" || message.substr(0, 19) == "serialInput2Device=") {
                    if (message.length() > 18) {
                        LEVEL < VERBOSE ? cout << "Setting serial input 2 device path" << endl : cout;
                        mySettings["serialInput2Device"] = message.substr(19);
                    }
                    LEVEL < NORMAL ? cout << "  serialInput2Device: " << mySettings.find("serialInput2Device")->second << endl : cout;
                } else if (message.substr(0, 16) == "serialInput1Baud" || message.substr(0, 17) == "serialInput1Baud=") {
                    if (message.length() > 16) {
                        LEVEL < VERBOSE ? cout << "Setting serial input 1 baud rate" << endl : cout;
                        if (message.substr(17) != "R38400" && message.substr(17) != "R57600" && message.substr(17) != "R115200") {
                            LEVEL < NORMAL ? cout << "  Invalid data rate" << endl : cout;
                        } else {
                            mySettings["serialInput1Baud"] = message.substr(17);
                        }
                    }
                    LEVEL < NORMAL ? cout << "  serialInput1Baud: " << mySettings.find("serialInput1Baud")->second << endl : cout;
                } else if (message.substr(0, 16) == "serialInput2Baud" || message.substr(0, 17) == "serialInput2Baud=") {
                    if (message.length() > 16) {
                        LEVEL < VERBOSE ? cout << "Setting serial input 2 baud rate" << endl : cout;
                        if (message.substr(17) != "R38400" && message.substr(17) != "R57600" && message.substr(17) != "R115200") {
                            LEVEL < NORMAL ? cout << "  Invalid data rate" << endl : cout;
                        } else {
                            mySettings["serialInput2Baud"] = message.substr(17);
                        }
                    }
                    LEVEL < NORMAL
                        ? cout << "  serialInput2Baud: " << mySettings.find("serialInput2Baud")->second << endl
                        : cout;
                } else if (message.substr(0, 13) == "printQuality=") {
                    LEVEL < VERBOSE ? cout << "Setting print quality" << endl : cout;
                    mySettings["printerQuality"] = message.substr(13);
                } else if (message.substr(0, 10) == "fitToPage=") {
                    LEVEL < VERBOSE ? cout << "Setting fit-to-page true/false" << endl : cout;
                    mySettings["fitToPage"] = message.substr(10);
                } else if (message.substr(0, 16) == "currentLocation=") {
                    LEVEL < VERBOSE ? cout << "Setting current location for logging" << endl : cout;
                    mySettings["currentLocation"] = message.substr(16);
                } else if (message.substr(0, 13) == "newLabelFile=") {
                    LEVEL < VERBOSE ? cout << "Setting the filename of the new label queue" << endl : cout;
                    mySettings["newLabelFile"] = message.substr(13);
                } else if (message.substr(0, 8) == "setting=") {
                    LEVEL < VERBOSE ? cout << "Printing setting to console" << endl : cout;
                    LEVEL < SILENT ? cout << mySettings[message.substr(8)] << endl : cout;
                } else if (message.substr(0, 8) == "printer=") {
                    LEVEL < VERBOSE ? cout << "Setting printer" << endl : cout;

                    if (is_number(message.substr(8))) {
                        int token = stoi(message.substr(8));

                        auto command = "";
                        command = R"(lpstat -a | cut -d " " -f1)";
                        string ret = exec(command);
                        vector<string> tokenString = tokenize(ret, "\n");

                        mySettings["printer"] = tokenString[token - 1];
                    } else {
                        LEVEL < SILENT ? cout << "  Invalid printer" << endl : cout;
                    }
                    LEVEL < SILENT ? cout << "  Printer: " << mySettings.find("printer")->second << endl : cout;
                } else if (message == "help" || message == "?" || message == "--help") {
                    LEVEL < SILENT ? cout << "Display help menu" << endl : cout;
                    LEVEL < SILENT ? cout << "help, ?, --help        : display help" << endl : cout;
                    LEVEL < SILENT ? cout << "mode                   : display/set scanning mode to pass/fail (fail records and doesn't print)" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: mode" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: mode=pass" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: mode=fail" << endl : cout;
                    LEVEL < SILENT ? cout << "printer                : set printer" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: printer           display printers" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: printer=1         select printer by number" << endl : cout;
                    LEVEL < SILENT ? cout << "currentLocation        : set current location to be recorded in the log" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: currentLocation=Performance" << endl : cout;
                    LEVEL < SILENT ? cout << "media                  : set/display active print media (must be valid for printer driver)" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: media" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: media=Custom.1x1in" << endl : cout;
                    LEVEL < SILENT ? cout << "printQuality           : set active print quality (must be valid for printer driver)" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: printQuality=Text" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: printQuality=Graphics" << endl : cout;
                    LEVEL < SILENT ? cout << "fitToPage              : set fit-to-page toggle (must be valid for printer driver)" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: fitToPage=true" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: fitToPage=false" << endl : cout;
                    LEVEL < SILENT ? cout << "newLabel               : print one or more next available new labels" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: newLabel" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: newLabel=10" << endl : cout;
                    LEVEL < SILENT ? cout << "tempLabel              : print one or more temporary labels" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: tempLabel" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: tempLabel=10" << endl : cout;
                    LEVEL < SILENT ? cout << "glabelsFile            : set/display glabels filename normal labels" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: glabelsFile" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: glabelsFile=labels.glabels" << endl : cout;
                    LEVEL < SILENT ? cout << "serialInput1Device     : print/display device path to input device" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput1Device" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput1Device=/dev/ttyUSB0" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput1Device=none  (disable input device)" << endl : cout;
                    LEVEL < SILENT ? cout << "serialInput1Baud       : print/display baud rate of input device" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput1Baud" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput1Baud=R115200  {R115200,R57600,R38400}" << endl : cout;
                    LEVEL < SILENT ? cout << "serialInput2Device     : print/display device path to input device" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput2Device" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput2Device=/dev/ttyUSB0" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput2Device=none  (disable input device)" << endl : cout;
                    LEVEL < SILENT ? cout << "serialInput2Baud       : print/display baud rate of input device" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput2Baud" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: serialInput2Baud=R115200  {R115200,R57600,R38400}" << endl : cout;
                    LEVEL < SILENT ? cout << "tempFile               : set glabels filename for temp labels" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: tempFile=tempLabels.glabels" << endl : cout;
                    LEVEL < SILENT ? cout << "year                   : set/display active year" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: year" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: year=2026" << endl : cout;
                    LEVEL < SILENT ? cout << "equipmentFile          : set equipment filename" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: equipmentFile=equipment.csv" << endl : cout;
                    LEVEL < SILENT ? cout << "newLabelFile           : set filename which contains the new label queue" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: newLabelFile=newLabels.txt" << endl : cout;
                    LEVEL < SILENT ? cout << "workingDirectory       : set/display working directory" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: workingDirectory" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: workingDirectory=./labels" << endl : cout;
                    LEVEL < SILENT ? cout << "tempDirectory          : set/display working directory" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: tempDirectory" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: tempDirectory=/tmp/labels" << endl : cout;
                    LEVEL < SILENT ? cout << "setting                : print a specific setting to console" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: setting=printer" << endl : cout;
                    LEVEL < SILENT ? cout << "clear                  : clear current settings and set defaults" << endl : cout;
                    LEVEL < SILENT ? cout << "save                   : save current settings" << endl : cout;
                    LEVEL < SILENT ? cout << "debug                  : set the debug level" << endl : cout;
                    LEVEL < SILENT ? cout << "  usage: debug=VERBOSE" << endl : cout;
                    LEVEL < SILENT ? cout << "  params: {DEBUG,VERBOSE,NORMAL,ERROR,SILENT}" << endl : cout;
                    LEVEL < SILENT ? cout << "exit, quit             : exit process" << endl : cout;
                    LEVEL < SILENT ? cout << "reboot                 : reboot the device" << endl : cout;
                } else if (message == "clear") {
                    LEVEL < VERBOSE ? cout << "Clear setting file and restore default settings" << endl : cout;
                    string value = mySettings.find("settingsFile")->second;
                    LEVEL < NORMAL ? cout << "Settings file: " << endl << "  " << value << endl : cout;
                    deleteSettingsFile(value);

                    //Set defaults
                    mySettings = loadDefaults();

                    //Save
                    if (saveSettings(mySettings.find("settingsFile")->second, mySettings)) {
                        LEVEL < NORMAL ? cout << "Settings saved successfully!" << endl : cout;
                    } else {
                        cerr << "Failed to save settings." << endl;
                        LEVEL < ERROR ? cout << "Failed to save settings." << endl : cout;
                    }
                } else if (message == "save") {
                    LEVEL < VERBOSE ? cout << "Save setting file" << endl : cout;
                    string value = mySettings.find("settingsFile")->second;
                    LEVEL < NORMAL ? cout << "Settings file: " << endl << "  " << value << endl : cout;
                    if (saveSettings(value, mySettings)) {
                        LEVEL < NORMAL ? cout << "Settings saved successfully!" << endl : cout;
                    } else {
                        cerr << "Failed to save settings." << endl;
                        LEVEL < ERROR ? cout << "Failed to save settings." << endl : cout;
                    }
                } else if (message.substr(0, 6) == "debug=") {
                    LEVEL < NORMAL ? cout << "Setting debug level" << endl : cout;
                    string value = message.substr(6);
                    if (value == "DEBUG") {
                        LEVEL = DEBUG - 1;
                    } else if (value == "VERBOSE") {
                        LEVEL = VERBOSE - 1;
                    } else if (value == "NORMAL") {
                        LEVEL = NORMAL - 1;
                    } else if (value == "ERROR") {
                        LEVEL = ERROR - 1;
                    } else if (value == "SILENT") {
                        LEVEL = SILENT - 1;
                    } else {
                        LEVEL < NORMAL ? cout << "  No change" << endl : cout;
                    }
                    string levelName;
                    switch (LEVEL + 1) {
                        case VERBOSE:
                            levelName = "VERBOSE";
                            break;
                        case DEBUG:
                            levelName = "DEBUG";
                            break;
                        case NORMAL:
                            levelName = "NORMAL";
                            break;
                        case ERROR:
                            levelName = "ERROR";
                            break;
                        case SILENT:
                            levelName = "SILENT";
                            break;
                        default:
                            (void) 0;
                    }
                    LEVEL < SILENT ? cout << "  Debug level: " << levelName << endl : cout;
                } else if (message == "newLabel" || message.substr(0, 9) == "newLabel=") {
                    LEVEL < VERBOSE ? cout << "Prepare to print new label(s)" << message << endl : cout;

                    int token = 1;
                    if (message.length() > 8) {
                        if (is_number(message.substr(9))) {
                            token = stoi(message.substr(9));
                        }
                    }

                    //Loop to create new labels
                    for (int count = 0; count < token; count++) {
                        //Get the next available label
                        message = "";
                        message = newLabelQueue.dequeue();
                        if (message.empty()) {
                            LEVEL < NORMAL ? cout << "  Label queue empty" << endl : cout;
                        } else {
                            //Record the equipment number in a log file
                            LEVEL < VERBOSE ? cout << "Logging equipment number" << endl : cout;
                            logMessage(message, mySettings);

                            //Print a label
                            if (printLabel(message, mySettings)) {
                                LEVEL < ERROR ? cout << "  Label did not print." << message << endl : cout;
                            } else {
                                //Consume new label
                                LEVEL < NORMAL ? cout << "  Label printed." << message << endl : cout;
                                LEVEL < VERBOSE ? cout << "  Consuming: " + message << endl : cout;
                                newLabelQueue.dequeue_remove();
                                message = newLabelQueue.peek();
                                LEVEL < VERBOSE ? cout << "  Next: " + message << endl : cout;
                            };
                        }
                    }
                } else if (message == "tempLabel" || message.substr(0, 10) == "tempLabel=") {
                    LEVEL < VERBOSE ? cout << "Prepare to print temp label(s)" << message << endl : cout;

                    int token = 1;
                    if (message.length() > 9) {
                        if (is_number(message.substr(10))) {
                            token = stoi(message.substr(10));
                        }
                    }

                    //Print a label
                    if (printLabel(message, mySettings, token, true)) {
                        LEVEL < ERROR ? cout << "  Label(s) did not print." << message << endl : cout;
                    };
                } else if (message.length() == 6 && is_number(message)) {
                    LEVEL < NORMAL ? cout << "Label format received: " << message << endl : cout;

                    //Record the equipment number in a log file
                    LEVEL < VERBOSE ? cout << "Logging equipment number" << endl : cout;
                    logMessage(message, mySettings);

                    //Print a label
                    if (mySettings.find("mode")->second == "pass" ? true : false) {
                        LEVEL < VERBOSE ? cout << "Prepare to print label " << message << endl : cout;
                        printLabel(message, mySettings);
                    }
                } else {
                    LEVEL < VERBOSE ? cout << "Unknown command: " << message << endl : cout;
                }
            }
        }

        //Check for new inputs
        checkInput(inputQueue, mySettings);
        usleep(1000);  // Small delay to avoid busy-waiting (adjust as needed)
    }

    //Clean up
    LEVEL < VERBOSE ? cout << "Exiting process " << endl : cout;
    return 0;
}


int checkInput(queue<string> &inputQueue, map<string, string> &settings) {
    LEVEL < DEBUG ? cout << "Entering input queuing function." << endl : cout;

    //Loop through all waiting standard inputs (cin)
    LEVEL < DEBUG ? cout << "Input in input buffer (cin): " << cin.rdbuf()->in_avail() << endl : cout;
    if (cin.rdbuf()->in_avail()) {
        static string input_cin;
        getline(cin, input_cin);
        LEVEL < VERBOSE ? cout << "The following input was read the Console: " << input_cin << endl : cout;
        inputQueue.push(input_cin);
        input_cin.clear();
    }

    //Loop through all waiting serial inputs (/dev/ttyUSB0)
    if (settings.find("serialInput1Device")->second != "none") {
        static string inputSerial1;
        static vector<string> inputSerial1Tokens;
        string portSerial1 = settings.find("serialInput1Device")->second;
        unsigned int baudSerial1 = static_cast<int>(validateBaudRate(settings.find("serialInput1Baud")->second).value());
        static int failCountSerial1 = 0;
        static SerialPort_CH9350L serial1(portSerial1, baudSerial1, LEVEL);
        if (serial1.readData(inputSerial1)) {
            //Serial connection failed, count failures and disable if needed
            failCountSerial1++;
            if (failCountSerial1 >= 10) {
                LEVEL < ERROR ? cout << "Repeat serial connection errors, setting serialInput1Device = 'none'" << endl : cout;
                settings["serialInput1Device"] = "none";
                failCountSerial1 = 0;
            }
        } else {
            failCountSerial1 = 0;
        }

        //Tokenize serial data, Enter key = 0x0D = "\r" -> new line feed
        inputSerial1Tokens = tokenize(inputSerial1,"\r");
        inputSerial1.clear();
        if (inputSerial1Tokens.back().empty()) {
            inputSerial1Tokens.pop_back();
        }

        //Push tokens onto inputQueue
        while (!inputSerial1Tokens.empty()) {
            LEVEL < VERBOSE ? cout << "The following input was read from Serial1: " << inputSerial1Tokens[0] << endl : cout;
            inputQueue.push(inputSerial1Tokens[0]);
            inputSerial1Tokens.erase(inputSerial1Tokens.begin());
        }
        inputSerial1Tokens.clear();
    }

    if (settings.find("serialInput2Device")->second != "none") {
        static string inputSerial2;
        string portSerial2 = settings.find("serialInput2Device")->second;
        unsigned int baudSerial2 = static_cast<int>(validateBaudRate(settings.find("serialInput2Baud")->second).value());
        static int failCountSerial2 = 0;
        static SerialPort_CH9350L serial2(portSerial2, baudSerial2, LEVEL);
        if (serial2.readData(inputSerial2)) {
            //Serial connection failed, count failures and disable if needed
            failCountSerial2++;
            if (failCountSerial2 >= 10) {
                LEVEL < ERROR ? cout << "Repeat serial connection errors, setting serialInput2Device = 'none'" << endl : cout;
                settings["serialInput2Device"] = "none";
                failCountSerial2 = 0;
            }
        } else {
            failCountSerial2 = 0;
        }

        if (inputSerial2.back() == 0x0D) {
            //0x0D = Enter key -> new line feed
            inputSerial2 = inputSerial2.substr(0, inputSerial2.length() - 1);
            LEVEL < VERBOSE ? cout << "The following input was read: " << inputSerial2 << endl : cout;
            inputQueue.push(inputSerial2);
            inputSerial2.clear();
        }
        if (LEVEL < DEBUG) {
            sleep(1);
        }
    }

    LEVEL < DEBUG ? cout << "Exiting input queuing function" << endl : cout;
    return 0;
}

string exec(auto cmd) {
    array<char, 128> buffer{};
    string result;
    const unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool saveSettings(const string &filename, const map<string, string> &settings) {
    // Function to write program settings to a file
    ofstream outfile(filename); // Open the file for writing

    if (!outfile.is_open()) {
        cerr << "Error: Could not open file " << filename << " for writing." << endl;
        LEVEL < ERROR ? cout << "Error: Could not open file " << filename << " for writing." << endl : cout;
        return false;
    }

    for (const auto &pair: settings) {
        outfile << pair.first << ":" << pair.second << "\n"; // Write key-value pairs with a colon separator
    }

    outfile.close();
    LEVEL < NORMAL ? cout << "Settings saved to " << filename << endl : cout;
    return true;
}

map<string, string> loadSettings(const string &filename, bool showError) {
    // Function to load program settings from a file
    ifstream infile(filename);
    map<string, string> settings;

    if (!infile.is_open()) {
        if (showError) {
            cerr << "Error: Could not open file " << filename << " for reading." << endl;
            LEVEL < ERROR ? cout << "Error: Could not open file " << filename << " for reading." << endl : cout;
        }
        return settings; // Return an empty map if the file doesn't exist or can't be opened
    }

    string line;
    while (getline(infile, line)) {
        size_t pos = line.find(':');
        if (pos != string::npos) {
            // Ensure a colon was found
            string key = line.substr(0, pos);
            string value = line.substr(pos + 1);
            settings[key] = value;
        }
    }

    infile.close();
    return settings;
}

bool deleteSettingsFile(const string &filename) {
    // Function to remove the settings file
    try {
        remove(filename); // Use remove_file for C++17 and later
        LEVEL < ERROR ? cout << "Settings file '" << filename << "' deleted." << endl : cout;
        return true;
    } catch (const exception &e) {
        cerr << "Error deleting settings file '" << filename << "': " << e.what() << endl;
        LEVEL < ERROR ? cout << "Error deleting settings file '" << filename << "': " << e.what() << endl : cout;
        return false;
    }
}

void printSetting(const map<string, string> &settings, const string &key) {
    // Function to print a single setting from the map
    auto it = settings.find(key);
    if (it != settings.end()) {
        LEVEL < ERROR ? cout << key << ": " << it->second << endl : cout;
    } else {
        cerr << "Warning: Setting '" << key << "' not found in the settings." << endl;
        LEVEL < ERROR ? cout << "Warning: Setting '" << key << "' not found in the settings." << endl : cout;
    }
}

string replace_all(string str, const string &from, const string &to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Move past the last replaced substring
    }
    return str;
}

vector<string> tokenize(string s, const string &delim) {
    vector<string> tokens;
    size_t pos = 0;

    while ((pos = s.find(delim)) != string::npos) {
        tokens.push_back(s.substr(0, pos));
        s.erase(0, pos + delim.length());
    }
    tokens.push_back(s); // Add the last token
    return tokens;
}

bool is_number(const string &s) {
    string::const_iterator it = s.begin();
    while (it != s.end() && isdigit(*it)) ++it;
    return !s.empty() && it == s.end();
}

string findValueInCSV(const string &filename, const string &key) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        LEVEL < ERROR ? cout << "Error opening file: " << filename << endl : cout;
        return ""; // Or throw an exception if appropriate for your use case
    }

    string line;
    while (getline(file, line)) {
        stringstream ss(line);
        string cell1, cell2;

        if (getline(ss, cell1, ',') && getline(ss, cell2, ',')) {
            if (cell1 == key) {
                LEVEL < VERBOSE ? cout << "Value for key '" << key << "' is: " << cell2 << endl : cout;
                return cell2;
            }
        }
    }

    LEVEL < VERBOSE ? cout << "Key '" << key << "' not found in the CSV file." << endl : cout;
    return "";
}

bool csvFileExists(const string &filename, const string &workingDirectory) {
    return exists(path(workingDirectory + "/" + filename));
}

bool createFileFromTemplate(const string &filename, const string &workingDirectory, const vector<string> &content) {
    ofstream outputFile(workingDirectory + "/" + filename);
    if (!outputFile.is_open()) {
        cerr << "Error: Could not create/open output file: " << filename << endl;
        LEVEL < ERROR ? cout << "Error: Could not create/open output file: " << filename << endl : cout;
        return false;
    }

    // Write the template content to the output file
    for (const auto &line: content) {
        outputFile << line << endl;
    }

    outputFile.close();
    return true;
}

bool createDirectory(const string &directoryName) {
    try {
        create_directories(directoryName);
        return true;
    } catch (const exception &e) {
        cerr << "Error creating directory: " << e.what() << endl;
        LEVEL < ERROR ? cout << "Error creating directory: " << e.what() << endl : cout;
        return false;
    }
}

bool deleteFile(const string &filePath) {
    // Function to delete a file, handling potential errors and specifying a working directory
    try {
        path path(filePath);

        // Check if the file exists before attempting to delete it. This prevents exceptions.
        if (!exists(path)) {
            cerr << "Error: File '" << filePath << "' does not exist." << endl;
            LEVEL < ERROR ? cout << "Error: File '" << filePath << "' does not exist." << endl : cout;
            return false; // File doesn't exist, so return failure
        }

        // Attempt to delete the file
        remove(path);

        if (!exists(path)) {
            LEVEL < VERBOSE ? cout << "File '" << filePath << "' deleted successfully." << endl : cout;
            return true; // Successfully deleted
        } else {
            cerr << "Error: Failed to delete file '" << filePath << "'." << endl;
            LEVEL < ERROR ? cout << "Error: Failed to delete file '" << filePath << "'." << endl : cout;
            return false; // Failed to delete (likely permissions issue)
        }
    } catch (const filesystem_error &e) {
        cerr << "Filesystem error deleting '" << filePath << "': " << e.what() << endl;
        LEVEL < ERROR ? cout << "Filesystem error deleting '" << filePath << "': " << e.what() << endl : cout;
        return false; // Handle filesystem errors gracefully
    }
}

void logMessage(const string &message, const map<string, string> &settings) {
    // Function to log messages to a file
    string currentLocation = settings.find("currentLocation")->second;
    string mode = settings.find("mode")->second;
    string logFilename = settings.find("workingDirectory")->second + "/log.txt";
    // Use working directory for the log file
    ofstream outfile(logFilename, ios::app); // Open in append mode

    if (outfile.is_open()) {
        time_t now = time(nullptr);
        struct tm *ltm = localtime(&now);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);

        outfile << "[" << buffer << "]," << message << "," << currentLocation << "," << mode << endl;
        outfile.close();
    } else {
        cerr << "Error: Could not open log file: " << logFilename << endl;
        LEVEL < ERROR ? cout << "Error: Could not open log file: " << logFilename << endl : cout;
    }
}

void saveValue(const string &value, const string &filename, const map<string, string> &settings) {
    // Function to vales to a file
    string currentLocation = settings.find("currentLocation")->second;
    string filepath = settings.find("workingDirectory")->second + "/" + filename;
    ofstream outfile(filepath, ios::out); // Open in write mode

    if (outfile.is_open()) {
        outfile << value << endl;
        outfile.close();
    } else {
        cerr << "Error: Could not open log file: " << filepath << endl;
        LEVEL < ERROR ? cout << "Error: Could not open log file: " << filepath << endl : cout;
    }
}

string readValue(const string &filename, const map<string, string> &settings) {
    // This function reads a value from the specified file and returns it.
    string currentLocation = settings.find("currentLocation")->second;
    string workingDirectory = settings.find("workingDirectory")->second;
    string filepath = workingDirectory + "/" + filename;

    ifstream infile(filepath); // Open in read mode

    if (infile.is_open()) {
        string value;
        getline(infile, value); // Read a line from the file
        infile.close();
        return value;
    } else {
        cerr << "Error: Could not open log file: " << filepath << endl;
        LEVEL < ERROR ? cout << "Error: Could not open log file: " << filepath << endl : cout;
        return "";
        // Return an empty string in case of error.  Consider throwing an exception instead for more robust handling.
    }
}

int printLabel(const string &printText, const map<string, string> &settings, const int quantity,
               const bool isTempLabel) {
    //Get working directory
    string workingDirectory = settings.find("workingDirectory")->second;
    string tempDirectory = settings.find("tempDirectory")->second;

    string area;
    if (!isTempLabel) {
        //Check if equipment file exists
        string equipmentFile = settings.find("equipmentFile")->second;
        LEVEL < VERBOSE ? cout << "Checking is equipment file exists " << equipmentFile << endl : cout;

        if (csvFileExists(equipmentFile, workingDirectory)) {
            LEVEL < VERBOSE ? cout << "CSV file '" << equipmentFile << "' exists in '" << workingDirectory << "'." << endl : cout;
        } else {
            LEVEL < VERBOSE ? cout << "CSV file '" << equipmentFile << "' does not exist in '" << workingDirectory << ". Creating it..." << endl : cout;
            if (createFileFromTemplate(equipmentFile, workingDirectory, templateContent)) {
                LEVEL < VERBOSE ? cout << "CSV file '" << equipmentFile << "' created successfully in '" << workingDirectory << "'." << endl : cout;
            } else {
                LEVEL < ERROR ? cout << "Failed to create CSV file." << endl : cout;
                cerr << "Failed to create CSV file." << endl;
            }
        }

        //Search for area
        string filename = workingDirectory + "/" + equipmentFile; // Replace with your file name
        LEVEL < VERBOSE
            ? cout << "  Searching for equipment " << printText << " in " << workingDirectory + "/" + equipmentFile <<
              endl
            : cout;
        area = findValueInCSV(filename, printText);
        if (!area.empty()) {
            LEVEL < VERBOSE ? cout << "  Area found: " << area << endl : cout;
        } else {
            LEVEL < VERBOSE ? cout << "  Area not found, setting area to nothing" << area << endl : cout;
            area = "";
        }
    }

    //Get active year
    string activeYear = settings.find("activeYear")->second;
    LEVEL < VERBOSE ? cout << "  Active year: " << activeYear << endl : cout;

    //Generate random id for unique file names
    auto fileID = rand() % 999999; // Generate a random number between 0
    LEVEL < VERBOSE ? cout << "  ID generated: " << fileID << endl : cout;

    //Generate a batch CSV file for glabels
    LEVEL < VERBOSE ? cout << "  Creating batch file... " << fileID << endl : cout;
    string batchFile = printText + "_" + to_string(fileID) + ".csv";
    vector<string> batchContent = {};
    for (int count = 0; count < quantity; count++) {
        batchContent.push_back(printText + "," + area + "," + activeYear);
    }
    createFileFromTemplate(batchFile, tempDirectory, batchContent);
    LEVEL < VERBOSE ? cout << "  Batch file " << tempDirectory + "/" + batchFile << " created" << endl : cout;

    //Get glabels file
    string glabelsFile;
    if (isTempLabel) {
        glabelsFile = settings.find("tempFile")->second;
    } else {
        glabelsFile = settings.find("glabelsFile")->second;
    }
    LEVEL < VERBOSE ? cout << "  glabels file: " << glabelsFile << endl : cout;

    //Generate pdf file
    string pdfFile = replace_all(batchFile, ".csv", ".pdf");
    LEVEL < VERBOSE ? cout << "  pdf file: " << pdfFile << endl : cout;

    //Generate pdf of label using glabels batch command
    LEVEL < VERBOSE ? cout << "  Generating pdf of label" << endl : cout;
    string command;
    command = "glabels-3-batch -i " + tempDirectory + "/" + batchFile + " " + workingDirectory + "/" + glabelsFile +
              " -o " + tempDirectory + "/" + pdfFile;
    LEVEL < VERBOSE ? cout << "  Sending command: " << command << endl : cout;
    system(command.c_str());

    //Get active printMedia
    string printMedia = settings.find("printMedia")->second;
    LEVEL < VERBOSE ? cout << "  Active print media: " << printMedia << endl : cout;

    //Get active printer
    string printer = settings.find("printer")->second;
    LEVEL < VERBOSE ? cout << "  Active printer: " << printer << endl : cout;

    //Get active fit-to-page
    bool fitToPage = false;
    if (settings.find("fit-to-page")->second == "true") {
        fitToPage = true;
    };
    LEVEL < VERBOSE ? cout << "  fit-to-page: " << (fitToPage ? "true" : "false") << endl : cout;

    //Get active fit-to-page
    string printQuality = settings.find("printQuality")->second;
    LEVEL < VERBOSE ? cout << "  Print quality: " << printQuality << endl : cout;

    //Send pdf of label to printer
    command = "lpr -P " + printer +
              " " + (fitToPage ? "-o fit-to-page" : "") +
              " -o media=" + printMedia +
              " -o DymoPrintQuality=" + printQuality +
              " -o number-up=1 "
              " " + tempDirectory + "/" + pdfFile;
    LEVEL < VERBOSE ? cout << "  Sending command: " << command << endl : cout;
    system(command.c_str());

    //Clean up batch file and pdf
    LEVEL < VERBOSE ? cout << "  Clean up and remove working files" << endl : cout;
    LEVEL < VERBOSE ? cout << "   Deleting: " << batchFile << endl : cout;
    deleteFile(tempDirectory + "/" + batchFile);
    LEVEL < VERBOSE ? cout << "   Deleting: " << pdfFile << endl : cout;
    deleteFile(tempDirectory + "/" + pdfFile);
    return 0;
}

bool isPackageInstalled(const string &packageName) {
    string cmd = "dpkg -s " + packageName;
    auto result = system(cmd.c_str());

    if (result == 0) {
        return true;
    } else {
        return false;
    }
}

bool checkDependencies() {
    //Check that glabels is installed
    LEVEL < SILENT ? cout << "Check for the glabels package." << endl << "---------" << endl : cout;
    if (isPackageInstalled("glabels")) {
        LEVEL < SILENT ? cout << "  The glabels package is already installed." << endl : cout;
    } else {
        LEVEL < SILENT ? cout << "  The glabels package is not installed." << endl : cout;
        LEVEL < NORMAL ? cout << "  To install it, run the following command in your terminal:" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt update" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt install glabels" << endl : cout;
        return true;
    }
    LEVEL < SILENT ? cout << "---------" << endl : cout;

    //Check that cpus-bsd is installed
    LEVEL < SILENT ? cout << "Check for the cups-bsd package." << endl << "---------" << endl : cout;
    if (isPackageInstalled("cups-bsd")) {
        LEVEL < SILENT ? cout << "  The cups-bsd package is already installed." << endl : cout;
    } else {
        LEVEL < SILENT ? cout << "  The cups-bsd package is not installed." << endl : cout;
        LEVEL < NORMAL ? cout << "  To install it, run the following command in your terminal:" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt update" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt install cups-bsd" << endl : cout;
        return true;
    }
    LEVEL < SILENT ? cout << "---------" << endl : cout;

    //Check that cpus-bsd is installed
    LEVEL < SILENT ? cout << "Check for the cups package." << endl << "---------" << endl : cout;
    if (isPackageInstalled("cups")) {
        LEVEL < SILENT ? cout << "  The cups-bsd package is already installed." << endl : cout;
    } else {
        LEVEL < SILENT ? cout << "  The cups package is not installed." << endl : cout;
        LEVEL < NORMAL ? cout << "  To install it, run the following command in your terminal:" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt update" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt install cups" << endl : cout;
        return true;
    }
    LEVEL < SILENT ? cout << "---------" << endl : cout;

    //Check that printer-driver-dymo is installed
    LEVEL < SILENT ? cout << "Check for the printer-driver-dymo package." << endl << "---------" << endl : cout;
    if (isPackageInstalled("printer-driver-dymo")) {
        LEVEL < SILENT ? cout << "  The printer-driver-dymo package is already installed." << endl : cout;
    } else {
        LEVEL < SILENT ? cout << "  The printer-driver-dymo is not installed." << endl : cout;
        LEVEL < NORMAL ? cout << "  To install it, run the following command in your terminal:" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt update" << endl : cout;
        LEVEL < NORMAL ? cout << "  sudo apt install printer-driver-dymo" << endl : cout;
        return true;
    }
    LEVEL < SILENT ? cout << "---------" << endl : cout;

    return false;
}
optional<BaudRate> validateBaudRate(const string& input) { // Helper function to validate and convert a string input
    // Create a map that links user-friendly strings to the enum value.
    static const std::map<string, BaudRate> validRates = {
        {"R57600", BaudRate::R57600},
        {"R38400", BaudRate::R38400},
        {"R115200", BaudRate::R115200}
    };

    // Check if the input string exists as a key in our map.
    auto it = validRates.find(input);

    if (it != validRates.end()) {
        // If found, return the associated enum value wrapped in std::optional
        return it->second;
    } else {
        // If not found, return nothing
        return nullopt;
    }
}

