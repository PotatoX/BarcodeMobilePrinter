#include <iostream>
#include <fstream>
#include <string>

using namespace std; // Added this line

class TextFileQueue {
private:
    string filename;
    string firstLine;
    string currentContent; // Stores the entire content of the file

public:
    TextFileQueue(const string& filename) : filename(filename), firstLine("") {}

    // Appends a value to the queue (adds a new row at the front)
    void enqueue(const string& value) {
        ofstream outfile(filename, ios_base::app); // Append mode
        if (outfile.is_open()) {
            outfile << value << "\n";
            outfile.close();
            currentContent = "";  // Reset current content after appending
            reloadFile();       // Reload the file to update currentContent
        } else {
            cerr << "Error opening file for appending." << endl;
        }
    }

    // Returns the value at the front of the queue (first line)
    string dequeue() {
        reloadFile();
        if (isEmpty()) {
            cerr << "Queue is empty. Cannot dequeue." << endl;
            return ""; // Or throw an exception if appropriate
        }

        ifstream infile(filename);
        if (!infile.is_open()) {
            cerr << "Error opening file for reading." << endl;
            return "";
        }

        string line;
        getline(infile, firstLine);
        infile.close();
        return firstLine;
    }

    // Returns the value at the front of the queue without removing it
    string peek() {
        if (isEmpty()) {
            cerr << "Queue is empty. Cannot peek." << endl;
            return ""; // Or throw an exception if appropriate
        }
        return firstLine;
    }

    // Removes the value at the front of the queue
    void dequeue_remove() {
        if (isEmpty()) {
            cerr << "Queue is empty. Cannot remove." << endl;
            return;
        }

        ifstream infile(filename);
        if (!infile.is_open()) {
            cerr << "Error opening file for reading/writing." << endl;
            return;
        }

        // Remove the original firstLine (if it exists)
        getline(infile, firstLine);

        string line;
        getline(infile, firstLine);
        currentContent = firstLine + "\n";  // Reset current content after removing the first line

        // Check if there's another line to read after the firstLine
        while (getline(infile, line)) {
            currentContent += line + "\n";
        }

        //Write current loaded content to file
        infile.close();
        ofstream outfile(filename);
        if (outfile.is_open()) {
            outfile << currentContent;
            outfile.close();
        }
        else {
            cerr << "Error opening file for writing." << endl;
        }

        reloadFile();       // Reload the file to update currentContent
    }

    // Checks if the queue is empty
    bool isEmpty() {
        reloadFile();
        return firstLine.empty();
    }

private:
    // Helper function to reload the file content into currentContent
    void reloadFile() {
        ifstream infile(filename);
        if (infile.is_open()) {
            string line;
            getline(infile, firstLine);
            currentContent = firstLine + "\n";
            while (getline(infile, line)) {
                currentContent += line + "\n";
            }
            infile.close();
        } else {
            cerr << "Error opening file for reading." << endl;
        }
    }
};