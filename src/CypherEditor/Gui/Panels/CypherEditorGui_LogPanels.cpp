//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_LogPanels.cpp
//  Purpose: Implements the Problems and Output panels.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_LogPanels.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

QToolButton *Toggle( QWidget *pParent, const editor_style_t &style, const char *pIcon, const QString &tip )
{
    auto *pButton = new QToolButton( pParent );
    pButton->setCheckable( true );
    pButton->setChecked( true );
    pButton->setAutoRaise( true );
    pButton->setIcon( EditorStyle_Icon( style, pIcon ) );
    pButton->setIconSize( QSize( 16, 16 ) );
    pButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    pButton->setToolTip( tip );
    return pButton;
}

QToolButton *Action( QWidget *pParent, const editor_style_t &style, const char *pIcon, const QString &tip )
{
    auto *pButton = new QToolButton( pParent );
    pButton->setAutoRaise( true );
    pButton->setIcon( EditorStyle_Icon( style, pIcon ) );
    pButton->setIconSize( QSize( 16, 16 ) );
    pButton->setToolTip( tip );
    return pButton;
}

// ---------------------------------------------------------------------------
// Problems
// ---------------------------------------------------------------------------

class problems_t final : public QWidget {
public:
    problems_t( QWidget *pParent, const editor_style_t *pStyle, editor_log_t *pLog ) : QWidget( pParent ), m_pStyle( pStyle ), m_pLog( pLog )
    {
        setObjectName( QStringLiteral( "EditorProblems" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 0 );
        auto *pBar = new QWidget( this );
        pBar->setObjectName( QStringLiteral( "EditorConsoleToolbar" ) );
        auto *pBarLayout = new QHBoxLayout( pBar );
        pBarLayout->setContentsMargins( 4, 2, 4, 2 );
        pBarLayout->setSpacing( 2 );
        m_pErrors = Toggle( pBar, *pStyle, "log-error", QStringLiteral( "Show errors" ) );
        m_pWarnings = Toggle( pBar, *pStyle, "log-warning", QStringLiteral( "Show warnings" ) );
        m_pSearch = new QLineEdit( pBar );
        m_pSearch->setPlaceholderText( QStringLiteral( "Filter problems" ) );
        m_pSearch->setClearButtonEnabled( true );
        QToolButton *pClear = Action( pBar, *pStyle, "log-clear", QStringLiteral( "Clear the log" ) );
        pBarLayout->addWidget( m_pErrors );
        pBarLayout->addWidget( m_pWarnings );
        pBarLayout->addWidget( m_pSearch, 1 );
        pBarLayout->addWidget( pClear );
        m_pTree = new QTreeWidget( this );
        m_pTree->setObjectName( QStringLiteral( "EditorProblemsList" ) );
        m_pTree->setColumnCount( 4 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Description" ), QStringLiteral( "Channel" ), QStringLiteral( "Location" ), QStringLiteral( "Time" ) } );
        m_pTree->setRootIsDecorated( false );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setAlternatingRowColors( true );
        m_pTree->header()->setStretchLastSection( false );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        for ( int c = 1; c < 4; ++c ) { m_pTree->header()->setSectionResizeMode( c, QHeaderView::ResizeToContents ); }
        m_pTree->setToolTip( QStringLiteral( "Double-click a problem to copy its location" ) );
        pLayout->addWidget( pBar );
        pLayout->addWidget( m_pTree, 1 );
        QObject::connect( m_pErrors, &QToolButton::toggled, this, [this]( bool ) { Rebuild(); } );
        QObject::connect( m_pWarnings, &QToolButton::toggled, this, [this]( bool ) { Rebuild(); } );
        QObject::connect( m_pSearch, &QLineEdit::textChanged, this, [this]( const QString & ) { Rebuild(); } );
        QObject::connect( pClear, &QToolButton::clicked, this, [this]() { EditorLog_Clear( m_pLog ); } );
        QObject::connect( m_pTree, &QTreeWidget::itemDoubleClicked, this, []( QTreeWidgetItem *pItem, int ) {
            if ( !pItem->text( 2 ).isEmpty() ) { QApplication::clipboard()->setText( pItem->data( 2, Qt::UserRole ).toString() ); }
        } );
        ( void )EditorLog_AddListener( pLog, &problems_t::OnLog, this );
        Rebuild();
    }

    ~problems_t() override { EditorLog_RemoveListener( m_pLog, &problems_t::OnLog, this ); }

    QStringList Rows() const
    {
        QStringList rows;
        for ( int i = 0; i < m_pTree->topLevelItemCount(); ++i ) {
            const QTreeWidgetItem *pItem = m_pTree->topLevelItem( i );
            rows.append( QStringLiteral( "%1 %2 %3" ).arg( pItem->data( 0, Qt::UserRole ).toString(), pItem->text( 1 ), pItem->text( 0 ) ) );
        }
        return rows;
    }

private:
    static void OnLog( void *pContext, const editor_log_entry_t *pEntry ) noexcept
    {
        auto *pPanel = static_cast<problems_t *>( pContext );
        if ( pEntry == nullptr ) {
            pPanel->Rebuild();
            return;
        }
        pPanel->UpdateCounts();
        if ( pPanel->Passes( *pEntry ) ) { pPanel->AddRow( *pEntry ); }
    }

    bool Passes( const editor_log_entry_t &entry ) const
    {
        if ( entry.level < log_level_t::Warning || entry.level >= log_level_t::Count ) { return false; }
        if ( entry.level == log_level_t::Warning ? !m_pWarnings->isChecked() : !m_pErrors->isChecked() ) { return false; }
        const QString search = m_pSearch->text().trimmed();
        return search.isEmpty() || entry.message.contains( search, Qt::CaseInsensitive );
    }

    void AddRow( const editor_log_entry_t &entry )
    {
        auto *pItem = new QTreeWidgetItem( m_pTree );
        pItem->setIcon( 0, EditorStyle_Icon( *m_pStyle, entry.level == log_level_t::Warning ? "log-warning" : "log-error" ) );
        pItem->setText( 0, entry.message );
        pItem->setToolTip( 0, entry.message );
        pItem->setData( 0, Qt::UserRole, QString::fromLatin1( EditorLog_LevelTag( entry.level ) ) );
        pItem->setText( 1, QString::fromUtf8( Cy_LogChannelName( entry.channel ) ) );
        if ( !entry.file.isEmpty() ) {
            pItem->setText( 2, QStringLiteral( "%1:%2" ).arg( QFileInfo( entry.file ).fileName() ).arg( entry.line ) );
            pItem->setData( 2, Qt::UserRole, QStringLiteral( "%1:%2" ).arg( entry.file ).arg( entry.line ) );
            pItem->setToolTip( 2, pItem->data( 2, Qt::UserRole ).toString() );
        }
        pItem->setText( 3, QStringLiteral( "%1 s" ).arg( entry.msTime / 1000.0, 0, 'f', 2 ) );
        pItem->setForeground( 0, EditorStyle_TokenColor( *m_pStyle, entry.level == log_level_t::Warning ? "console.warning" : "console.error" ) );
    }

    void UpdateCounts()
    {
        m_pErrors->setText( QStringLiteral( "Errors %1" ).arg( EditorLog_Count( m_pLog, log_level_t::Error ) + EditorLog_Count( m_pLog, log_level_t::Fatal ) ) );
        m_pWarnings->setText( QStringLiteral( "Warnings %1" ).arg( EditorLog_Count( m_pLog, log_level_t::Warning ) ) );
    }

    void Rebuild()
    {
        m_pTree->clear();
        for ( const editor_log_entry_t &entry : m_pLog->entries ) {
            if ( Passes( entry ) ) { AddRow( entry ); }
        }
        UpdateCounts();
    }

    const editor_style_t *m_pStyle{ nullptr };
    editor_log_t *m_pLog{ nullptr };
    QToolButton *m_pErrors{ nullptr };
    QToolButton *m_pWarnings{ nullptr };
    QLineEdit *m_pSearch{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
};

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

// Which channels each Output source shows.
bool InSource( const QString &source, log_channel_t channel )
{
    using c = log_channel_t;
    static const struct {
        const char *pName;
        c channels[12];
        int nChannels;
    } kSources[]{
        { "Editor", { c::Editor, c::Gui, c::Command, c::Config, c::CVar }, 5 },
        { "Build", { c::Tools, c::Resource, c::Asset, c::Material, c::Texture, c::Serialization }, 6 },
        { "Game", { c::Game, c::Host, c::Script, c::Entity, c::World, c::AI, c::Animation, c::Input }, 8 },
        { "Engine", { c::Common, c::Memory, c::FileSystem, c::Pak, c::Render, c::Audio, c::Network, c::Physics, c::System, c::Job, c::Reflection }, 11 },
    };
    if ( source == QLatin1String( "All" ) ) { return true; }
    for ( const auto &entry : kSources ) {
        if ( source != QLatin1String( entry.pName ) ) { continue; }
        for ( int i = 0; i < entry.nChannels; ++i ) {
            if ( entry.channels[i] == channel ) { return true; }
        }
    }
    return false;
}

class output_t final : public QWidget {
public:
    output_t( QWidget *pParent, const editor_style_t *pStyle, editor_log_t *pLog ) : QWidget( pParent ), m_pStyle( pStyle ), m_pLog( pLog )
    {
        setObjectName( QStringLiteral( "EditorOutput" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 0 );
        auto *pBar = new QWidget( this );
        pBar->setObjectName( QStringLiteral( "EditorConsoleToolbar" ) );
        auto *pBarLayout = new QHBoxLayout( pBar );
        pBarLayout->setContentsMargins( 4, 2, 4, 2 );
        pBarLayout->setSpacing( 4 );
        auto *pLabel = new QLabel( QStringLiteral( "Show output from:" ), pBar );
        pLabel->setProperty( "muted", true );
        m_pSource = new QComboBox( pBar );
        m_pSource->setObjectName( QStringLiteral( "EditorOutputSource" ) );
        m_pSource->addItems( { QStringLiteral( "All" ), QStringLiteral( "Editor" ), QStringLiteral( "Build" ), QStringLiteral( "Game" ), QStringLiteral( "Engine" ) } );
        QToolButton *pCopy = Action( pBar, *pStyle, "edit-copy", QStringLiteral( "Copy the output" ) );
        pBarLayout->addWidget( pLabel );
        pBarLayout->addWidget( m_pSource );
        pBarLayout->addStretch( 1 );
        pBarLayout->addWidget( pCopy );
        m_pText = new QPlainTextEdit( this );
        m_pText->setObjectName( QStringLiteral( "EditorOutputText" ) );
        m_pText->setReadOnly( true );
        m_pText->setFont( pStyle->consoleFont );
        m_pText->setLineWrapMode( QPlainTextEdit::NoWrap );
        m_pText->setMaximumBlockCount( 10000 );
        pLayout->addWidget( pBar );
        pLayout->addWidget( m_pText, 1 );
        QObject::connect( m_pSource, &QComboBox::currentTextChanged, this, [this]( const QString & ) { Rebuild(); } );
        QObject::connect( pCopy, &QToolButton::clicked, this, [this]() { QApplication::clipboard()->setText( m_pText->toPlainText() ); } );
        ( void )EditorLog_AddListener( pLog, &output_t::OnLog, this );
        Rebuild();
    }

    ~output_t() override { EditorLog_RemoveListener( m_pLog, &output_t::OnLog, this ); }

    void SetSource( const QString &source ) { m_pSource->setCurrentText( source ); }
    QString Text() const { return m_pText->toPlainText(); }

private:
    static void OnLog( void *pContext, const editor_log_entry_t *pEntry ) noexcept
    {
        auto *pPanel = static_cast<output_t *>( pContext );
        if ( pEntry == nullptr ) {
            pPanel->m_pText->clear();
            return;
        }
        if ( !pPanel->Passes( *pEntry ) ) { return; }
        QScrollBar *pScroll = pPanel->m_pText->verticalScrollBar();
        const bool bAtBottom = pScroll->value() >= pScroll->maximum() - 2;
        pPanel->Append( *pEntry );
        if ( bAtBottom ) { pScroll->setValue( pScroll->maximum() ); }
    }

    bool Passes( const editor_log_entry_t &entry ) const
    {
        return !entry.bCommand && entry.level >= log_level_t::Info && InSource( m_pSource->currentText(), entry.channel );
    }

    // Output reads like a tool's: plain lines, with warnings and errors
    // marked and coloured.
    void Append( const editor_log_entry_t &entry )
    {
        QTextCursor cursor( m_pText->document() );
        cursor.movePosition( QTextCursor::End );
        if ( !m_pText->document()->isEmpty() ) { cursor.insertBlock(); }
        QTextCharFormat format;
        QString line = entry.message;
        if ( entry.level == log_level_t::Warning ) {
            format.setForeground( EditorStyle_TokenColor( *m_pStyle, "console.warning" ) );
            line = QStringLiteral( "warning: " ) + line;
        } else if ( entry.level >= log_level_t::Error ) {
            format.setForeground( EditorStyle_TokenColor( *m_pStyle, "console.error" ) );
            line = QStringLiteral( "error: " ) + line;
        } else {
            format.setForeground( EditorStyle_TokenColor( *m_pStyle, "console.text" ) );
        }
        cursor.insertText( line, format );
    }

    void Rebuild()
    {
        m_pText->clear();
        for ( const editor_log_entry_t &entry : m_pLog->entries ) {
            if ( Passes( entry ) ) { Append( entry ); }
        }
    }

    const editor_style_t *m_pStyle{ nullptr };
    editor_log_t *m_pLog{ nullptr };
    QComboBox *m_pSource{ nullptr };
    QPlainTextEdit *m_pText{ nullptr };
};

template <typename T>
T *As( QWidget *pWidget )
{
    auto *pImpl = dynamic_cast<T *>( pWidget );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QWidget *EditorProblems_Create( QWidget *pParent, const editor_style_t *pStyle, editor_log_t *pLog )
{
    CY_ASSERT( pStyle != nullptr && pLog != nullptr );
    return new problems_t( pParent, pStyle, pLog );
}

QStringList EditorProblems_Rows( QWidget *pPanel )
{
    auto *pImpl = As<problems_t>( pPanel );
    return pImpl != nullptr ? pImpl->Rows() : QStringList{};
}

QWidget *EditorOutput_Create( QWidget *pParent, const editor_style_t *pStyle, editor_log_t *pLog )
{
    CY_ASSERT( pStyle != nullptr && pLog != nullptr );
    return new output_t( pParent, pStyle, pLog );
}

void EditorOutput_SetSource( QWidget *pPanel, const QString &source )
{
    if ( auto *pImpl = As<output_t>( pPanel ) ) { pImpl->SetSource( source ); }
}

QString EditorOutput_Text( QWidget *pPanel )
{
    auto *pImpl = As<output_t>( pPanel );
    return pImpl != nullptr ? pImpl->Text() : QString();
}

} // namespace cypher::editor::gui
