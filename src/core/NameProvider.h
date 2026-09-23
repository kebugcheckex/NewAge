#pragma once

#include <memory>
#include <vector>

#include <QByteArrayView>
#include <QHash>
#include <QString>
#include <QStringList>

namespace genie {
class LangFile;
}

namespace newage {

// Looks up the game's language strings by ID, e.g. 5142 -> "Castle".
//
// Built from a list of language files, highest priority first: the first file
// with a non-empty string for an ID wins. Two kinds of file are read:
//   - language DLLs (language.dll, language_x1_p1.dll, ...), through genieutils
//   - key-value text files (HD / DE), one `<id> "<text>"` per line
// The kind is taken from the extension (.dll or not).
class NameProvider
{
public:
    NameProvider();
    ~NameProvider();
    NameProvider(const NameProvider &) = delete;
    NameProvider &operator=(const NameProvider &) = delete;

    // Replaces any loaded strings. Files that can't be read are skipped; each
    // one adds a message to `errors` (if given). Returns false if any failed.
    bool load(const QStringList &files, QStringList *errors = nullptr);
    void clear();

    // Files that loaded, highest priority first.
    const QStringList &files() const { return files_; }
    bool isEmpty() const { return sources_.empty(); }

    // Empty if no file has the string, or for a negative ID.
    QString text(int id) const;

    // Parses key-value text (UTF-8). Lines whose key isn't a plain number,
    // e.g. `IDS_OPT_ESC_MENU "..."` or `426071_perth "..."`, are skipped, as
    // are empty strings. `\"`, `\\`, `\n` and `\t` are unescaped.
    static QHash<int, QString> parseKeyValue(QByteArrayView text);

private:
    struct Source
    {
        QHash<int, QString> strings;           // key-value file
        std::unique_ptr<genie::LangFile> dll;  // or language DLL, read on demand
    };

    std::vector<Source> sources_;
    QStringList files_;
    // DLL lookups go through pcrio and iconv, so remember the results.
    mutable QHash<int, QString> dllCache_;
};

} // namespace newage
