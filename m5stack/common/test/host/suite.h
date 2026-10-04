#pragma once

#include <cstdio>
#include <string>

class Suite {
public:
    explicit Suite(const char *name) : name_(name) {}

    template <typename Case>
    void run(const char *test, Case body)
    {
        const std::string why = body();
        if (why.empty()) {
            ++passed_;
            std::printf("ok   %s %s\n", name_, test);
        } else {
            ++failed_;
            std::printf("FAIL %s %s: %s\n", name_, test, why.c_str());
        }
    }

    bool finish() const
    {
        std::printf("%s: %d cases, %d passed, %d failed\n", name_, passed_ + failed_, passed_, failed_);
        std::fflush(stdout);
        return failed_ == 0 && passed_ > 0;
    }

private:
    const char *name_;
    int passed_ = 0;
    int failed_ = 0;
};
