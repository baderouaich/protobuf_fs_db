#include <iostream>
#include "User.pb.h"
#include "Download.pb.h"
#include "Setting.pb.h"
#include <thread>
#include <filesystem>
#include <optional>
#include "Database.hpp"

namespace fs = std::filesystem;

#ifndef DB_DIR // Just in case. CMake will define it.
#define DB_DIR "./Database"
#endif

int main() {
  // so each object = file
  // each object has (must have) ID, and file path can easily be accessed
  // by id, so if id is: User{id = 100, ...}
  // file will be at: Database/User/100
  // for fast indexing.

  Database db(DB_DIR);

  // Optionally set directory name where user objects will be saved (default: types.User)
  db.typeDirName<types::User>("Users");
  db.typeDirName<types::Download>("Downloads");
  db.typeDirName<types::Setting>("Settings");

  // Clear users & downloads objects
  db.clear<types::User>();
  db.clear<types::Download>();
  db.clear<types::Setting>();

  // Test adding new users
  assert(MutexFile::numLocks == 0);

  types::User user1;
  user1.set_id(1000);
  user1.set_name("James");
  user1.set_weight(83.15);
  db.add(user1, true);
  assert(db.exists<types::User>(user1.id()));
  assert(MutexFile::numLocks == 0);

  types::User user2;
  user2.set_id(2000);
  user2.set_name("Olga");
  user2.set_weight(62.00);
  assert(MutexFile::numLocks == 0);
  db.add(user2, true);
  assert(MutexFile::numLocks == 0);
  assert(db.exists<types::User>(user2.id()));
  assert(MutexFile::numLocks == 0);
  assert(db.count<types::User>() == db.all<types::User>().size());
  assert(MutexFile::numLocks == 0);
  assert(db.count<types::User>() == 2);
  assert(MutexFile::numLocks == 0);
  assert(db.remove<types::User>(1000));
  assert(MutexFile::numLocks == 0);
  assert(db.remove<types::User>(1000));
  assert(MutexFile::numLocks == 0);
  assert(db.count<types::User>() == 1);
  assert(MutexFile::numLocks == 0);


  types::Setting setting{};
  setting.set_id("favorite_game");
  setting.set_value("cs1.6");
  setting.set_userid(user1.id());
  db.add(setting, true);
  assert(db.get<types::Setting>("favorite_game").value() == "cs1.6");

  // Test adding new downloads
  types::Download download;
  download.set_id(3000);
  download.set_userid(user1.id()); // owner
  download.set_timestamp(std::time(nullptr));
  download.set_url("https://youtube.com/some/video");
  download.set_size(1024*1024*500);
  download.set_success(true);
  db.add(download, true);
  assert(db.exists<types::Download>(download.id()));
  assert(db.count<types::Download>() == 1);
  assert(MutexFile::numLocks == 0);

  std::cout << db.count<types::User>()  << " users\n";
  std::cout << db.count<types::Download>()  << " downloads\n";
  std::cout << db.get<types::User>(2000).name() << std::endl;
  if(db.exists<types::Download>(21)) {
    auto down = db.get<types::Download>(21);
    std::unreachable(); // no user exist with that id 21 (yet)
  }
  std::cout << db.get<types::Download>(3000).url() << std::endl;
  assert(MutexFile::numLocks == 0);

  std::cout << "All: " << std::endl;
  for(const types::User& user : db.all<types::User>()) {
    std::cout << user.id() << ": " << user.name() << std::endl;
  }
  assert(MutexFile::numLocks == 0);

  std::cout << "Find user with weight > 50:\n";
  std::optional<types::User> u = db.findIf<types::User>([](const types::User& u) -> bool {
    return u.weight() > 50.0;
  });
  if(u) {
    std::cout << "Found user with > 50 weight: " << u->name() << std::endl;
  }  else {
    std::cout << "No user was found with > 50 weight" << std::endl;
  }
  assert(MutexFile::numLocks == 0);

  struct ScopeBenchmark {
    explicit ScopeBenchmark(std::string  name)
      :
      start(std::chrono::system_clock::now()),
      name(std::move(name))
    {

    }
    ~ScopeBenchmark(){
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
      std::cout << "Benchmark <"<<name<<"> took " << ms << "ms\n";
    }
    std::chrono::system_clock::time_point start;
    std::string name;
  };

  {
    std::srand(std::time(nullptr));
    ScopeBenchmark _{"db.add 100 users"};
    for (int i = 0; i < 100; i++) {
      types::User user;
      user.set_id(i);
      user.set_name("User#" + std::to_string(user.id()));
      user.set_weight((std::rand() % 100) /((std::rand() % 50) + 1));
      db.add(user, true);
    }
  }
  assert(MutexFile::numLocks == 0);


  {
    ScopeBenchmark _{"db.findIf id == 50 in users"};
    auto user = db.findIf<types::User>([](const types::User& user)
    {
      return user.id() == 50;
    });
    assert(user.has_value());
  }
  assert(MutexFile::numLocks == 0);

  // Test multithreading
  std::vector<std::jthread> threads;
  for(int i = 0; i < 7; i++) {
    threads.emplace_back([&](){
      for(int j = 0; j < 10; ++j) {
        try {
          std::cout << "tid[" << std::this_thread::get_id() << "] job " << j << std::endl;

          auto user = db.findIf<types::User>([](const types::User &user) {
            return user.id() == 99;
          });
          assert(user.has_value());


          types::Download download;
          download.set_id(i + j);
          download.set_userid(rand() % 10000); // owner
          download.set_timestamp(std::time(nullptr));
          download.set_url("https://youtube.com/some/video");
          download.set_size(1024 * 1024 * (rand() % 1025));
          download.set_success(!!(rand() % 2));
          db.add(download, true);
          assert(db.exists<types::Download>(download.id()));

          std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 1000));
        }
        catch(const std::exception& err){
          std::cerr << "tid ["<<std::this_thread::get_id() <<"] error: " << err.what() << std::endl;
        }
      }
    });
  }

  for(auto& t : threads) {
    if(t.joinable())
      t.join();
  }

  assert(MutexFile::numLocks == 0);
}
