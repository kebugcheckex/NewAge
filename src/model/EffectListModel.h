#pragma once

#include "model/EntityListModel.h"

namespace genie {
class Effect;
}

namespace newage {

// All effects (DatFile::Effects is global, not per civ), one row per effect,
// so row == effect ID. Effects have no language string; the label is the
// internal name, else "(unnamed)".
class EffectListModel : public EntityListModel
{
    Q_OBJECT

public:
    explicit EffectListModel(Session *session, QObject *parent = nullptr);

    // nullptr when `row` is out of range or no data is open.
    const genie::Effect *effect(int row) const;

    QString name(int row) const override;
    void showFields(int row, FieldTreeModel &fields) override;

    int rowCount(const QModelIndex &parent = {}) const override;

protected:
    bool isActive(int row) const override;
    QString internalName(int row) const override;
};

} // namespace newage
