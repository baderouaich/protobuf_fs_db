#pragma once

#include <fcntl.h> // for lock/unlock mutex file
#include <unistd.h>
#include <sys/file.h>
#include <filesystem>
namespace fs = std::filesystem;

struct MutexFile  {
  fs::path filename;
  int fd{};

#ifndef NDEBUG
  inline static uint64_t numLocks = 0;
#endif
  explicit MutexFile(const fs::path& pfilename) : filename(pfilename)
  {

  }

  void lock()  {
#ifndef NDEBUG
    numLocks++;
#endif
    if(isAlreadyLockedByMe()) return;

    fd = open(filename.string().c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
      throw std::runtime_error("Failed to open/create mutex file " + filename.string());
    }

    struct flock flk{};
    flk.l_type = F_WRLCK; // Write lock
    flk.l_pid = getpid();
    flk.l_whence = SEEK_SET;
    flk.l_start = 0;
    flk.l_len = 0;
    /*
#  define F_GETLK	5	// Get record locking info.
#  define F_SETLK	6	// Set record locking info (non-blocking).
#  define F_SETLKW	7	// Set record locking info (blocking).
     */
    if(const int ret = fcntl(fd, F_SETLKW, &flk); ret < 0) {
      std::cerr << "ret: " << ret << ": " << strerror(errno) << std::endl;
      throw std::runtime_error("Failed to set exclusive lock " + filename.string());
    }
  }

  bool isAlreadyLockedByMe() const {
    struct flock flk{};
    flk.l_type = F_RDLCK;
    flk.l_whence = SEEK_SET;
    flk.l_start = 0;
    flk.l_len = 0;
    if(const int ret = fcntl(fd, F_GETLK, &flk); ret < 0) {
      std::cerr << "ret: " << ret << ": " << strerror(errno) << std::endl;
      throw std::runtime_error("Failed to get lock info " + filename.string());
    }
    return flk.l_pid == getpid();
  }

  void unlock() const  {

#ifndef NDEBUG
    numLocks--;
#endif
    if(!isAlreadyLockedByMe()) return;

    struct flock flk{};
    flk.l_type = F_UNLCK; // Remove lock
    flk.l_pid = getpid();
    flk.l_whence = SEEK_SET;
    flk.l_start = 0;
    flk.l_len = 0;
    /*
#  define F_GETLK	5	// Get record locking info.
#  define F_SETLK	6	// Set record locking info (non-blocking).
#  define F_SETLKW	7	// Set record locking info (blocking).
     */
    if(const int ret = fcntl(fd, F_SETLKW, &flk); ret < 0) {
      std::cerr << "ret: " << ret << ": " << strerror(errno) << std::endl;
      close(fd);
      throw std::runtime_error("Failed to unset exclusive lock " + filename.string());
    }
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