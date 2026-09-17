//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Project material discovery, compiler invocation, and thumbnails.
//////////////////////////////////////////////////////////////////////////
#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QPushButton;
class QSpinBox;

namespace cypher::tools::tile_editor
{

class CypherTileMaterialBrowser final : public QWidget
{
public:
    explicit CypherTileMaterialBrowser( QWidget *parent = nullptr );
    ~CypherTileMaterialBrowser() override;

    QString sourceRoot() const;
    QString cookedRoot() const;
    void setSourceRoot( const QString &root );
    void refresh();
    void cookSelected();
    void cancelPendingAssignment();

    void setAssignCallback( std::function<void( unsigned short, const QString & )> callback );
    void setApplyCallback( std::function<void( unsigned short )> callback );
    void setReloadCallback( std::function<void( const QString & )> callback );
    void setStatusCallback( std::function<void( const QString &, bool )> callback );

private:
    void assignSelected();
    void applyFilter();
    void updateSelection();
    void appendCompilerOutput( QProcess *process );
    void reportError( const QString &message );
    bool isBusy() const;
    QString selectedPath() const;

    QLineEdit *m_root{ nullptr };
    QLineEdit *m_filter{ nullptr };
    QListWidget *m_list{ nullptr };
    QSpinBox *m_slot{ nullptr };
    QLabel *m_details{ nullptr };
    QPushButton *m_cook{ nullptr };
    QPushButton *m_assign{ nullptr };
    QPushButton *m_apply{ nullptr };
    QPushButton *m_clear{ nullptr };
    QProcess *m_process{ nullptr };
    QString m_cookedRoot{};
    QString m_compilerLog{};
    QString m_pendingAssignPath{};
    unsigned short m_pendingAssignSlot{ 0 };

    std::function<void( unsigned short, const QString & )> m_assignCallback{};
    std::function<void( unsigned short )> m_applyCallback{};
    std::function<void( const QString & )> m_reloadCallback{};
    std::function<void( const QString &, bool )> m_statusCallback{};
};

} // namespace cypher::tools::tile_editor
