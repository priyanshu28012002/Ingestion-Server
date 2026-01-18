#include <iostream>
#include <string>
#include <vector>
#include "fileio.hpp"

class URLLoader
{
private:
    FileIO *fileIO;
    std::vector<std::string> urls;

public:
    URLLoader(const std::string &filename);

    ~URLLoader();
    void loadURLs();
    const std::vector<std::string> &getURLs() const;

    void printURLs() const;
};