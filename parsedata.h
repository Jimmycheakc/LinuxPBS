
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>


// Passive synchronous delimiter-based field parser.
//
// Example:
//   ParseData fields('[', ']', '|');
//   fields.Parse("[A|B|C]");
//
//   Field(0) -> "A"
//   Field(1) -> "B"
//   Field(2) -> "C"
//
// The parser owns the parsed fields, so returned std::string copies remain
// independent of the source buffer. It owns no thread, io_context, mutex,
// work guard, strand, or coroutine state.
class ParseData final
{
public:
    ParseData() = default;

    ParseData(
        char startChar,
        char endChar,
        char separatorChar)
        : startChar_(startChar),
          endChar_(endChar),
          separatorChar_(separatorChar)
    {
    }

    void SetStyle(
        char startChar,
        char endChar,
        char separatorChar)
    {
        startChar_ = startChar;
        endChar_ = endChar;
        separatorChar_ = separatorChar;
    }

    // Parses one framed field list.
    //
    // A start delimiter is required. For backward compatibility with the
    // legacy parser, a missing end delimiter means "parse until end of input".
    // Empty fields between separators, including a trailing empty field, are
    // preserved. Example: "[A|B|]" -> {"A", "B", ""}.
    std::size_t Parse(std::string_view text)
    {
        fields_.clear();
        hasStartDelimiter_ = false;
        hasEndDelimiter_ = false;

        const std::size_t startPos = text.find(startChar_);
        if (startPos == std::string_view::npos)
        {
            return 0;
        }

        hasStartDelimiter_ = true;

        const std::size_t payloadBegin = startPos + 1;
        const std::size_t endPos = text.find(endChar_, payloadBegin);

        hasEndDelimiter_ = (endPos != std::string_view::npos);

        const std::size_t payloadEnd =
            hasEndDelimiter_ ? endPos : text.size();

        if (payloadEnd <= payloadBegin)
        {
            return 0;
        }

        const std::string_view payload = text.substr(payloadBegin, payloadEnd - payloadBegin);

        std::size_t fieldBegin = 0;

        while (fieldBegin <= payload.size())
        {
            const std::size_t separatorPos = payload.find(separatorChar_, fieldBegin);

            if (separatorPos == std::string_view::npos)
            {
                fields_.emplace_back(payload.substr(fieldBegin));
                break;
            }

            fields_.emplace_back(payload.substr(fieldBegin, separatorPos - fieldBegin));

            fieldBegin = separatorPos + 1;

            // Preserve a trailing empty field: "[A|B|]".
            if (fieldBegin == payload.size())
            {
                fields_.emplace_back();
                break;
            }
        }

        return fields_.size();
    }

    std::string Field(std::size_t number) const
    {
        if (number >= fields_.size())
        {
            return {};
        }

        return fields_[number];
    }

    std::string_view FieldView(std::size_t number) const
    {
        if (number >= fields_.size())
        {
            return {};
        }

        return fields_[number];
    }

    bool HasField(std::size_t number) const
    {
        return number < fields_.size();
    }

    std::size_t Size() const
    {
        return fields_.size();
    }

    bool Empty() const
    {
        return fields_.empty();
    }

    bool HasStartDelimiter() const
    {
        return hasStartDelimiter_;
    }

    bool HasEndDelimiter() const
    {
        return hasEndDelimiter_;
    }

    // Legacy-compatible helper:
    //   SetStrLen("12", 4)    -> "0012"
    //   SetStrLen("12345", 3) -> "345"
    static std::string SetStrLen(std::string_view text, int width)
    {
        if (width <= 0)
        {
            return {};
        }

        const std::size_t targetWidth = static_cast<std::size_t>(width);

        if (text.size() >= targetWidth)
        {
            return std::string(text.substr(text.size() - targetWidth));
        }

        std::string result(targetWidth - text.size(), '0');

        result.append(text);
        return result;
    }

    static std::string i2nc(int value, int width)
    {
        return SetStrLen(std::to_string(value), width);
    }

private:
    std::vector<std::string> fields_;

    char startChar_{'['};
    char endChar_{']'};
    char separatorChar_{'|'};

    bool hasStartDelimiter_{false};
    bool hasEndDelimiter_{false};
};
