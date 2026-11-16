#include "../src/migration.hpp"
#include <cstdio>
#include "../src/profile.hpp"
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) { std::fputs("Pass a new isolated fixture directory.\n",stderr);return 2; }
    auto root=std::filesystem::path(argv[1]);bool passed=lb::migration::tests(root/L"migration");passed=lb::profile::tests(root/L"profile")&&passed;
    std::puts(passed?"{\"migration_fixtures\":true}":"{\"migration_fixtures\":false}");
    return passed?0:1;
}
