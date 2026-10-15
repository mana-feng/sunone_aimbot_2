#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>
#include <cctype>
#include "model_profile.h"

struct DetectorModelInfo
{
    std::string modelName;
    std::string provider;
    std::string input;
    std::vector<std::string> outputs;
    std::map<std::string, std::string> metadata;
    std::map<int, std::string> classNames;
    std::string namesSource;
    std::string layout;
    std::string colorOrder;
    float inputScale = 0;
    bool objectness = false;
    int classes = 0;
    std::string failedModel;
    std::string loadError;
};

namespace model_metadata_detail
{
// A restricted data-only parser for the Python/JSON list and dictionary
// literals exported in ONNX "names". No evaluation or regex-based splitting.
class NamesParser
{
    const std::string& text;
    size_t pos = 0;
    void space() { while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos; }
    char take() { if (pos == text.size()) throw std::runtime_error("Incomplete names"); return text[pos++]; }
    void expect(char c) { space(); if (take() != c) throw std::runtime_error("Invalid names"); }
    unsigned hex(int count)
    {
        unsigned value = 0;
        while (count--)
        {
            const char c = take();
            int digit = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) throw std::runtime_error("Invalid Unicode escape");
            value = value * 16 + digit;
        }
        return value;
    }
    static void utf8(std::string& out, unsigned cp)
    {
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp == 0)
            throw std::runtime_error("Invalid Unicode codepoint");
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63)); }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        }
        else
        {
            out += static_cast<char>(0xf0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 63));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63));
        }
    }
    std::string quoted()
    {
        space();
        const char quote = take();
        if (quote != '\'' && quote != '"') throw std::runtime_error("Expected string");
        std::string out;
        for (;;)
        {
            char c = take();
            if (c == quote) return out;
            if (static_cast<unsigned char>(c) < 32) throw std::runtime_error("Control character in names");
            if (c != '\\') { out += c; continue; }
            c = take();
            if (c == '\\' || c == '\'' || c == '"' || c == '/') out += c;
            else if (c == 'n') out += '\n';
            else if (c == 'r') out += '\r';
            else if (c == 't') out += '\t';
            else if (c == 'b') out += '\b';
            else if (c == 'f') out += '\f';
            else if (c == 'u' || c == 'U' || c == 'x')
            {
                unsigned cp = hex(c == 'u' ? 4 : c == 'U' ? 8 : 2);
                if (cp >= 0xd800 && cp <= 0xdbff)
                {
                    if (take() != '\\' || take() != 'u') throw std::runtime_error("Missing Unicode surrogate");
                    const auto low = hex(4);
                    if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("Invalid Unicode surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                }
                utf8(out, cp);
            }
            else throw std::runtime_error("Invalid string escape");
        }
    }
public:
    explicit NamesParser(const std::string& value) : text(value) {}
    std::map<int, std::string> parse()
    {
        if (text.size() > 1024 * 1024) throw std::runtime_error("Names too large");
        space();
        const char open = take();
        if (open != '[' && open != '{') throw std::runtime_error("Expected list or dictionary");
        const char close = open == '[' ? ']' : '}';
        std::map<int, std::string> result;
        space();
        while (pos < text.size() && text[pos] != close)
        {
            int id = static_cast<int>(result.size());
            if (open == '{')
            {
                space();
                std::string key;
                if (pos < text.size() && (text[pos] == '\'' || text[pos] == '"')) key = quoted();
                else while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) key += take();
                if (key.empty() || key.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("Invalid class ID");
                id = std::stoi(key);
                expect(':');
            }
            if (id >= 10000 || !result.emplace(id, quoted()).second) throw std::runtime_error("Invalid class ID");
            space();
            if (pos < text.size() && text[pos] == close) break;
            expect(',');
            space();
        }
        expect(close);
        space();
        if (pos != text.size()) throw std::runtime_error("Trailing names expression");
        return result;
    }
};
}

inline std::map<int, std::string> parseModelClassNames(const std::string& text)
{
    try { return model_metadata_detail::NamesParser(text).parse(); }
    catch (const std::exception&) { return {}; }
}

inline void saveModelClassRoles(const std::filesystem::path& model, int classes, const std::vector<ModelClassRole>& roles)
{
    validateModelClassRoles(roles, classes);
    const int player = firstModelClassWithRole(roles, ModelClassRole::EnemyBody);
    const int head = firstModelClassWithRole(roles, ModelClassRole::EnemyHead);
    std::string encoded;
    for (auto role : roles)
    {
        if (!encoded.empty()) encoded += ',';
        encoded += modelClassRoleToken(role);
    }
    if (classes < 1 || classes > 10000 || player < -1 || head < -1 || player >= classes || head >= classes)
        throw std::runtime_error("Target class is outside this model's class range");
    if (player >= 0 && player == head)
        throw std::runtime_error("Body and head must use different classes; disable head for a body-only model");
    auto path = model; path += L".ini";
    std::string content;
    bool bodyWritten = false, headWritten = false, rolesWritten = false;
    if (std::filesystem::exists(path))
    {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("Cannot read model settings; no changes saved");
        std::string line;
        while (std::getline(in, line))
        {
            const auto equal = line.find('=');
            const auto key = equal == std::string::npos ? std::string() : modelTrim(line.substr(0, equal));
            if (key == "class_player") { line = "class_player=" + std::to_string(player); bodyWritten = true; }
            if (key == "class_head") { line = "class_head=" + std::to_string(head); headWritten = true; }
            if (key == "class_roles") { line = "class_roles=" + encoded; rolesWritten = true; }
            content += line + '\n';
        }
        if (in.bad()) throw std::runtime_error("Cannot read model settings; no changes saved");
    }
    if (!bodyWritten) content += "class_player=" + std::to_string(player) + '\n';
    if (!headWritten) content += "class_head=" + std::to_string(head) + '\n';

    if (!rolesWritten) content += "class_roles=" + encoded + '\n';

    auto temp = path;
    temp += L".ui-tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create model settings file; check folder permissions");
    DWORD written = 0;
    const bool saved = content.size() <= MAXDWORD &&
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
        written == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("Cannot save model settings; original file was preserved");
    }
}

// Compatibility entry point for older callers; the persisted roles remain authoritative.
inline void saveModelTargetClasses(const std::filesystem::path& model, int classes, int player, int head)
{
    if (classes < 1 || classes > 10000 || player < -1 || head < -1 || player >= classes || head >= classes ||
        (player >= 0 && player == head)) throw std::runtime_error("Invalid legacy target classes");
    saveModelClassRoles(model, classes, legacyModelClassRoles(classes, player, head));
}
