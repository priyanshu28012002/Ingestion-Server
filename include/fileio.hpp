#ifndef FILEIO_HPP
#define FILEIO_HPP

#include <string>
#include <sys/stat.h>

class FileIO {
private:
    std::string _source_file;
    int _file_descriptor = -1;
    void* _mapped_memory = nullptr;
    size_t _file_size = 0;
    size_t _buffer_size = 0;
    bool _is_file_backed = true;

    // Helper methods
    bool isFileAvailable() const;
    bool openFile();
    struct stat getFileStatistics() const;
    void mapFile();
    void cleanup();

public:
    // Constructor
    explicit FileIO(const std::string& sourceFile);
    
    // Destructor
    ~FileIO();

    // Delete copy constructor and assignment operator
    FileIO(const FileIO&) = delete;
    FileIO& operator=(const FileIO&) = delete;

    // Getters
    void* getMappedMemory() const { return _mapped_memory; }
    size_t getFileSize() const { return _file_size; }
    const std::string& getSourceFile() const { return _source_file; }
};

#endif // FILEIO_HPP