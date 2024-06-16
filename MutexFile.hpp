#pragma once

#include <fcntl.h> // for lock/unlock mutex file
#include <unistd.h>
#include <sys/file.h>
#include <filesystem>
namespace fs = std::filesystem;

struct MutexFile  {
  fs::path filename;
  int fd{};

  explicit MutexFile(const fs::path& pfilename) : filename(pfilename)
  {

  }

  void lock()  {
    fd = open(filename.string().c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
      throw std::runtime_error("Failed to lock mutex file " + filename.string());
    }

    struct flock flk{};
    flk.l_type = F_WRLCK;
    fcntl(fd, F_SETLKW, &flk);
  }

  void unlock() const  {
    struct flock flk{};
    flk.l_type = F_UNLCK;
    fcntl(fd, F_SETLKW, &flk);
    close(fd);
  }

};
#if 0
struct MutexFile {
  fs::path filename;
  int fd{};

  explicit MutexFile(const fs::path& pfilename) : filename(pfilename) {
  }

  void lock() {
    fd = open(filename.string().c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
      throw std::runtime_error("Failed to open file " + filename.string());
    }
    if (flock(fd, LOCK_SH /*| LOCK_NB*/) < 0)
    {
      close(fd);
      throw std::runtime_error("Failed to lock file " + filename.string());
    }
  }

  void unlock() const noexcept {
    // Unlock the file
    flock(fd, LOCK_UN);
    // close fd
    close(fd);
  }
};
#endif