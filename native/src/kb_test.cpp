#include "knowledge.h"
#include "utf.h"

#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void expect(bool ok, const char* name) {
    if (ok) {
        std::cout << "ok  " << name << "\n";
        return;
    }
    std::cout << "FAIL " << name << "\n";
    ++g_failed;
}

void append_u16(std::string& out, uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void append_u32(std::string& out, uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

std::string deflate_store(const std::string& data) {
    std::string out;
    out.push_back(1);
    auto n = static_cast<uint16_t>(data.size());
    append_u16(out, n);
    append_u16(out, static_cast<uint16_t>(~n));
    out += data;
    return out;
}

std::string make_zip(const std::vector<std::pair<std::string, std::string>>& files, bool deflate) {
    std::string local;
    std::string central;
    for (const auto& file : files) {
        std::string payload = deflate ? deflate_store(file.second) : file.second;
        uint32_t local_at = static_cast<uint32_t>(local.size());
        local.push_back('P');
        local.push_back('K');
        local.push_back(3);
        local.push_back(4);
        append_u16(local, 20);
        append_u16(local, 0);
        append_u16(local, deflate ? 8 : 0);
        append_u16(local, 0);
        append_u16(local, 0);
        append_u32(local, 0);
        append_u32(local, static_cast<uint32_t>(payload.size()));
        append_u32(local, static_cast<uint32_t>(file.second.size()));
        append_u16(local, static_cast<uint16_t>(file.first.size()));
        append_u16(local, 0);
        local += file.first;
        local += payload;

        central.push_back('P');
        central.push_back('K');
        central.push_back(1);
        central.push_back(2);
        append_u16(central, 20);
        append_u16(central, 20);
        append_u16(central, 0);
        append_u16(central, deflate ? 8 : 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u32(central, 0);
        append_u32(central, static_cast<uint32_t>(payload.size()));
        append_u32(central, static_cast<uint32_t>(file.second.size()));
        append_u16(central, static_cast<uint16_t>(file.first.size()));
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u32(central, 0);
        append_u32(central, local_at);
        central += file.first;
    }
    std::string zip = local + central;
    uint32_t cd_at = static_cast<uint32_t>(local.size());
    zip.push_back('P');
    zip.push_back('K');
    zip.push_back(5);
    zip.push_back(6);
    append_u16(zip, 0);
    append_u16(zip, 0);
    append_u16(zip, static_cast<uint16_t>(files.size()));
    append_u16(zip, static_cast<uint16_t>(files.size()));
    append_u32(zip, static_cast<uint32_t>(central.size()));
    append_u32(zip, cd_at);
    append_u16(zip, 0);
    return zip;
}

std::wstring temp_dir() {
    wchar_t buffer[MAX_PATH];
    GetTempPathW(MAX_PATH, buffer);
    std::wstring dir = std::wstring(buffer) + L"valhalla-kb-test\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void write_file(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wrote = 0;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr);
        CloseHandle(file);
    }
}

bool contains(const KnowledgeBase& kb, const std::string& domain, const char* needle) {
    for (const auto& record : kb.records) {
        if (record.kind == "industry" && record.domain == domain && record.status == "imported" && record.body.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

int main() {
    std::wstring dir = temp_dir();
    KnowledgeBase kb;
    sync_notes(kb, "Hand note: keep the session id.");

    write_file(dir + L"shift.txt", "Night shift starts at 22:00.\n\nThe mill keeps a written handover.");
    auto txt = import_industry_file(kb, dir + L"shift.txt", "steel");
    expect(txt.added == 2 && txt.failed == 0, "txt paragraphs");
    auto again = import_industry_file(kb, dir + L"shift.txt", "steel");
    expect(again.added == 0 && again.recognized == 2, "exact text already in the domain is recognized");

    write_file(dir + L"stock.csv", "grade,stock\nQ355,18000\n\"Q235\",\"has, comma\"\n");
    auto csv = import_industry_file(kb, dir + L"stock.csv", "steel");
    expect(csv.added == 2 && contains(kb, "steel", "grade: Q355") && contains(kb, "steel", "has, comma"), "csv rows");

    write_file(dir + L"loom.tsv", "item\tcount\nloom\t48\n");
    auto tsv = import_industry_file(kb, dir + L"loom.tsv", "textile");
    expect(tsv.added == 1 && contains(kb, "textile", "item: loom"), "tsv row");

    write_file(dir + L"page.html", "<script>SECRETTOKEN</script><p>Visible rolling schedule is weekly.</p>");
    auto html = import_industry_file(kb, dir + L"page.html", "steel");
    expect(html.added == 1 && contains(kb, "steel", "weekly") && !contains(kb, "steel", "SECRETTOKEN"), "html drops script");

    write_file(dir + L"rule.json", "[{\"title\":\"Shift rule\",\"body\":\"Night shift starts at 22:00 from json\"}]");
    auto json = import_industry_file(kb, dir + L"rule.json", "steel");
    expect(json.added == 1 && contains(kb, "steel", "from json"), "json object");

    write_file(dir + L"lines.jsonl", "{\"text\":\"Billet length is 9000 mm\"}\n{\"text\":\"Billet length is 9000 mm\"}\n");
    auto jsonl = import_industry_file(kb, dir + L"lines.jsonl", "steel");
    expect(jsonl.added == 1 && jsonl.recognized == 1, "jsonl duplicate inside one file");

    write_file(dir + L"note.xml", "<law>Export licence number is A-19.</law>");
    auto xml = import_industry_file(kb, dir + L"note.xml", "steel");
    expect(xml.added == 1 && contains(kb, "steel", "A-19"), "xml text");

    write_file(dir + L"heat.rtf", "{\\rtf1\\ansi The furnace temperature is 1180 C.}");
    auto rtf = import_industry_file(kb, dir + L"heat.rtf", "steel");
    expect(rtf.added == 1 && contains(kb, "steel", "1180"), "rtf text");

    write_file(dir + L"plain.md", "Slab width is 1500 mm.");
    auto md = import_industry_file(kb, dir + L"plain.md", "steel");
    expect(md.added == 1 && contains(kb, "steel", "1500 mm"), "markdown");

    write_file(dir + L"run.log", "Caster speed is 1.2 m/min.");
    auto log = import_industry_file(kb, dir + L"run.log", "steel");
    expect(log.added == 1 && contains(kb, "steel", "1.2"), "log");

    write_file(dir + L"licence.pdf", "BT (Export licence A-19 stays on the pdf page) Tj ET\n");
    auto pdf = import_industry_file(kb, dir + L"licence.pdf", "steel");
    expect(pdf.added == 1 && contains(kb, "steel", "pdf page"), "pdf literal");

    std::string pdf_text = "BT (Hello coil from flate) Tj ET";
    std::string flate = deflate_store(pdf_text);
    std::string pdf_flate = "%PDF-1.4\n1 0 obj\n<< /Filter /FlateDecode /Length " + std::to_string(flate.size()) +
                            " >>\nstream\n" + flate + "\nendstream\nendobj\n%%EOF\n";
    write_file(dir + L"flate.pdf", pdf_flate);
    auto pdf2 = import_industry_file(kb, dir + L"flate.pdf", "steel");
    expect(pdf2.added == 1 && contains(kb, "steel", "from flate"), "pdf flate");

    std::string docxml = "<?xml version=\"1.0\"?><w:document><w:p><w:r><w:t>Slab width is 1500 mm in the handbook</w:t></w:r></w:p></w:document>";
    write_file(dir + L"handbook.docx", make_zip({{"word/document.xml", docxml}}, true));
    auto docx = import_industry_file(kb, dir + L"handbook.docx", "steel");
    expect(docx.added == 1 && contains(kb, "steel", "handbook"), "docx deflate");

    std::string sheet = "<sst><si><t>coil grade Q355 in the sheet</t></si></sst>";
    write_file(dir + L"grades.xlsx", make_zip({{"xl/sharedStrings.xml", sheet}}, false));
    auto xlsx = import_industry_file(kb, dir + L"grades.xlsx", "steel");
    expect(xlsx.added == 1 && contains(kb, "steel", "Q355 in the sheet"), "xlsx stored");

    std::string slide = "<p:sld><a:t>Furnace temperature is 1180 on the slide</a:t></p:sld>";
    write_file(dir + L"deck.pptx", make_zip({{"ppt/slides/slide1.xml", slide}}, false));
    auto pptx = import_industry_file(kb, dir + L"deck.pptx", "steel");
    expect(pptx.added == 1 && contains(kb, "steel", "on the slide"), "pptx stored");

    write_file(dir + L"old.doc", std::string("\xD0\xCF\x11\xE0 old word", 8));
    auto doc = import_industry_file(kb, dir + L"old.doc", "steel");
    expect(doc.failed == 1 && doc.added == 0, "legacy doc is rejected");

    write_file(dir + L"cn.txt", "\xe9\x92\xa2\xe6\x9d\xbf\xe5\xae\xbd\xe5\xba\xa6\xe4\xb8\xba 1500 \xe6\xaf\xab\xe7\xb1\xb3");
    auto cn = import_industry_file(kb, dir + L"cn.txt", "\xe9\x92\xa2\xe9\x93\x81");
    expect(cn.added == 1, "utf-8 industry text");

    std::string hit = retrieve_industry(kb, "What is the slab width?", 1);
    expect(hit.find("1500") != std::string::npos && hit.find("loom") == std::string::npos, "retrieval picks the matching passage");
    std::string chinese = retrieve_industry(kb, "\xe9\x92\xa2\xe6\x9d\xbf\xe5\xae\xbd\xe5\xba\xa6", 1);
    expect(chinese.find("1500") != std::string::npos, "retrieval matches Chinese");

    std::string corpus = compose_corpus(kb, "Hand note: keep the session id.", "slab width");
    expect(corpus.find("Hand note") != std::string::npos && corpus.find("1500") != std::string::npos, "notes and industry are both offered");

    int before = industry_records(kb);
    sync_notes(kb, "A different handwritten fact.");
    expect(industry_records(kb) == before, "rewriting notes keeps industry records");
    bool note_ok = false;
    bool old_note = false;
    for (const auto& record : kb.records) {
        if (record.kind == "note" && record.body.find("different handwritten") != std::string::npos) note_ok = true;
        if (record.kind == "note" && record.body.find("session id") != std::string::npos) old_note = true;
    }
    expect(note_ok && !old_note, "notes collection is replaced as its own list");

    std::wstring saved = dir + L"knowledge.json";
    expect(save_knowledge(saved, kb), "save json");
    KnowledgeBase loaded;
    expect(load_knowledge(saved, loaded) && industry_records(loaded) == industry_records(kb) && industry_domains(loaded) >= 2, "load json collections");
    bool pending = false;
    for (const auto& record : loaded.records) {
        if (record.kind == "industry" && record.status == "imported") pending = true;
    }
    expect(pending, "imported records stay pending");

    std::cout << (g_failed ? "FAILED\n" : "ALL PASSED\n");
    return g_failed ? 1 : 0;
}
