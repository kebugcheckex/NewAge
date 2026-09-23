#include "core/NameProvider.h"

#include <exception>
#include <string>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include "genie/lang/LangFile.h"

namespace newage {

namespace {

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// Parses one line of a key-value file. Returns false if it holds no string.
bool parseLine(QByteArrayView line, int *id, QString *text)
{
    if (line.endsWith('\r'))
        line.chop(1);
    qsizetype i = 0;
    while (i < line.size() && isSpace(line[i]))
        ++i;

    const qsizetype keyStart = i;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9')
        ++i;
    // No digits, or the key goes on past them (`IDS_...`, `426071_perth`).
    if (i == keyStart || i == line.size() || !isSpace(line[i]))
        return false;
    bool ok = false;
    *id = line.sliced(keyStart, i - keyStart).toInt(&ok);
    if (!ok)
        return false;

    while (i < line.size() && isSpace(line[i]))
        ++i;
    if (i == line.size() || line[i] != '"')
        return false;
    ++i;

    // Anything after the closing quote (usually a // comment) is ignored. A
    // missing closing quote takes the rest of the line.
    QByteArray value;
    for (; i < line.size() && line[i] != '"'; ++i)
    {
        char c = line[i];
        if (c == '\\' && i + 1 < line.size())
        {
            switch (line[i + 1])
            {
            case '"': c = '"'; ++i; break;
            case '\\': c = '\\'; ++i; break;
            case 'n': c = '\n'; ++i; break;
            case 't': c = '\t'; ++i; break;
            default: break; // Keep unknown escapes as written.
            }
        }
        value += c;
    }
    if (value.isEmpty())
        return false;
    *text = QString::fromUtf8(value);
    return true;
}

// pcrio returns a half-initialised file for input that isn't a PE image, and
// genie::LangFile's destructor then frees garbage pointers. Check the DOS and
// PE signatures first so such files are rejected before they reach pcrio.
bool looksLikePeFile(const QString &path, QString *why)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *why = file.errorString();
        return false;
    }
    const QByteArray dosHeader = file.read(64);
    if (dosHeader.size() < 64 || !dosHeader.startsWith("MZ"))
    {
        *why = QStringLiteral("Not a DLL file.");
        return false;
    }
    // e_lfanew: offset of the "PE\0\0" signature.
    const auto peOffset = qFromLittleEndian<quint32>(dosHeader.constData() + 0x3C);
    if (peOffset < 64 || peOffset > file.size() - 4 || !file.seek(peOffset)
        || file.read(4) != QByteArrayView("PE\0\0", 4))
    {
        *why = QStringLiteral("Not a DLL file.");
        return false;
    }
    return true;
}

} // namespace

NameProvider::NameProvider() = default;

NameProvider::~NameProvider() = default;

bool NameProvider::load(const QStringList &files, QStringList *errors)
{
    clear();
    bool allLoaded = true;
    const auto fail = [&](const QString &path, const QString &why) {
        allLoaded = false;
        if (errors)
            errors->append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), why));
    };

    for (const QString &path : files)
    {
        Source source;
        if (QFileInfo(path).suffix().compare(QStringLiteral("dll"), Qt::CaseInsensitive) == 0)
        {
            QString why;
            if (!looksLikePeFile(path, &why))
            {
                fail(path, why);
                continue;
            }
            auto dll = std::make_unique<genie::LangFile>();
            try
            {
                // Same narrow-path limitation as Session: ANSI code page only.
                dll->load(QFile::encodeName(path).constData());
            }
            catch (const std::exception &e)
            {
                fail(path, QString::fromLocal8Bit(e.what()));
                continue;
            }
            catch (const std::string &e) // LangFile throws these too.
            {
                fail(path, QString::fromLocal8Bit(e));
                continue;
            }
            source.dll = std::move(dll);
        }
        else
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
            {
                fail(path, file.errorString());
                continue;
            }
            source.strings = parseKeyValue(file.readAll());
        }
        sources_.push_back(std::move(source));
        files_.append(path);
    }
    return allLoaded;
}

void NameProvider::clear()
{
    sources_.clear();
    files_.clear();
    dllCache_.clear();
}

QString NameProvider::text(int id) const
{
    if (id < 0)
        return {};

    if (auto cached = dllCache_.constFind(id); cached != dllCache_.cend())
        return *cached;

    bool usedDll = false;
    QString result;
    for (const Source &source : sources_)
    {
        if (source.dll)
        {
            usedDll = true;
            try
            {
                result = QString::fromStdString(source.dll->getString(static_cast<unsigned int>(id)));
            }
            catch (...)
            {
                // A string iconv can't convert; treat it as missing.
                result.clear();
            }
        }
        else
        {
            result = source.strings.value(id);
        }
        if (!result.isEmpty())
            break;
    }
    if (usedDll)
        dllCache_.insert(id, result);
    return result;
}

QHash<int, QString> NameProvider::parseKeyValue(QByteArrayView text)
{
    // Skip a UTF-8 byte order mark.
    if (text.startsWith("\xEF\xBB\xBF"))
        text = text.sliced(3);

    QHash<int, QString> strings;
    qsizetype start = 0;
    while (start < text.size())
    {
        qsizetype end = text.indexOf('\n', start);
        if (end < 0)
            end = text.size();
        int id = 0;
        QString value;
        if (parseLine(text.sliced(start, end - start), &id, &value))
            strings.insert(id, value);
        start = end + 1;
    }
    return strings;
}

} // namespace newage
