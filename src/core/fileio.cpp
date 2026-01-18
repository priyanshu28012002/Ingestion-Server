#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <fileio.hpp>

// Check if file is available and accessible
bool FileIO::isFileAvailable() const
{
    if (_source_file.empty())
    {
        return false;
    }

    // Use access() to check file existence and readability
    // F_OK: Check existence
    // R_OK: Check read permission
    return access(_source_file.c_str(), F_OK | R_OK) == 0;
}

// Open the file
bool FileIO::openFile()
{
    // Open file in read-only mode
    // O_RDONLY: Open for reading only
    // O_RDWR: Open for reading and writing (if you need to modify)
    _file_descriptor = open(_source_file.c_str(), O_RDONLY);

    if (_file_descriptor == -1)
    {
        std::cerr << "Failed to open file: " << _source_file
                  << " - Error: " << strerror(errno) << std::endl;
        return false;
    }

    return true;
}

// Get file statistics
struct stat FileIO::getFileStatistics() const
{
    struct stat file_stats;

    if (fstat(_file_descriptor, &file_stats) == -1)
    {
        throw std::runtime_error("Failed to get file statistics: " +
                                 std::string(strerror(errno)));
    }

    return file_stats;
}

// Cleanup resources
void FileIO::cleanup()
{
    if (_mapped_memory && _mapped_memory != MAP_FAILED)
    {
        // Unmap the memory
        size_t unmap_size = _is_file_backed ? _file_size : _buffer_size;
        if (munmap(_mapped_memory, unmap_size) == -1)
        {
            std::cerr << "Warning: Failed to unmap memory: "
                      << strerror(errno) << std::endl;
        }
        _mapped_memory = nullptr;
    }

    if (_file_descriptor != -1)
    {
        // Close file descriptor if still open
        if (close(_file_descriptor) == -1)
        {
            std::cerr << "Warning: Failed to close file descriptor: "
                      << strerror(errno) << std::endl;
        }
        _file_descriptor = -1;
    }
}

// Memory map the file
void FileIO::mapFile()
{
    // Map the entire file into memory
    // PROT_READ: Read-only access (use PROT_READ | PROT_WRITE for writing)
    // MAP_PRIVATE: Private copy-on-write mapping
    // MAP_SHARED: Share modifications with other processes (for writing)
    _mapped_memory = mmap(nullptr, _file_size,
                          PROT_READ,   // Read-only
                          MAP_PRIVATE, // Private mapping
                          _file_descriptor, 0);

    if (_mapped_memory == MAP_FAILED)
    {
        throw std::runtime_error("Failed to map file to memory: " +
                                 std::string(strerror(errno)));
    }

    // We can close the file descriptor now - mmap keeps a reference
    // Closing doesn't unmap the memory
    if (close(_file_descriptor) == -1)
    {
        // Log warning but don't throw - mapping succeeded
        std::cerr << "Warning: Failed to close file descriptor: "
                  << strerror(errno) << std::endl;
    }
    _file_descriptor = -1;
}

FileIO::FileIO(const std::string &sourceFile) : _source_file(sourceFile)
{

    // Check if file exists and is accessible
    if (!isFileAvailable())
    {
        throw std::runtime_error("File not available: " + _source_file);
    }

    // Open the file
    if (!openFile())
    {
        throw std::runtime_error("Failed to open file: " + _source_file);
    }

    try
    {
        // Get file statistics (size, permissions, etc.)
        struct stat file_stats = getFileStatistics();
        _file_size = file_stats.st_size;

        if (_file_size == 0)
        {
            throw std::runtime_error("File is empty: " + _source_file);
        }

        // Memory map the file
        mapFile();

        // std::cout << "Successfully memory-mapped file: " << _source_file
        //           << " (" << _file_size << " bytes)" << std::endl;
    }
    catch (const std::exception &e)
    {
        // Cleanup on error
        if (_file_descriptor != -1)
        {
            close(_file_descriptor);
        }
        throw;
    }
}

// Destructor - ensures proper cleanup
FileIO::~FileIO()
{
    cleanup();
}
