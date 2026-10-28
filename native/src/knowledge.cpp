#include "knowledge.h"
#include "utf.h"

#include <nlohmann/json.hpp>

extern "C" {
#include "../third_party/puff/puff.h"
}

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <set>

namespace {

constexpr size_t kMaxFile = 8 * 1024 * 1024;
constexpr size_t kChunk = 700;

std::string trim_copy_utf8(std::string text) {
    return trim_utf8(std::move(text));
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool next_cp(const std::string& text, size_t& i, uint32_t& cp) {
    if (i >= text.size()) return false;
    auto byte = static_cast<unsigned char>(text[i]);
    if (byte < 0x80) {
        cp = byte;
        ++i;
        return true;
    }
    int need = byte < 0xE0 ? 1 : byte < 0xF0 ? 2 : 3;
    if (i + static_cast<size_t>(need) >= text.size()) {
        cp = byte;
        ++i;
        return true;
    }
    cp = byte & (need == 1 ? 0x1F : need == 2 ? 0x0F : 0x07);
    for (int n = 0; n < need; ++n) {
        cp = (cp << 6) | (static_cast<unsigned char>(text[++i]) & 0x3F);
    }
    ++i;
    return true;
}

std::string lower_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

std::string normalize_body(const std::string& text) {
    std::string out;
    bool space = false;
    for (unsigned char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out.push_back(' ');
        space = false;
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::string collapse_space(const std::string& text) {
    std::string out = normalize_body(text);
    if (out.size() > 480) out.resize(480);
    return out;
}

std::wstring extension_of(const std::wstring& path) {
    auto dot = path.find_last_of(L'.');
    auto slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    std::wstring ext = path.substr(dot + 1);
    for (wchar_t& ch : ext) ch = static_cast<wchar_t>(towlower(ch));
    return ext;
}

std::wstring file_name_of(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool read_file_bytes(const std::wstring& path, std::string& bytes, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "unreadable";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > kMaxFile) {
        CloseHandle(file);
        error = "too large";
        return false;
    }
    bytes.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD got = 0;
    BOOL ok = bytes.empty() || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr);
    CloseHandle(file);
    if (!ok || got != bytes.size()) {
        error = "unreadable";
        return false;
    }
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    } else if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring wide(bytes.size() / 2 - 1, L'\0');
        memcpy(wide.data(), bytes.data() + 2, wide.size() * sizeof(wchar_t));
        bytes = wide_to_utf8(wide);
    }
    return true;
}

uint16_t read_u16(const std::string& data, size_t at) {
    if (at + 2 > data.size()) return 0;
    return static_cast<uint16_t>(static_cast<unsigned char>(data[at]) | (static_cast<unsigned char>(data[at + 1]) << 8));
}

uint32_t read_u32(const std::string& data, size_t at) {
    if (at + 4 > data.size()) return 0;
    return static_cast<uint32_t>(static_cast<unsigned char>(data[at])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[at + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[at + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[at + 3])) << 24);
}

bool inflate_raw(const unsigned char* src, size_t src_len, size_t hint, std::string& out) {
    unsigned long destlen = static_cast<unsigned long>(hint ? hint : src_len * 4 + 64);
    if (destlen < 64) destlen = 64;
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::string buf(destlen, '\0');
        unsigned long slen = static_cast<unsigned long>(src_len);
        unsigned long dlen = destlen;
        int rc = puff(reinterpret_cast<unsigned char*>(buf.data()), &dlen, src, &slen);
        if (rc == 0) {
            buf.resize(dlen);
            out = std::move(buf);
            return true;
        }
        if (rc != 1) return false;
        if (destlen > 32 * 1024 * 1024) return false;
        destlen *= 2;
    }
    return false;
}

struct ZipEntry {
    std::string name;
    std::string data;
};

bool unzip(const std::string& zip, std::vector<ZipEntry>& entries) {
    if (zip.size() < 22) return false;
    size_t start = zip.size() > 66000 ? zip.size() - 66000 : 0;
    size_t eocd = std::string::npos;
    for (size_t i = zip.size() - 22;; --i) {
        if (read_u32(zip, i) == 0x06054b50) {
            eocd = i;
            break;
        }
        if (i <= start) break;
    }
    if (eocd == std::string::npos) return false;
    uint16_t count = read_u16(zip, eocd + 8);
    if (count == 0) count = read_u16(zip, eocd + 10);
    uint32_t cursor = read_u32(zip, eocd + 16);
    if (cursor >= zip.size()) return false;
    for (uint16_t n = 0; n < count; ++n) {
        if (cursor + 46 > zip.size() || read_u32(zip, cursor) != 0x02014b50) break;
        uint16_t method = read_u16(zip, cursor + 10);
        uint32_t comp = read_u32(zip, cursor + 20);
        uint32_t uncomp = read_u32(zip, cursor + 24);
        uint16_t nlen = read_u16(zip, cursor + 28);
        uint16_t elen = read_u16(zip, cursor + 30);
        uint16_t clen = read_u16(zip, cursor + 32);
        uint32_t local = read_u32(zip, cursor + 42);
        if (cursor + 46 + nlen > zip.size()) break;
        std::string name = zip.substr(cursor + 46, nlen);
        cursor += 46 + nlen + elen + clen;
        if (local + 30 > zip.size() || read_u32(zip, local) != 0x04034b50) continue;
        size_t data_at = local + 30 + read_u16(zip, local + 26) + read_u16(zip, local + 28);
        if (data_at + comp > zip.size()) continue;
        ZipEntry entry;
        entry.name = name;
        if (method == 0) {
            entry.data = zip.substr(data_at, comp);
        } else if (method == 8) {
            if (!inflate_raw(reinterpret_cast<const unsigned char*>(zip.data() + data_at), comp, uncomp, entry.data)) continue;
        } else {
            continue;
        }
        entries.push_back(std::move(entry));
    }
    return !entries.empty();
}

std::string decode_entities(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        std::string token = text.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        bool known = true;
        if (token == "amp") cp = '&';
        else if (token == "lt") cp = '<';
        else if (token == "gt") cp = '>';
        else if (token == "quot") cp = '"';
        else if (token == "apos") cp = '\'';
        else if (!token.empty() && token[0] == '#') {
            try {
                cp = token.size() > 1 && (token[1] == 'x' || token[1] == 'X') ? static_cast<uint32_t>(std::stoul(token.substr(2), nullptr, 16))
                                                                               : static_cast<uint32_t>(std::stoul(token.substr(1)));
            } catch (const std::exception&) {
                known = false;
            }
        } else {
            known = false;
        }
        if (!known || cp == 0) {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        append_utf8(out, cp);
        i = semi + 1;
    }
    return out;
}

bool tag_is(const std::string& tag, const char* name) {
    size_t i = 0;
    if (!tag.empty() && tag[0] == '/') ++i;
    size_t n = strlen(name);
    if (tag.size() < i + n) return false;
    for (size_t k = 0; k < n; ++k) {
        char ch = tag[i + k];
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        if (ch != name[k]) return false;
    }
    if (tag.size() == i + n) return true;
    char next = tag[i + n];
    return next == ' ' || next == '/' || next == '>';
}

std::string strip_markup(const std::string& in, bool html) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] != '<') {
            out.push_back(in[i]);
            ++i;
            continue;
        }
        size_t end = in.find('>', i + 1);
        if (end == std::string::npos) {
            out.push_back(in[i]);
            ++i;
            continue;
        }
        std::string tag = in.substr(i + 1, end - i - 1);
        std::string lower = lower_ascii(tag);
        if (html && (tag_is(lower, "script") || tag_is(lower, "style")) && (lower.empty() || lower[0] != '/')) {
            std::string close = tag_is(lower, "script") ? "</script" : "</style";
            size_t from = end + 1;
            size_t stop = lower_ascii(in.substr(from)).find(close);
            if (stop == std::string::npos) break;
            i = from + stop;
            continue;
        }
        if (tag_is(lower, "p") || tag_is(lower, "div") || tag_is(lower, "br") || tag_is(lower, "tr") || tag_is(lower, "li") ||
            tag_is(lower, "h1") || tag_is(lower, "h2") || tag_is(lower, "h3") || tag_is(lower, "w:p") || tag_is(lower, "a:p") ||
            tag_is(lower, "si") || tag_is(lower, "row")) {
            out.push_back('\n');
        }
        i = end + 1;
    }
    return decode_entities(out);
}

std::string rtf_text(const std::string& rtf) {
    std::string out;
    for (size_t i = 0; i < rtf.size();) {
        unsigned char ch = static_cast<unsigned char>(rtf[i]);
        if (ch == '{' || ch == '}') {
            ++i;
            continue;
        }
        if (ch != '\\') {
            out.push_back(static_cast<char>(ch));
            ++i;
            continue;
        }
        ++i;
        if (i >= rtf.size()) break;
        unsigned char next = static_cast<unsigned char>(rtf[i]);
        if (next == '\\' || next == '{' || next == '}') {
            out.push_back(static_cast<char>(next));
            ++i;
            continue;
        }
        if (next == '\'') {
            if (i + 2 < rtf.size()) {
                auto hex = [](char c) {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return 0;
                };
                append_utf8(out, static_cast<uint32_t>(hex(rtf[i + 1]) * 16 + hex(rtf[i + 2])));
                i += 3;
            } else {
                break;
            }
            continue;
        }
        size_t word = i;
        while (i < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[i]))) ++i;
        std::string name = lower_ascii(rtf.substr(word, i - word));
        int sign = 1;
        if (i < rtf.size() && rtf[i] == '-') {
            sign = -1;
            ++i;
        }
        int number = 0;
        bool has_number = false;
        while (i < rtf.size() && std::isdigit(static_cast<unsigned char>(rtf[i]))) {
            has_number = true;
            number = number * 10 + (rtf[i] - '0');
            ++i;
        }
        if (i < rtf.size() && rtf[i] == ' ') ++i;
        if (name == "par" || name == "line") out.push_back('\n');
        if (name == "u" && has_number) {
            int cp = sign * number;
            if (cp < 0) cp += 65536;
            append_utf8(out, static_cast<uint32_t>(cp));
            if (i < rtf.size() && rtf[i] != '\\' && rtf[i] != '{' && rtf[i] != '}') ++i;
        }
    }
    return out;
}

std::vector<std::string> split_chunks(const std::string& text) {
    std::vector<std::string> blocks;
    std::string current;
    std::string line;
    auto flush = [&]() {
        std::string piece = trim_copy_utf8(current);
        current.clear();
        if (piece.empty()) return;
        while (piece.size() > 900) {
            blocks.push_back(piece.substr(0, 900));
            piece.erase(0, 900);
        }
        if (!piece.empty()) blocks.push_back(piece);
    };
    std::string source = text;
    source.push_back('\n');
    for (char ch : source) {
        if (ch == '\r') continue;
        if (ch != '\n') {
            line.push_back(ch);
            continue;
        }
        if (trim_copy_utf8(line).empty()) {
            flush();
        } else {
            if (!current.empty() && current.size() + line.size() + 1 > kChunk) flush();
            if (!current.empty()) current.push_back('\n');
            current += line;
        }
        line.clear();
    }
    flush();
    if (blocks.size() > 400) blocks.resize(400);
    return blocks;
}

std::string title_from(const std::string& body, const std::string& fallback) {
    std::string line = body;
    auto cut = line.find('\n');
    if (cut != std::string::npos) line.resize(cut);
    line = trim_copy_utf8(line);
    if (line.size() > 80) line.resize(80);
    return line.empty() ? fallback : line;
}

std::vector<std::string> parse_row(const std::string& line, char sep) {
    std::vector<std::string> cells;
    std::string cell;
    bool quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (quote) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cell.push_back('"');
                    ++i;
                } else {
                    quote = false;
                }
            } else {
                cell.push_back(ch);
            }
        } else if (ch == '"') {
            quote = true;
        } else if (ch == sep) {
            cells.push_back(trim_copy_utf8(cell));
            cell.clear();
        } else {
            cell.push_back(ch);
        }
    }
    cells.push_back(trim_copy_utf8(cell));
    return cells;
}

std::vector<std::string> table_records(const std::string& text, char sep) {
    std::vector<std::string> rows;
    std::string line;
    std::string source = text;
    source.push_back('\n');
    for (char ch : source) {
        if (ch == '\r') continue;
        if (ch != '\n') {
            line.push_back(ch);
            continue;
        }
        if (!trim_copy_utf8(line).empty()) rows.push_back(line);
        line.clear();
    }
    if (rows.empty()) return {};
    auto header = parse_row(rows[0], sep);
    bool named = rows.size() >= 2;
    std::vector<std::string> out;
    for (size_t r = named ? 1 : 0; r < rows.size() && out.size() < 400; ++r) {
        auto cells = parse_row(rows[r], sep);
        std::string body;
        for (size_t c = 0; c < cells.size(); ++c) {
            if (cells[c].empty()) continue;
            if (!body.empty()) body += "; ";
            if (named && c < header.size() && !header[c].empty()) body += header[c] + ": " + cells[c];
            else body += cells[c];
        }
        if (!body.empty()) out.push_back(body);
    }
    return out;
}

std::string field_text(const nlohmann::json& item, const char* key) {
    if (!item.is_object() || !item.contains(key) || item[key].is_null()) return {};
    if (item[key].is_string()) return item[key].get<std::string>();
    return item[key].dump();
}

std::vector<std::pair<std::string, std::string>> json_records(const nlohmann::json& item) {
    std::vector<std::pair<std::string, std::string>> out;
    if (item.is_array()) {
        for (const auto& child : item) {
            auto part = json_records(child);
            out.insert(out.end(), part.begin(), part.end());
            if (out.size() >= 400) break;
        }
        return out;
    }
    if (item.is_object()) {
        std::string title = field_text(item, "title");
        if (title.empty()) title = field_text(item, "name");
        if (title.empty()) title = field_text(item, "heading");
        std::string body = field_text(item, "body");
        if (body.empty()) body = field_text(item, "text");
        if (body.empty()) body = field_text(item, "content");
        if (body.empty()) body = field_text(item, "fact");
        if (body.empty()) body = item.dump();
        out.push_back({title, body});
        return out;
    }
    if (item.is_string()) out.push_back({"", item.get<std::string>()});
    else if (!item.is_null()) out.push_back({"", item.dump()});
    return out;
}

std::vector<std::string> pdf_strings(const std::string& text) {
    std::vector<std::string> found;
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '(') {
            ++i;
            continue;
        }
        std::string literal;
        bool ok = false;
        ++i;
        while (i < text.size()) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                char n = text[i + 1];
                if (n == 'n') literal.push_back('\n');
                else if (n == 'r') literal.push_back('\r');
                else if (n == 't') literal.push_back('\t');
                else literal.push_back(n);
                i += 2;
                continue;
            }
            if (text[i] == ')') {
                ok = true;
                ++i;
                break;
            }
            literal.push_back(text[i]);
            ++i;
        }
        if (!ok) continue;
        size_t look = i;
        while (look < text.size() && (text[look] == ' ' || text[look] == '\r' || text[look] == '\n')) ++look;
        if (text.compare(look, 2, "Tj") == 0 || text.compare(look, 2, "TJ") == 0) {
            literal = trim_copy_utf8(literal);
            if (literal.size() >= 2) found.push_back(literal);
        }
    }
    return found;
}

std::string inflate_pdf_streams(const std::string& pdf) {
    std::string extra;
    for (size_t pos = 0; pos + 6 < pdf.size();) {
        size_t at = pdf.find("stream", pos);
        if (at == std::string::npos) break;
        if (at >= 3 && pdf.compare(at - 3, 3, "end") == 0) {
            pos = at + 6;
            continue;
        }
        size_t start = at + 6;
        if (start < pdf.size() && pdf[start] == '\r') ++start;
        if (start < pdf.size() && pdf[start] == '\n') ++start;
        size_t stop = pdf.find("endstream", start);
        if (stop == std::string::npos) break;
        size_t dict = at > 500 ? at - 500 : 0;
        bool flate = pdf.find("FlateDecode", dict) != std::string::npos && pdf.find("FlateDecode", dict) < at;
        if (flate && stop > start) {
            std::string inflated;
            const unsigned char* src = reinterpret_cast<const unsigned char*>(pdf.data() + start);
            size_t len = stop - start;
            while (len && (src[len - 1] == '\n' || src[len - 1] == '\r')) --len;
            bool ok = inflate_raw(src, len, 0, inflated);
            if (!ok && len > 2 && src[0] == 0x78) ok = inflate_raw(src + 2, len - 2, 0, inflated);
            if (ok) extra += inflated;
        }
        pos = stop + 9;
    }
    return extra;
}

struct Piece {
    std::string title;
    std::string body;
};

std::vector<Piece> pieces_from(const std::wstring& ext, const std::string& bytes, const std::string& fallback_title) {
    std::vector<Piece> pieces;
    auto add_blocks = [&](const std::string& title, const std::string& text) {
        for (const std::string& block : split_chunks(text)) {
            Piece piece;
            piece.title = title.empty() ? title_from(block, fallback_title) : title;
            piece.body = block;
            pieces.push_back(std::move(piece));
            if (pieces.size() >= 400) return;
        }
    };
    if (ext == L"csv" || ext == L"tsv") {
        for (const std::string& row : table_records(bytes, ext == L"tsv" ? '\t' : ',')) add_blocks("", row);
        return pieces;
    }
    if (ext == L"json") {
        auto parsed = nlohmann::json::parse(bytes, nullptr, false);
        if (!parsed.is_discarded()) {
            for (const auto& item : json_records(parsed)) add_blocks(item.first, item.second);
            return pieces;
        }
    }
    if (ext == L"jsonl") {
        std::string line;
        std::string source = bytes;
        source.push_back('\n');
        for (char ch : source) {
            if (ch == '\r') continue;
            if (ch != '\n') {
                line.push_back(ch);
                continue;
            }
            if (!trim_copy_utf8(line).empty()) {
                auto parsed = nlohmann::json::parse(line, nullptr, false);
                if (!parsed.is_discarded()) {
                    for (const auto& item : json_records(parsed)) add_blocks(item.first, item.second);
                } else {
                    add_blocks("", line);
                }
            }
            line.clear();
        }
        return pieces;
    }
    if (ext == L"html" || ext == L"htm" || ext == L"xml" || ext == L"docx" || ext == L"xlsx" || ext == L"pptx") {
        std::string markup = bytes;
        if (ext == L"docx" || ext == L"xlsx" || ext == L"pptx") {
            std::vector<ZipEntry> entries;
            if (!unzip(bytes, entries)) return pieces;
            markup.clear();
            for (const auto& entry : entries) {
                std::string name = lower_ascii(entry.name);
                bool keep = false;
                if (ext == L"docx") keep = name.find("word/document") != std::string::npos || name.find("word/header") != std::string::npos || name.find("word/footer") != std::string::npos;
                if (ext == L"xlsx") keep = name.find("xl/sharedstrings") != std::string::npos || name.find("xl/worksheets/") != std::string::npos;
                if (ext == L"pptx") keep = name.find("ppt/slides/slide") != std::string::npos;
                if (keep) markup += entry.data;
            }
        }
        add_blocks("", strip_markup(markup, ext == L"html" || ext == L"htm"));
        return pieces;
    }
    if (ext == L"rtf") {
        add_blocks("", rtf_text(bytes));
        return pieces;
    }
    if (ext == L"pdf") {
        std::string combined = bytes + "\n" + inflate_pdf_streams(bytes);
        std::string joined;
        for (const std::string& literal : pdf_strings(combined)) {
            if (!joined.empty()) joined.push_back('\n');
            joined += literal;
        }
        add_blocks("", joined);
        return pieces;
    }
    add_blocks("", bytes);
    return pieces;
}

int next_serial(const KnowledgeBase& kb) {
    int serial = 0;
    for (const auto& record : kb.records) {
        auto dash = record.id.rfind('-');
        if (dash == std::string::npos) continue;
        try {
            serial = std::max(serial, std::stoi(record.id.substr(dash + 1)));
        } catch (const std::exception&) {
        }
    }
    return serial;
}

bool same_body(const KbRecord& record, const std::string& domain, const std::string& body) {
    return record.kind == "industry" && record.domain == domain && normalize_body(record.body) == normalize_body(body);
}

std::vector<std::string> question_tokens(const std::string& question) {
    std::vector<std::string> tokens;
    std::string ascii;
    std::vector<uint32_t> cjk;
    auto flush_ascii = [&]() {
        static const std::set<std::string> stop = {"the", "is", "of", "to", "and", "or", "in", "on", "for", "an", "what", "how", "does", "with", "are", "was"};
        if (ascii.size() >= 2 && !stop.count(ascii)) tokens.push_back(ascii);
        ascii.clear();
    };
    auto flush_cjk = [&]() {
        if (cjk.size() == 1) {
            std::string one;
            append_utf8(one, cjk[0]);
            tokens.push_back(one);
        } else {
            for (size_t i = 0; i + 1 < cjk.size(); ++i) {
                std::string gram;
                append_utf8(gram, cjk[i]);
                append_utf8(gram, cjk[i + 1]);
                tokens.push_back(gram);
            }
        }
        cjk.clear();
    };
    size_t i = 0;
    uint32_t cp = 0;
    while (next_cp(question, i, cp)) {
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) {
            flush_cjk();
            ascii.push_back(static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp - 'A' + 'a' : cp));
        } else if (cp >= 0x4E00 && cp <= 0x9FFF) {
            flush_ascii();
            cjk.push_back(cp);
        } else {
            flush_ascii();
            flush_cjk();
        }
    }
    flush_ascii();
    flush_cjk();
    return tokens;
}

}  // namespace

bool load_knowledge(const std::wstring& path, KnowledgeBase& kb) {
    std::string bytes;
    std::string error;
    if (!read_file_bytes(path, bytes, error)) return false;
    auto parsed = nlohmann::json::parse(bytes, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("records") || !parsed["records"].is_array()) return false;
    KnowledgeBase loaded;
    for (const auto& item : parsed["records"]) {
        if (!item.is_object()) continue;
        KbRecord record;
        record.id = item.value("id", "");
        record.kind = item.value("kind", "industry");
        record.domain = item.value("domain", "");
        record.title = item.value("title", "");
        record.body = item.value("body", "");
        record.source = item.value("source", "");
        record.format = item.value("format", "");
        record.status = item.value("status", "imported");
        if (record.body.empty()) continue;
        loaded.records.push_back(std::move(record));
    }
    kb = std::move(loaded);
    return true;
}

bool save_knowledge(const std::wstring& path, const KnowledgeBase& kb) {
    nlohmann::json records = nlohmann::json::array();
    for (const auto& record : kb.records) {
        records.push_back({{"id", record.id},
                           {"kind", record.kind},
                           {"domain", record.domain},
                           {"title", record.title},
                           {"body", record.body},
                           {"source", record.source},
                           {"format", record.format},
                           {"status", record.status}});
    }
    nlohmann::json root = {{"version", 1}, {"records", records}};
    std::string body = root.dump(2);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr);
    CloseHandle(file);
    return ok && wrote == body.size();
}

void sync_notes(KnowledgeBase& kb, const std::string& notes) {
    std::vector<KbRecord> kept;
    for (const auto& record : kb.records) {
        if (record.kind != "note") kept.push_back(record);
    }
    kb.records.clear();
    int serial = 0;
    std::string line;
    std::string source = notes;
    source.push_back('\n');
    for (char ch : source) {
        if (ch == '\r') continue;
        if (ch != '\n') {
            line.push_back(ch);
            continue;
        }
        std::string body = trim_copy_utf8(line);
        line.clear();
        if (body.empty()) continue;
        KbRecord record;
        record.id = "note-" + std::to_string(++serial);
        record.kind = "note";
        record.domain = "notes";
        record.title = title_from(body, "note");
        record.body = body;
        record.format = "txt";
        record.status = "noted";
        kb.records.push_back(std::move(record));
    }
    for (auto& record : kept) kb.records.push_back(std::move(record));
}

ImportReport import_industry_file(KnowledgeBase& kb, const std::wstring& path, const std::string& domain_text) {
    ImportReport report;
    std::string domain = trim_copy_utf8(domain_text);
    if (domain.empty()) domain = "industry";
    if (domain.size() > 80) domain.resize(80);
    std::wstring ext = extension_of(path);
    const std::set<std::wstring> supported = {L"txt", L"md", L"csv", L"tsv", L"json", L"jsonl", L"html", L"htm",
                                              L"xml", L"rtf", L"log", L"docx", L"xlsx", L"pptx", L"pdf"};
    if (!supported.count(ext)) {
        report.failed = 1;
        report.detail = wide_to_utf8(file_name_of(path)) + " unsupported\n";
        return report;
    }
    std::string bytes;
    std::string error;
    if (!read_file_bytes(path, bytes, error)) {
        report.failed = 1;
        report.detail = wide_to_utf8(file_name_of(path)) + " " + error + "\n";
        return report;
    }
    std::string source = wide_to_utf8(file_name_of(path));
    std::string format = wide_to_utf8(ext);
    auto pieces = pieces_from(ext, bytes, source);
    if (pieces.empty()) {
        report.failed = 1;
        report.detail = source + " empty\n";
        return report;
    }
    int serial = next_serial(kb);
    for (const auto& piece : pieces) {
        std::string body = trim_copy_utf8(piece.body);
        if (body.empty()) continue;
        bool known = false;
        for (const auto& record : kb.records) {
            if (same_body(record, domain, body)) {
                known = true;
                break;
            }
        }
        if (known) {
            ++report.recognized;
            continue;
        }
        KbRecord record;
        record.id = "kb-" + std::to_string(++serial);
        record.kind = "industry";
        record.domain = domain;
        record.title = piece.title.empty() ? title_from(body, source) : piece.title;
        record.body = body;
        record.source = source;
        record.format = format;
        record.status = "imported";
        kb.records.push_back(std::move(record));
        ++report.added;
    }
    if (report.added == 0 && report.recognized == 0) {
        report.failed = 1;
        report.detail = source + " empty\n";
    }
    return report;
}

std::string retrieve_industry(const KnowledgeBase& kb, const std::string& question, int limit) {
    if (limit <= 0) return {};
    auto tokens = question_tokens(question);
    struct Hit {
        int score = 0;
        const KbRecord* record = nullptr;
    };
    std::vector<Hit> hits;
    for (const auto& record : kb.records) {
        if (record.kind != "industry") continue;
        std::string hay = lower_ascii(record.title + "\n" + record.body);
        int score = 0;
        for (const auto& token : tokens) {
            if (hay.find(token) != std::string::npos) ++score;
        }
        if (score > 0) hits.push_back({score, &record});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    std::string out;
    int kept = 0;
    for (const auto& hit : hits) {
        if (kept >= limit) break;
        if (!out.empty()) out.push_back('\n');
        out += "[" + hit.record->domain + "] " + hit.record->title + ": " + collapse_space(hit.record->body);
        ++kept;
    }
    if (!out.empty() || !has_industry(kb)) return out;
    std::set<std::string> domains;
    for (const auto& record : kb.records) {
        if (record.kind == "industry") domains.insert(record.domain);
    }
    out = "Industry library:";
    for (const auto& domain : domains) {
        int count = 0;
        for (const auto& record : kb.records) {
            if (record.kind == "industry" && record.domain == domain) ++count;
        }
        out += " " + domain + " (" + std::to_string(count) + ")";
    }
    out += ". No passage matched the question.";
    return out;
}

std::string compose_corpus(const KnowledgeBase& kb, const std::string& notes, const std::string& question) {
    std::string out = trim_copy_utf8(notes);
    std::string hit = retrieve_industry(kb, question, 6);
    if (!hit.empty()) {
        if (!out.empty()) out.push_back('\n');
        out += hit;
    }
    return out;
}

int industry_records(const KnowledgeBase& kb) {
    int count = 0;
    for (const auto& record : kb.records) {
        if (record.kind == "industry") ++count;
    }
    return count;
}

int industry_domains(const KnowledgeBase& kb) {
    std::set<std::string> domains;
    for (const auto& record : kb.records) {
        if (record.kind == "industry") domains.insert(record.domain);
    }
    return static_cast<int>(domains.size());
}

bool has_industry(const KnowledgeBase& kb) {
    return industry_records(kb) > 0;
}
