#include "fa/mapped_file.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fa {

MappedFile::~MappedFile()
{
    close();
}

MappedFile::MappedFile(MappedFile&& other) noexcept
{
    *this = std::move(other);
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept
{
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
#if defined(_WIN32)
        file_ = other.file_;
        mapping_ = other.mapping_;
        other.file_ = nullptr;
        other.mapping_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void MappedFile::close()
{
#if defined(_WIN32)
    if (data_ != nullptr) UnmapViewOfFile(data_);
    if (mapping_ != nullptr) CloseHandle(static_cast<HANDLE>(mapping_));
    if (file_ != nullptr) CloseHandle(static_cast<HANDLE>(file_));
    mapping_ = nullptr;
    file_ = nullptr;
#else
    if (data_ != nullptr && size_ != 0) munmap(const_cast<std::uint8_t*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
#endif
    data_ = nullptr;
    size_ = 0;
}

bool MappedFile::open(const std::filesystem::path& path, std::string& error)
{
    close();

#if defined(_WIN32)
    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size)) {
        CloseHandle(file);
        error = "cannot determine size of '" + path.string() + "'";
        return false;
    }
    if (file_size.QuadPart == 0) {
        CloseHandle(file);
        error = "empty file";
        return false;
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        error = "cannot map '" + path.string() + "'";
        return false;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        error = "cannot map '" + path.string() + "'";
        return false;
    }
    file_ = file;
    mapping_ = mapping;
    data_ = static_cast<const std::uint8_t*>(view);
    size_ = static_cast<std::size_t>(file_size.QuadPart);
    return true;
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        error = "cannot determine size of '" + path.string() + "'";
        return false;
    }
    void* view = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        error = "cannot map '" + path.string() + "'";
        return false;
    }
    fd_ = fd;
    data_ = static_cast<const std::uint8_t*>(view);
    size_ = static_cast<std::size_t>(st.st_size);
    return true;
#endif
}

}  // namespace fa
