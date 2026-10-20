#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fa {

// Read-only view of a file backed by the OS paging (mmap/MapViewOfFile), so
// opening a 4 GiB archive does not read it into memory. The mapping is released
// on destruction; the file is not copied.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    bool open(const std::filesystem::path& path, std::string& error);

    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }

private:
    void close();

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
#if defined(_WIN32)
    void* file_ = nullptr;     // HANDLE
    void* mapping_ = nullptr;  // HANDLE
#else
    int fd_ = -1;
#endif
};

}  // namespace fa
