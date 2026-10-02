#pragma once

#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

struct Span {
    size_t begin = 0;
    size_t end = 0;
};

class RawScanner {
public:
    explicit RawScanner(const std::string &text) : text_(text) {}

    std::optional<Span> root()
    {
        position_ = 0;
        space();
        const size_t begin = position_;
        if (!value()) {
            return std::nullopt;
        }
        return Span{begin, position_};
    }

    std::optional<Span> member(Span object, std::string_view name)
    {
        position_ = object.begin;
        if (text_[position_] != '{') {
            return std::nullopt;
        }
        ++position_;
        for (;;) {
            space();
            if (text_[position_] == '}') {
                return std::nullopt;
            }
            const size_t keyBegin = position_;
            if (!string()) {
                return std::nullopt;
            }
            const std::string_view key(text_.data() + keyBegin + 1, position_ - keyBegin - 2);
            space();
            if (text_[position_] != ':') {
                return std::nullopt;
            }
            ++position_;
            space();
            const size_t valueBegin = position_;
            if (!value()) {
                return std::nullopt;
            }
            if (key == name) {
                return Span{valueBegin, position_};
            }
            space();
            if (text_[position_] == ',') {
                ++position_;
            }
        }
    }

    std::optional<Span> element(Span array, size_t index)
    {
        position_ = array.begin;
        if (text_[position_] != '[') {
            return std::nullopt;
        }
        ++position_;
        for (size_t at = 0;; ++at) {
            space();
            if (position_ >= text_.size() || text_[position_] == ']') {
                return std::nullopt;
            }
            const size_t valueBegin = position_;
            if (!value()) {
                return std::nullopt;
            }
            if (at == index) {
                return Span{valueBegin, position_};
            }
            space();
            if (text_[position_] == ',') {
                ++position_;
            }
        }
    }

private:
    void space()
    {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\n' || text_[position_] == '\r' ||
                text_[position_] == '\t')) {
            ++position_;
        }
    }

    bool string()
    {
        if (text_[position_] != '"') {
            return false;
        }
        ++position_;
        while (position_ < text_.size() && text_[position_] != '"') {
            position_ += text_[position_] == '\\' ? 2 : 1;
        }
        ++position_;
        return position_ <= text_.size();
    }

    bool container(char open, char close)
    {
        ++position_;
        for (;;) {
            space();
            if (position_ >= text_.size()) {
                return false;
            }
            if (text_[position_] == close) {
                ++position_;
                return true;
            }
            if (open == '{') {
                if (!string()) {
                    return false;
                }
                space();
                ++position_;
                space();
            }
            if (!value()) {
                return false;
            }
            space();
            if (text_[position_] == ',') {
                ++position_;
            }
        }
    }

    bool value()
    {
        if (position_ >= text_.size()) {
            return false;
        }
        const char c = text_[position_];
        if (c == '"') {
            return string();
        }
        if (c == '{') {
            return container('{', '}');
        }
        if (c == '[') {
            return container('[', ']');
        }
        while (position_ < text_.size() && text_[position_] != ',' && text_[position_] != '}' &&
               text_[position_] != ']' && text_[position_] != ' ' && text_[position_] != '\n' &&
               text_[position_] != '\r' && text_[position_] != '\t') {
            ++position_;
        }
        return true;
    }

    const std::string &text_;
    size_t position_ = 0;
};

inline std::optional<std::string> readFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}
