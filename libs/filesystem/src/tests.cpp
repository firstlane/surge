#if 0
#include <filesystem>
namespace fs = std::filesystem;
#else
#include "filesystem/filesystem.h"
namespace fs = Surge::filesystem;
#endif

#include "catch2/catch2.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

namespace {
std::string temp_name()
{
   char temp_late[] = "/tmp/surge-tests-filesystem-XXXXXX";
   REQUIRE(mktemp(temp_late)[0]);
   return temp_late;
}

std::string temp_mkdir(mode_t mode)
{
   std::string name{temp_name()};
   REQUIRE(mkdir(name.c_str(), mode) == 0);
   return name;
}
} // anonymous namespace

// Less noise than with an exception matcher
#define REQUIRE_THROWS_FS_ERROR(expr, errcode) do { \
    try { expr; FAIL("Did not throw: " #expr); } \
    catch (fs::filesystem_error & e) { REQUIRE(e.code() == std::errc::errcode); } \
} while (0)

TEST_CASE("Filesystem", "[filesystem]")
{
   SECTION("Directory Iterators")
   {
      SECTION("Reports errors at construction")
      {
         std::error_code ec;
         const fs::path p{"/dev/null"};
         SECTION("directory_iterator")
         {
            REQUIRE_THROWS_FS_ERROR(fs::directory_iterator{p}, not_a_directory);
            fs::directory_iterator it{p, ec};
            REQUIRE(ec == std::errc::not_a_directory);
         }
         SECTION("recursive_directory_iterator")
         {
            REQUIRE_THROWS_FS_ERROR(fs::recursive_directory_iterator{p}, not_a_directory);
            fs::recursive_directory_iterator it{p, ec};
            REQUIRE(ec == std::errc::not_a_directory);
         }
      }

      SECTION("Reports errors during recursion")
      {
         fs::path p{temp_mkdir(0777)};
         fs::path denied{p / fs::path("denied")};
         fs::create_directories(denied);
         REQUIRE(chmod(denied.c_str(), 0) == 0);
         fs::recursive_directory_iterator it{p};
         REQUIRE_THROWS_FS_ERROR(++it, permission_denied);
         REQUIRE_THROWS_FS_ERROR(fs::remove_all(p), permission_denied);
         REQUIRE(fs::remove(denied));
         REQUIRE(fs::remove(p));
      }

      SECTION("Skips . and ..")
      {
         std::error_code ec;
         fs::path p{temp_mkdir(0777)};
         SECTION("directory_iterator")
         {
            REQUIRE(fs::directory_iterator{p} == fs::directory_iterator{});
            REQUIRE(fs::directory_iterator{p, ec} == fs::directory_iterator{});
            REQUIRE_FALSE(ec);
         }
         SECTION("directory_iterator")
         {
            REQUIRE(fs::recursive_directory_iterator{p} == fs::recursive_directory_iterator{});
            REQUIRE(fs::recursive_directory_iterator{p, ec} == fs::recursive_directory_iterator{});
            REQUIRE_FALSE(ec);
         }
         REQUIRE(fs::remove(p));
         REQUIRE_FALSE(fs::remove(p));
      }

      SECTION("Visits each directory entry exactly once")
      {
         std::vector<std::string> paths = {
             "dir/1_entry/1a.file",
             "dir/2_entries/2a.dir",
             "dir/2_entries/2b.file",
             "dir/3_entries/3a.dir",
             "dir/3_entries/3b.dir",
             "dir/3_entries/3c.file",
             "file.file",
         };

         std::set<std::string> filenames;
         std::transform(paths.begin(), paths.end(), std::inserter(filenames, filenames.begin()),
                        [](const auto& p) {
                           auto fn(fs::path(p).filename());
                           REQUIRE_FALSE(fn.empty());
                           return fn.native();
                        });
         REQUIRE(filenames.size() == paths.size());

         fs::path rootdir{temp_mkdir(0777)};
         for (auto& pp : paths)
         {
            fs::path p{rootdir / fs::path{pp}};
            if (p.extension().native() == ".file")
            {
               fs::path f{p};
               f.remove_filename();
               fs::create_directories(f);
               const std::ofstream of{p};
               REQUIRE(of.good());
            }
            else
            {
               REQUIRE(fs::create_directories(p) > 0);
               REQUIRE(fs::is_directory(p));
            }
         }

         const auto iterate = [&](auto& it) {
            REQUIRE(it->path().native() != "");
            for (const fs::path& p : it)
            {
               auto fit{filenames.find(p.filename().native())};
               if (fit != end(filenames))
                  filenames.erase(fit);
               if (p.extension().native() == ".file")
               {
                  REQUIRE(fs::is_regular_file(p));
                  REQUIRE_FALSE(fs::is_directory(p));
               }
               else
               {
                  REQUIRE_FALSE(fs::is_regular_file(p));
                  REQUIRE(fs::is_directory(p));
               }
            }
         };

         SECTION("directory_iterator")
         {
            for (auto& pp : paths)
            {
               fs::path dir{(rootdir / fs::path{pp}).remove_filename()};
               fs::directory_iterator it{dir};
               iterate(it);
            }
         }

         SECTION("recursive_directory_iterator")
         {
            fs::recursive_directory_iterator it{rootdir};
            iterate(it);
         }

         REQUIRE(fs::remove_all(rootdir) == 12);
         REQUIRE(filenames.empty());
      }
   }

   SECTION("Operations")
   {
      SECTION("create_directories")
      {
         REQUIRE_THROWS_FS_ERROR(fs::create_directories(fs::path{"/dev/null"}), file_exists);
         REQUIRE_THROWS_FS_ERROR(fs::create_directories(fs::path{"/dev/null/dir"}), not_a_directory);

         const fs::path basep{temp_name()};
         REQUIRE_FALSE(fs::exists(basep));
         const fs::path p{basep / fs::path("this/is/a/test")};
         REQUIRE(fs::create_directories(p));
         REQUIRE(fs::is_directory(p));
         REQUIRE(fs::create_directories(p) == 0);
         REQUIRE(fs::remove_all(basep) == 5);
         REQUIRE_FALSE(fs::exists(basep));
         REQUIRE(fs::remove_all(basep) == 0);
         REQUIRE_FALSE(fs::is_directory(p));
      }

      SECTION("create_directory")
      {
         REQUIRE_FALSE(fs::create_directory(fs::path{"/dev/null"}));
         REQUIRE_THROWS_FS_ERROR(fs::create_directory(fs::path{"/dev/null/dir"}), not_a_directory);

         const fs::path p{temp_name()};
         REQUIRE_FALSE(fs::exists(p));
         REQUIRE(fs::create_directory(p));
         REQUIRE_FALSE(fs::create_directory(p));
         REQUIRE(fs::remove(p));
         REQUIRE_FALSE(fs::exists(p));
         REQUIRE_FALSE(fs::remove(p));
         REQUIRE_FALSE(fs::is_directory(p));
      }

      SECTION("exists")
      {
         REQUIRE(fs::exists(fs::path{"."}));
         const fs::path p{temp_mkdir(0)};
         REQUIRE_THROWS_FS_ERROR(fs::exists(p / fs::path{"file"}), permission_denied);
         REQUIRE(fs::remove(p));
         REQUIRE_FALSE(fs::remove(p));
      }

      SECTION("file_size")
      {
         REQUIRE_THROWS_FS_ERROR(fs::file_size(fs::path{"."}), is_a_directory);
         REQUIRE_THROWS_FS_ERROR(fs::file_size(fs::path{"/dev/null"}), not_supported);

         const fs::path p{temp_name()};
         const char testdata[] = "testdata";
         std::ofstream of{p};
         REQUIRE(of.good());
         REQUIRE(fs::file_size(p) == 0);
         of << testdata;
         of.close();
         REQUIRE(fs::file_size(p) == sizeof(testdata) - 1);
         REQUIRE(fs::remove(p));
         REQUIRE_FALSE(fs::remove(p));
      }

      SECTION("is_directory")
      {
         REQUIRE(fs::is_directory(fs::path{"."}));
         REQUIRE(fs::is_directory(fs::path{"/"}));
         REQUIRE_FALSE(fs::is_directory(fs::path{"/dev/null"}));
         REQUIRE_FALSE(fs::is_directory(fs::path{"/dev/null/dir"}));
      }

      SECTION("is_regular_file")
      {
         const fs::path p{temp_name()};
         const std::ofstream of{p};
         REQUIRE(of.good());
         REQUIRE(fs::is_regular_file(p));
         REQUIRE(fs::remove(p));
         REQUIRE_FALSE(fs::remove(p));

         REQUIRE_FALSE(fs::is_regular_file(fs::path{"."}));
         REQUIRE_FALSE(fs::is_regular_file(fs::path{"/"}));
         REQUIRE_FALSE(fs::is_regular_file(fs::path{"/dev/null"}));
         REQUIRE_FALSE(fs::is_regular_file(fs::path{"/dev/null/file"}));
      }

      SECTION("remove")
      {
         {
            const fs::path p{temp_name()};
            const std::ofstream of{p};
            REQUIRE(of.good());
            REQUIRE(fs::remove(p));
            REQUIRE_FALSE(fs::remove(p));
         }
         {
            const fs::path p{temp_mkdir(0777)};
            REQUIRE(fs::remove(p / fs::path("")));
            REQUIRE_FALSE(fs::remove(p / fs::path("")));
         }
         {
            const fs::path p{temp_mkdir(0777)};
            REQUIRE(fs::create_directories(p / fs::path{"dir"}));
            REQUIRE_THROWS_FS_ERROR(fs::remove(p), directory_not_empty);
            REQUIRE(fs::remove_all(p) == 2);
         }
      }

      SECTION("remove_all")
      {
         const fs::path p{temp_name()};
         const std::ofstream of{p};
         REQUIRE(of.good());
         REQUIRE(fs::remove_all(p) == 1);
         REQUIRE(fs::remove_all(p) == 0);
      }
   }

   SECTION("Path")
   {
      SECTION("operator /=, /")
      {
         REQUIRE((fs::path("foo") / fs::path("/bar")).native() == "/bar");
         REQUIRE((fs::path("foo") /= fs::path("/bar")).native() == "/bar");
         REQUIRE((fs::path("foo") / fs::path()).native() == "foo/");
         REQUIRE((fs::path("foo") /= fs::path()).native() == "foo/");
         REQUIRE((fs::path("foo") / fs::path("bar")).native() == "foo/bar");
         REQUIRE((fs::path("foo") /= fs::path("bar")).native() == "foo/bar");
         REQUIRE((fs::path("foo/") / fs::path("bar")).native() == "foo/bar");
         REQUIRE((fs::path("foo/") /= fs::path("bar")).native() == "foo/bar");
      }

      SECTION("remove_filename")
      {
         // https://en.cppreference.com/w/cpp/filesystem/path/remove_filename
         REQUIRE(fs::path{"foo/bar"}.remove_filename().native() == "foo/");
         REQUIRE(fs::path{"foo/"}.remove_filename().native() == "foo/");
         REQUIRE(fs::path{"/foo"}.remove_filename().native() == "/");
         REQUIRE(fs::path{"/"}.remove_filename().native() == "/");
         REQUIRE(fs::path{}.remove_filename().native() == "");
      }

      SECTION("filename, has_filename")
      {
         REQUIRE(fs::path().filename().native() == "");
         REQUIRE_FALSE(fs::path().has_filename());

         // https://en.cppreference.com/w/cpp/filesystem/path/filename
         REQUIRE(fs::path("/foo/bar.txt").filename().native() == "bar.txt");
         REQUIRE(fs::path("/foo/bar.txt").has_filename());

         REQUIRE(fs::path("/foo/.bar").filename().native() == ".bar");
         REQUIRE(fs::path("/foo/.bar").has_filename());

         REQUIRE(fs::path("/foo/bar/").filename().native() == "");
         REQUIRE_FALSE(fs::path("/foo/bar/").has_filename());

         REQUIRE(fs::path("/foo/.").filename().native() == ".");
         REQUIRE(fs::path("/foo/.").has_filename());

         REQUIRE(fs::path("/foo/..").filename().native() == "..");
         REQUIRE(fs::path("/foo/..").has_filename());

         REQUIRE(fs::path(".").filename().native() == ".");
         REQUIRE(fs::path(".").has_filename());

         REQUIRE(fs::path("..").filename().native() == "..");
         REQUIRE(fs::path("..").has_filename());

         REQUIRE(fs::path("/").filename().native() == "");
         REQUIRE_FALSE(fs::path("/").has_filename());

         REQUIRE(fs::path("//host").filename().native() == "host");
         REQUIRE(fs::path("//host").has_filename());
      }

      SECTION("stem, has_stem")
      {
         REQUIRE(fs::path().stem().native() == "");
         REQUIRE_FALSE(fs::path().has_stem());

         REQUIRE(fs::path(".").stem().native() == ".");
         REQUIRE(fs::path(".").has_stem());

         REQUIRE(fs::path("..").stem().native() == "..");
         REQUIRE(fs::path("..").has_stem());

         REQUIRE(fs::path("...").stem().native() == "..");
         REQUIRE(fs::path("...").has_stem());

         // https://en.cppreference.com/w/cpp/filesystem/path/stem
         REQUIRE(fs::path("/foo/bar.txt").stem().native() == "bar");
         REQUIRE(fs::path("/foo/bar.txt").has_stem());

         REQUIRE(fs::path("/foo/.bar").stem().native() == ".bar");
         REQUIRE(fs::path("/foo/.bar").has_stem());

         REQUIRE(fs::path("foo.bar.baz.tar").stem().native() == "foo.bar.baz");
         REQUIRE(fs::path("foo.bar.baz.tar").has_stem());
      }

      SECTION("extension, has_extension")
      {
         REQUIRE(fs::path().extension().native() == "");
         REQUIRE_FALSE(fs::path().has_extension());

         // https://en.cppreference.com/w/cpp/filesystem/path/extension
         REQUIRE(fs::path("/foo/bar.txt").extension().native() == ".txt");
         REQUIRE(fs::path("/foo/bar.txt").has_extension());

         REQUIRE(fs::path("/foo/bar.").extension().native() == ".");
         REQUIRE(fs::path("/foo/bar.").has_extension());

         REQUIRE(fs::path("/foo/bar").extension().native() == "");
         REQUIRE_FALSE(fs::path("/foo/bar").has_extension());

         REQUIRE(fs::path("/foo/bar.txt/bar.cc").extension().native() == ".cc");
         REQUIRE(fs::path("/foo/bar.txt/bar.cc").has_extension());

         REQUIRE(fs::path("/foo/bar.txt/bar.").extension().native() == ".");
         REQUIRE(fs::path("/foo/bar.txt/bar.").has_extension());

         REQUIRE(fs::path("/foo/bar.txt/bar").extension().native() == "");
         REQUIRE_FALSE(fs::path("/foo/bar.txt/bar").has_extension());

         REQUIRE(fs::path("/foo/.").extension().native() == "");
         REQUIRE_FALSE(fs::path("/foo/.").has_extension());

         REQUIRE(fs::path("/foo/..").extension().native() == "");
         REQUIRE_FALSE(fs::path("/foo/..").has_extension());

         REQUIRE(fs::path("/foo/.hidden").extension().native() == "");
         REQUIRE_FALSE(fs::path("/foo/.hidden").has_extension());

         REQUIRE(fs::path("/foo/..bar").extension().native() == ".bar");
         REQUIRE(fs::path("/foo/..bar").has_extension());
      }

      SECTION("is_absolute, is_relative")
      {
         REQUIRE_FALSE(fs::path("").is_absolute());
         REQUIRE(fs::path("").is_relative());
         REQUIRE(fs::path("/").is_absolute());
         REQUIRE_FALSE(fs::path("/").is_relative());
         REQUIRE(fs::path("/dir/").is_absolute());
         REQUIRE_FALSE(fs::path("/dir/").is_relative());
         REQUIRE(fs::path("/file").is_absolute());
         REQUIRE_FALSE(fs::path("/file").is_relative());
         REQUIRE_FALSE(fs::path("file").is_absolute());
         REQUIRE(fs::path("file").is_relative());
      }
   }
}