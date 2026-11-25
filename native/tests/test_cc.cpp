// Control Center helpers without a Virus: log anonymizing (no home paths / user names in collected logs), log tails,
// patch names, .syx export
#include "cc/controlcenter.h"
#include "cc/diag.h"
#include "device/broker.h"
#include "check.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace vti::cc;
using vti::Dump;
namespace fs = std::filesystem;

namespace {
std::string env(const char *name)
{
    const char *v = getenv(name);
    return v ? v : "";
}
fs::path scratch()
{
    fs::path d = fs::temp_directory_path() / "vtitests-cc";
    fs::create_directories(d);
    return d;
}
} // namespace

TEST(cc_anonymize_home_and_user)
{
#ifdef _WIN32
    const std::string home = env("USERPROFILE"), user = env("USERNAME");
#else
    const std::string home = env("HOME"), user = env("USER");
#endif
    if (home.size() < 2 || user.size() < 3)
        SKIP("no home folder / user name in the environment");
    std::string other = home; // the same path with the other kind of slashes (Wine, logs from both worlds)
    for (auto &c : other)
        c = c == '\\' ? '/' : c == '/' ? '\\' : c;
    std::string s = anonymize("cache " + home + "/x.bin, again " + other + "\\y, user " + user + ".");
    CHECK(s.find(home) == std::string::npos);
    CHECK(s.find(other) == std::string::npos);
    CHECK(s.find(user) == std::string::npos);
    CHECK(s.find("~/x.bin") != std::string::npos && s.find("<user>.") != std::string::npos);
    CHECK(anonymize("nothing personal here") == "nothing personal here");
}

TEST(cc_tail_file)
{
    const fs::path d = scratch();
    CHECK(tail_file((d / "missing.log").string(), 5).empty());
    {
        std::ofstream o(d / "short.log", std::ios::binary);
        o << "one\r\ntwo\nthree\r\nfour\n";
    }
    auto t = tail_file((d / "short.log").string(), 2);
    CHECK(t.size() == 2 && t[0] == "three" && t[1] == "four"); // the Windows line ends are gone
    CHECK_EQ(tail_file((d / "short.log").string(), 10).size(), 4u);
    {
        std::ofstream o(d / "long.log", std::ios::binary); // longer than the part tail_file reads (n * 400 + 4 KB)
        for (int i = 0; i < 2000; i++)
            o << "line " << i << " " << std::string(30, 'x') << "\n";
    }
    t = tail_file((d / "long.log").string(), 3);
    CHECK(t.size() == 3 && t[2].rfind("line 1999 ", 0) == 0 && t[0].rfind("line 1997 ", 0) == 0);
    fs::remove_all(d);
}

TEST(cc_patch_name)
{
    CHECK(patch_name(Dump(100, 0)).empty()); // too short for a name
    Dump d(524, 0);
    const char name[] = "Pad 1     ";
    std::copy(name, name + 10, d.begin() + 249);
    CHECK(patch_name(d) == "Pad 1"); // trailing spaces trimmed
    const char full[] = "TenLetters";
    std::copy(full, full + 10, d.begin() + 249);
    CHECK(patch_name(d) == "TenLetters");
}

TEST(cc_write_syx)
{
    const fs::path d = scratch();
    const std::string path = (d / "bank.syx").string();
    std::string err;
    CHECK(write_syx({Dump{0xF0, 0x01, 0xF7}, Dump{0xF0, 0x02, 0x03, 0xF7}}, path, err));
    CHECK(fs::file_size(path) == 7 && !fs::exists(path + ".tmp"));
    {
        std::ifstream i(path, std::ios::binary);
        std::string b((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
        CHECK(b == std::string("\xF0\x01\xF7\xF0\x02\x03\xF7", 7));
    }
    CHECK(write_syx({}, path, err) && fs::file_size(path) == 0); // replaces the file
    err.clear();
    CHECK(!write_syx({Dump{0xF0, 0xF7}}, (d / "no-such-dir" / "x.syx").string(), err));
    CHECK(!err.empty());
    fs::remove_all(d);
}

TEST(cc_write_syx_failure_keeps_the_file) // review 2026-11-25 E9: a full disk fails only at the flush (close)
{
#ifndef _WIN32
    std::error_code ec;
    if (!fs::exists("/dev/full", ec))
        SKIP("no /dev/full");
    const fs::path d = scratch() / "full";
    fs::remove_all(d);
    fs::create_directories(d);
    const std::string path = (d / "bank.syx").string();
    std::string err;
    CHECK(write_syx({Dump{0xF0, 0x01, 0xF7}}, path, err));
    fs::create_symlink("/dev/full", path + ".tmp");
    CHECK(!write_syx({Dump{0xF0, 0x02, 0xF7}}, path, err) && !err.empty());
    // not a symlink first: reading /dev/full never ends
    CHECK(!fs::is_symlink(path) && fs::file_size(path) == 3);
    CHECK(!fs::exists(fs::symlink_status(path + ".tmp")));
    fs::remove_all(d);
#endif
}

TEST(cc_cache_dir_like_the_plugin)
{
    std::string c = cache_dir();
    if (c.empty())
        SKIP("no home folder in the environment");
    CHECK(c.size() > 22 && c.substr(c.size() - 22) == "/virus-ti-native/cache");
}

TEST(cc_backup_folder_without_permission)
{
#ifndef _WIN32
    // macOS user test M2 (0.8.19): the Access installer had created Documents/Access Music as root, and the backup
    // only said "permission denied". The message names the way out; nothing reaches the broker before that.
    if (geteuid() == 0)
        return; // root may write anywhere
    const fs::path d = scratch() / "locked";
    fs::create_directories(d);
    fs::permissions(d, fs::perms::owner_read | fs::perms::owner_exec);
    vti::Broker broker;
    std::string err;
    CHECK(!backup_ram(broker, "1", (d / "Backups" / "2026-10-17 0628").string(), nullptr, err));
    CHECK(err.find("cannot create") != std::string::npos);
    CHECK(err.find("Choose another folder") != std::string::npos);
    fs::permissions(d, fs::perms::owner_all);
    fs::remove_all(scratch());
#endif
}
