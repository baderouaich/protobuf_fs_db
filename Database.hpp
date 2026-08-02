#pragma once
#include <iostream>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <typeindex>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include "MutexFile.hpp"
#include "sha256/sha256.h"
namespace fs = std::filesystem;

namespace utils {
  template<typename T>
  static T sto(const std::string& str) {
    if constexpr (std::is_same_v<T, std::string>) return str; // if not necessary
    T v{};
    std::istringstream iss{str};
    iss >> v;
    return v;
  }

  template<typename In>
  static constexpr std::string to_string(const In& in) {
    if constexpr (std::is_same_v<In, std::string>) return in; // if not necessary

    std::ostringstream oss{};
    oss << in;
    return oss.str();
  }
}

class Database {
  fs::path dbDir;
  std::unordered_map<std::type_index, std::string> typeDirNames;

  MutexFile mutexFile; // for multiple processes accessing the database
  std::shared_mutex mutex{}; // for multithreading

  struct DatabaseRAIILocker {
    Database& db;
    bool readonly{false};

    explicit DatabaseRAIILocker(Database& pdb, bool preadonly = false) : db(pdb), readonly(preadonly){
      if(readonly) {
        db.mutex.lock_shared();
      }
      else  {
        db.mutexFile.lock();
        db.mutex.lock();
      }
      //std::cout << "Database Locked" << std::endl;
    }

    ~DatabaseRAIILocker() {
      if(readonly) {
        db.mutex.unlock_shared();
      }
      else {
        db.mutexFile.unlock();
        db.mutex.unlock();
      }
      //std::cout << "Database Unlocked" << std::endl;
    }
  };

public:
  explicit Database(const fs::path& pdbDir) : dbDir(pdbDir),
                                              mutexFile(dbDir / "mutex") {
    if(!fs::is_directory(dbDir))
      fs::create_directories(dbDir);
  }

  template<typename T>
  void typeDirName(const std::string& name) {
    typeDirNames[typeid(T)] = name;
  }

  template<typename T>
  std::string typeDirName() const {
    const std::type_index idx = typeid(T);
    if(typeDirNames.contains(idx)) return typeDirNames.at(idx);
    return T{}.GetTypeName();
  }

  template<typename T>
  void add(const T& obj, bool overwrite = false)
  {
    DatabaseRAIILocker locker{*this, false};
    this->_add<T>(obj, overwrite);
  }

  template<typename T>
  void update(const T& obj)
  {
    DatabaseRAIILocker locker{*this, false};
    this->_update<T>(obj);
  }


  template<typename T, typename ID>
  std::optional<T> get(const ID& id) {
    DatabaseRAIILocker locker{*this, true};
    return this->_get<T, ID>(id);
  }


  template<typename T, typename ID>
  bool exists(const ID& id) {
    DatabaseRAIILocker locker{*this, true};
    return this->_exists<T, ID>(id);
  }

  template<typename T>
  std::size_t count() {
    DatabaseRAIILocker locker{*this, true};
    return this->_count<T>();
  }

  template<typename T>
  std::vector<T> all() {
    DatabaseRAIILocker locker{*this, true};
    return this->_all<T>();
  }

  // TODO: iterating over objects in such way:
  // for(const types::User& user : db.iterate<types::User>())
  // {
  //   ...
  // }
  // Unlike the all() method which loads all objects then returns them,
  // This should be iterative, load -> serve -> next.

  template<typename T, typename ID>
  bool remove(const ID& id)
  {
    DatabaseRAIILocker locker{*this, false};
    return this->_remove<T, ID>(id);
  }

  template<typename T>
  bool clear() {
    DatabaseRAIILocker locker{*this, false};
    return this->_clear<T>();
  }

  template<typename T, typename Predicate>
  std::optional<T> findIf(const Predicate& predicate) {
    DatabaseRAIILocker locker{*this, true};
    return this->_findIf<T, Predicate>(predicate);
  }

  template<typename T, typename Predicate>
  std::size_t countIf(const Predicate& predicate) {
    DatabaseRAIILocker locker{*this, true};
    return this->_countIf<T, Predicate>(predicate);
  }

  template<typename T, typename Predicate>
  bool removeIf(const Predicate& predicate) {
    DatabaseRAIILocker locker{*this, false};
    return this->_removeIf<T, Predicate>(predicate);
  }

  template<typename T = std::string>
  static std::string sha256(T input) {
    BYTE hash[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx{};
    sha256_init(&ctx);
    sha256_update(&ctx, reinterpret_cast<const BYTE*>(input.c_str()), input.size());
    sha256_final(&ctx, hash);

    std::ostringstream oss{};
    for (const unsigned char i : hash) {
      oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(i);
    }
    return oss.str();
  }
  template<typename T> requires std::is_arithmetic_v<T>
  static std::string sha256(T input) {
    return sha256(std::to_string(input));
  }

  template<const std::size_t Size>
  static fs::path shard_dir(const char(&id)[Size]) {
    return shard_dir(std::string(id));
    // return std::to_string(std::hash<std::string>()(std::string(id))).substr(0, 2);
  }

  template<typename T>
  static fs::path shard_dir(const T& id) {
    constexpr size_t kNumLayers = 3; // Users/layer1/layer2/layer3/user.bin
    std::string hash = sha256(id);
    fs::path shards;
    for (int i = 0; i < kNumLayers * 2; i += 2) {
      shards /= hash.substr(i, 2);
    }
    return shards;
  }
private:
  template<typename T>
  void _add(const T& obj, bool overwrite = false)
  {
    const fs::path objDir = dbDir / typeDirName<T>() / shard_dir(obj.id());
    if(!fs::is_directory(objDir))
      fs::create_directories(objDir);

    const fs::path objFilename = objDir / utils::to_string(obj.id());
    if(fs::exists(objFilename) && !overwrite)
    {
      throw std::logic_error("Object file " + objFilename.string() + " already exists. Cannot overwrite");
    }

    if(std::ofstream ofs{objFilename, std::ios::binary})
    {
      obj.SerializeToOstream(&ofs);
      ofs.flush();
      ofs.close();
    } else {
      throw std::runtime_error("Could not open file " + objFilename.string());
    }

  }

  template<typename T>
  void _update(const T& obj) {
    _add<T>(obj, true);
  }

  template<typename T, typename ID>
  std::optional<T> _get(const ID& id) {
    const fs::path objDir = dbDir / typeDirName<T>() / shard_dir(id);
    if(!fs::is_directory(objDir))
      //throw std::logic_error("Db is empty");
      return std::nullopt;

    const fs::path objFilename = objDir / utils::to_string(id);
    if(!fs::exists(objFilename)){
      //throw std::logic_error("Object file " + objFilename.string() + " does not exists");
      return std::nullopt;
    }

    if(std::ifstream ifs{objFilename, std::ios::binary}) {
      T v{};
      v.ParseFromIstream(&ifs);
      ifs.close();
      return v;
    } else {
      //throw std::runtime_error("Could not open file " + objFilename.string());
      return std::nullopt;
    }
  }

  template<typename T, typename ID>
  bool _exists(const ID& id) {
    const fs::path objDir = dbDir / typeDirName<T>() / shard_dir(id);
    const fs::path objFilename = objDir / utils::to_string(id);
    return fs::exists(objFilename);
  }

  template<typename T>
  std::size_t _count() {
    const fs::path objDir = dbDir / typeDirName<T>() ;
    std::size_t c{0};
    for([[maybe_unused]] const auto& it : fs::recursive_directory_iterator(objDir)) {
      c += it.is_regular_file();
    }
    return c;
  }

  template<typename T>
  std::vector<T> _all() {
    std::vector<T> types{};
    types.reserve(_count<T>()); // note we're using _count() and not count() to not lock db twice.
    const fs::path objDir = dbDir / typeDirName<T>();
    for(const auto& it : fs::recursive_directory_iterator(objDir)) {
      if (not it.is_regular_file()) continue;
      const auto id = utils::sto<std::remove_cvref_t<decltype(std::declval<T>().id())>>(it.path().filename().string());
      types.emplace_back(*_get<T>(id)); // note we're using _get() and not get() to not lock db twice.
    }
    return types;
  }

  template<typename T, typename ID>
  bool _remove(const ID& id)
  {
    const fs::path objDir = dbDir / typeDirName<T>() / shard_dir(id);
    if(!fs::exists(objDir))
      fs::create_directories(objDir);

    const fs::path objFilename = objDir / utils::to_string(id);
    if(!fs::exists(objFilename)) return true;

    return fs::remove(objFilename);
  }

  template<typename T>
  bool _clear() {
    const fs::path objDir = dbDir / typeDirName<T>();
    if(!fs::exists(objDir)) return true;

    std::error_code ec{};
    fs::remove_all(objDir, ec);
    return !ec;
  }

  template<typename T, typename Predicate>
  std::optional<T> _findIf(const Predicate& predicate) {
    const fs::path objDir = dbDir / typeDirName<T>();
    for(const auto& it : fs::recursive_directory_iterator(objDir)) {
      if (!it.is_regular_file()) continue;
      const auto id = utils::sto<std::remove_cvref_t<decltype(std::declval<T>().id())>>(it.path().filename().string());
      std::optional<T> obj = _get<T>(id); // note we're using _get() and not get() to not lock db twice.
      if(predicate(*obj)) {
        return obj;
      }
    }
    return std::nullopt;
  }

  template<typename T, typename Predicate>
  std::size_t _countIf(const Predicate& predicate) {
    std::size_t count{0};
    const fs::path objDir = dbDir / typeDirName<T>();
    for(const auto& it : fs::recursive_directory_iterator(objDir)) {
      if (!it.is_regular_file()) continue;
      const auto id = utils::sto<std::remove_cvref_t<decltype(std::declval<T>().id())>>(it.path().filename().string());
      std::optional<T> obj = _get<T>(id); // note we're using _get() and not get() to not lock db twice.
      if(predicate(*obj)) {
        ++count;
      }
    }
    return count;
  }

  template<typename T, typename Predicate>
  bool _removeIf(const Predicate& predicate) {
    const fs::path objDir = dbDir / typeDirName<T>();
    bool ok = true;
    for(const auto& it : fs::recursive_directory_iterator(objDir)) {
      if (!it.is_regular_file())continue;
      const auto id = utils::sto<std::remove_cvref_t<decltype(std::declval<T>().id())>>(it.path().filename().string());
      std::optional<T> obj = _get<T>(id); // note we're using _get() and not get() to not lock db twice.
      if(predicate(*obj)) {
        ok &= _remove<T>(id);
      }
    }
    return ok;
  }

};
