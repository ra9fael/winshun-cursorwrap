// query_parser.cpp — see query_parser.h.
#include "query_parser.h"

#include <cctype>

namespace cw {
namespace {

struct Token {
    const char* word;
    CommandKind kind;
    long long value;
};

// Longest-match first so "impact on" wins over "impact".
const Token kTokens[] = {
    { "impact on", CommandKind::SetImpact, 1 },
    { "impact off", CommandKind::SetImpact, 0 },
    { "impact", CommandKind::MenuImpact, 0 },
    { "撞击 开", CommandKind::SetImpact, 1 },
    { "撞击 关", CommandKind::SetImpact, 0 },
    { "撞击", CommandKind::MenuImpact, 0 },
    { "speed", CommandKind::MenuSpeed, 0 },
    { "速度", CommandKind::MenuSpeed, 0 },
    { "band", CommandKind::MenuBand, 0 },
    { "区间", CommandKind::MenuBand, 0 },
    { "mode both", CommandKind::SetMode, static_cast<long long>(WrapMode::Both) },
    { "mode vertical", CommandKind::SetMode, static_cast<long long>(WrapMode::VerticalOnly) },
    { "mode horizontal", CommandKind::SetMode, static_cast<long long>(WrapMode::HorizontalOnly) },
    { "mode", CommandKind::MenuMode, 0 },
    { "模式 双向", CommandKind::SetMode, static_cast<long long>(WrapMode::Both) },
    { "模式 上下", CommandKind::SetMode, static_cast<long long>(WrapMode::VerticalOnly) },
    { "模式 左右", CommandKind::SetMode, static_cast<long long>(WrapMode::HorizontalOnly) },
    { "模式", CommandKind::MenuMode, 0 },
    { "ctrl", CommandKind::SetActivation, static_cast<long long>(ActivationMode::WhileCtrl) },
    { "shift", CommandKind::SetActivation, static_cast<long long>(ActivationMode::WhileShift) },
    { "always", CommandKind::SetActivation, static_cast<long long>(ActivationMode::Always) },
    { "activation", CommandKind::MenuActivation, 0 },
    { "激活", CommandKind::MenuActivation, 0 },
    { "按住 ctrl", CommandKind::SetActivation, static_cast<long long>(ActivationMode::WhileCtrl) },
    { "按住 shift", CommandKind::SetActivation, static_cast<long long>(ActivationMode::WhileShift) },
    { "按住ctrl", CommandKind::SetActivation, static_cast<long long>(ActivationMode::WhileCtrl) },
    { "按住shift", CommandKind::SetActivation, static_cast<long long>(ActivationMode::WhileShift) },
    { "总是", CommandKind::SetActivation, static_cast<long long>(ActivationMode::Always) },
    { "按住", CommandKind::SetActivation, static_cast<long long>(ActivationMode::WhileCtrl) },
    { "guard", CommandKind::MenuGuard, 0 },
    { "防震", CommandKind::MenuGuard, 0 },
    { "edge", CommandKind::MenuEdge, 0 },
    { "边缘", CommandKind::MenuEdge, 0 },
    { "tol", CommandKind::MenuTolerance, 0 },
    { "容差", CommandKind::MenuTolerance, 0 },
    { "diag", CommandKind::CopyDiagnostics, 0 },
    { "诊断", CommandKind::CopyDiagnostics, 0 },
    { "reset", CommandKind::ResetToDefaults, 0 },
    { "恢复默认", CommandKind::ResetToDefaults, 0 },
    { "恢复", CommandKind::ResetToDefaults, 0 },
};

bool asciiEqual(const std::string& text, const char* lower)
{
    return asciiLower(text) == lower;
}

// The number after a keyword token: the rest of the text with spaces and a
// Chinese colon stripped. Returns nullopt when there is no integer there.
std::optional<long long> trailingNumber(const std::string& rest)
{
    std::string digits;
    bool started = false;
    for (char c : rest) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
            started = true;
        } else if (started) {
            break; // digits ended; ignore any trailing words
        } else if (c != ' ' && c != '\t' && c != ':') {
            return std::nullopt; // a word where the number should be
        }
    }
    if (!started || digits.size() > 9)
        return std::nullopt;
    long long value = 0;
    for (char c : digits)
        value = value * 10 + (c - '0');
    return value;
}

long long clampTo(long long v, long long lo, long long hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

std::string asciiLower(const std::string& text)
{
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

ParsedCommand parseCommand(const std::string& rawText)
{
    // Trim.
    size_t begin = rawText.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return { CommandKind::Show, 0, "" };
    size_t end = rawText.find_last_not_of(" \t");
    const std::string text = asciiLower(rawText.substr(begin, end - begin + 1));

    for (const Token& token : kTokens) {
        const std::string word = asciiLower(token.word);
        if (text.compare(0, word.size(), word) != 0)
            continue;
        // A keyword must not glue onto a longer word ("modes" is not "mode";
        // "撞击模式" is not "撞击"). ASCII words: the next char must be a
        // boundary. zh words: the next char must not be another zh char.
        const std::string rest = text.substr(word.size());
        const bool asciiWord = word.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz ") == std::string::npos;
        bool boundary = rest.empty();
        if (!rest.empty()) {
            const unsigned char next = static_cast<unsigned char>(rest.front());
            if (asciiWord)
                boundary = next == ' ' || next == '\t' || rest.front() == ':'
                    || next >= 0x80; // an ASCII keyword may end at a zh char
            else
                boundary = next < 0x80; // a zh keyword may not run into zh
        }
        if (!boundary)
            continue;

        ParsedCommand cmd;
        cmd.kind = token.kind;
        cmd.word = token.word;
        cmd.value = token.value;
        // A menu keyword with a trailing number becomes a direct set
        // ("speed 3000" sets the speed; "speed" alone opens the submenu).
        const auto n = trailingNumber(rest);
        switch (token.kind) {
        case CommandKind::MenuSpeed:
            if (n) {
                cmd.kind = CommandKind::SetImpactSpeed;
                cmd.value = clampTo(*n, 500, 20000);
            }
            break;
        case CommandKind::MenuBand:
            if (n) {
                cmd.kind = CommandKind::SetBand;
                cmd.value = clampTo(*n, 50, 500);
            }
            break;
        case CommandKind::MenuGuard:
            if (n) {
                cmd.kind = CommandKind::SetGuard;
                cmd.value = clampTo(*n, 10, 200);
            }
            break;
        case CommandKind::MenuEdge:
            if (n) {
                cmd.kind = CommandKind::SetEdge;
                cmd.value = clampTo(*n, 1, 20);
            }
            break;
        case CommandKind::MenuTolerance:
            if (n) {
                cmd.kind = CommandKind::SetTolerance;
                cmd.value = clampTo(*n, 10, 200);
            }
            break;
        case CommandKind::MenuMode:
            // "mode" alone opens the submenu; "mode both" etc. already
            // matched the longer tokens above.
            break;
        case CommandKind::MenuImpact:
            // "impact" alone opens the submenu; "impact on/off" matched above.
            break;
        default:
            break;
        }
        return cmd;
    }
    return { CommandKind::Unknown, 0, text };
}

} // namespace cw
