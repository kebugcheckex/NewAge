#pragma once

#include <QList>
#include <QString>

#include "model/FieldDesc.h"

namespace genie {
class Tech;
}

namespace newage {

// A tech together with its ID, which genie::Tech doesn't store: the ID is the
// tech's index in DatFile::Techs. The reference is non-const so that editable
// fields can write through it.
struct TechRef
{
    int id;
    genie::Tech &tech;
};

// Descriptors for the tech fields shown in the property view, in display
// order. Groups appear in the order of their first field.
const QList<FieldDesc<TechRef>> &techFields();

// "2 - Age" style label for a tech Type value.
QString techTypeName(int type);

} // namespace newage
