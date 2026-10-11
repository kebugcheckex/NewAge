#include "model/FieldDesc.h"

#include <cmath>

namespace newage {

namespace {

ParsedField rejectNumber()
{
    return {{}, QStringLiteral("bad_value"), QStringLiteral("not a number")};
}

ParsedField rejectRange(int minimum, int maximum)
{
    return {{}, QStringLiteral("out_of_range"),
            QStringLiteral("accepts %1..%2").arg(minimum).arg(maximum)};
}

ParsedField parseText(const FieldValueDesc &field, const QVariant &input)
{
    if (input.typeId() != QMetaType::QString)
        return {{}, QStringLiteral("bad_value"), QStringLiteral("not text")};
    const QString text = input.toString();
    for (const QChar c : text)
    {
        if (c.unicode() == 0 || c.unicode() > 0xFF)
            return {{}, QStringLiteral("bad_value"), QStringLiteral("not Latin-1 text")};
    }
    if (text.size() > field.maximum)
        return {{}, QStringLiteral("out_of_range"), QStringLiteral("accepts up to %1 characters").arg(field.maximum)};
    return {QVariant(text)};
}

} // namespace

ParsedField parseFieldValue(const FieldValueDesc &field, const QVariant &input)
{
    if (field.typeId == QMetaType::QString)
        return parseText(field, input);
    // An already-typed number is used as-is, so a writer can hand set() the
    // value FieldTreeModel parsed without a locale-dependent round trip.
    if (field.typeId == QMetaType::Float
        && (input.typeId() == QMetaType::Float || input.typeId() == QMetaType::Double))
    {
        const float number = input.toFloat();
        if (!std::isfinite(number))
            return rejectNumber();
        return {QVariant(number)};
    }
    if (field.typeId != QMetaType::Float
        && (input.typeId() == QMetaType::Int || input.typeId() == QMetaType::LongLong
            || input.typeId() == QMetaType::UInt || input.typeId() == QMetaType::ULongLong))
    {
        bool ok = false;
        const qlonglong number = input.toLongLong(&ok);
        if (!ok)
            return rejectNumber();
        if (number < field.minimum || number > field.maximum)
            return rejectRange(field.minimum, field.maximum);
        return {QVariant(static_cast<int>(number))};
    }

    // Going through text also accepts other numeric QVariants.
    const QString text = input.toString().trimmed();
    bool ok = false;
    if (field.typeId == QMetaType::Float)
    {
        const float number = text.toFloat(&ok);
        if (!ok || !std::isfinite(number))
            return rejectNumber();
        return {QVariant(number)};
    }

    const qlonglong number = text.toLongLong(&ok);
    if (!ok)
        return rejectNumber();
    if (number < field.minimum || number > field.maximum)
        return rejectRange(field.minimum, field.maximum);
    return {QVariant(static_cast<int>(number))};
}

} // namespace newage
