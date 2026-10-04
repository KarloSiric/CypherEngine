//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingsDialog.cpp
//  Purpose: Implements the generated settings dialog.
//  Details: Rows are rebuilt when the page or search changes and refreshed
//           in place when a value changes, so typing in a spin box never
//           loses focus to a rebuild. The dialog listens to the registry
//           like any other part of the editor; its own edits come back
//           through that listener, guarded so a refresh does not write the
//           value again. Custom pages sit in a stack beside the generated
//           form, so switching pages never deletes them.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//  - 2026-10-01: custom pages
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_SettingsDialog.h"
#include "CypherEditorGui_Application.h"
#include "CypherEditorGui_Style.h"

#include "CypherEditor_FuzzyMatch.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <memory>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr int kPageRole = Qt::UserRole;

const char *PageIcon( const QString &page )
{
    const QString name = page.section( QLatin1Char( '/' ), -1 );
    if ( name == QStringLiteral( "Appearance" ) ) { return "theme-editor"; }
    if ( name == QStringLiteral( "Keybindings" ) ) { return "keymap"; }
    if ( name == QStringLiteral( "Camera" ) ) { return "tool-camera"; }
    if ( name == QStringLiteral( "Grid and Snapping" ) ) { return "snap-grid"; }
    if ( name == QStringLiteral( "2D Display" ) ) { return "view-top"; }
    if ( name == QStringLiteral( "3D Display" ) || name == QStringLiteral( "Viewports" ) ) { return "view-3d"; }
    if ( name == QStringLiteral( "Selection and Gizmos" ) ) { return "tool-translate"; }
    if ( name == QStringLiteral( "Command Palette" ) ) { return "command-palette"; }
    if ( name == QStringLiteral( "Files" ) ) { return "file-open"; }
    if ( name == QStringLiteral( "Assets" ) ) { return "asset-folder"; }
    if ( name == QStringLiteral( "Console" ) ) { return "view-console"; }
    if ( name == QStringLiteral( "Plugins" ) ) { return "plugins"; }
    if ( name == QStringLiteral( "Audio" ) ) { return "entity-sound"; }
    if ( name == QStringLiteral( "Map Editing" ) ) { return "tool-block"; }
    return "settings";
}

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

// Borrows the bytes; the caller keeps them alive for the call.
string_view_t ViewOf( const QByteArray &bytes ) noexcept
{
    return string_view_t{ bytes.constData(), static_cast<usize>( bytes.size() ) };
}

QColor FromRgba( u32 rgba )
{
    return QColor( static_cast<int>( ( rgba >> 24u ) & 0xFFu ), static_cast<int>( ( rgba >> 16u ) & 0xFFu ), static_cast<int>( ( rgba >> 8u ) & 0xFFu ),
                   static_cast<int>( rgba & 0xFFu ) );
}

// Reals as the settings files write them: "75" and "0.15", with a dot
// whatever the system locale, since the same numbers appear in files and
// docs.
class real_spin_t final : public QDoubleSpinBox {
public:
    explicit real_spin_t( QWidget *pParent ) : QDoubleSpinBox( pParent )
    {
        setLocale( QLocale::c() );
        setDecimals( 4 );
    }

protected:
    QString textFromValue( double value ) const override { return QLocale::c().toString( value, 'g', 10 ); }
};

class settings_dialog_t final : public QDialog {
public:
    settings_dialog_t( QWidget *pParent, settings_registry_t *pRegistry, const editor_style_t *pStyle )
        : QDialog( pParent ), m_pRegistry( pRegistry ), m_pStyle( pStyle )
    {
        CY_ASSERT( pRegistry != nullptr );
        setObjectName( QStringLiteral( "EditorSettingsDialog" ) );
        setWindowTitle( QStringLiteral( "Settings" ) );
        QSize initialSize( 1260, 850 );
        if ( const auto *pScreen = pParent != nullptr ? pParent->screen() : QGuiApplication::primaryScreen() ) {
            const auto available = pScreen->availableGeometry().size();
            initialSize = initialSize.boundedTo( QSize( std::max( 1, available.width() - 64 ), std::max( 1, available.height() - 64 ) ) );
        }
        resize( initialSize ); setSizeGripEnabled( true );
        EditorGui_RegisterResources();

        auto *pRoot = new QVBoxLayout( this );
        pRoot->setContentsMargins( 14, 14, 14, 12 );
        pRoot->setSpacing( 12 );
        auto *pTop = new QHBoxLayout();
        m_pSearch = new QLineEdit( this );
        m_pSearch->setObjectName( QStringLiteral( "SettingsSearch" ) );
        m_pSearch->setPlaceholderText( QStringLiteral( "Search settings and pages…" ) );
        m_pSearch->setAccessibleName( QStringLiteral( "Search all settings" ) );
        m_pSearch->setClearButtonEnabled( true );
        m_pTarget = new QComboBox( this );
        m_pTarget->setObjectName( QStringLiteral( "SettingsTargetScope" ) );
        m_pTarget->setToolTip( QStringLiteral( "Choose where edits and resets are saved. A narrower scope can override these values." ) );
        m_pOverrides = new QCheckBox( QStringLiteral( "Overrides only" ), this );
        m_pOverrides->setObjectName( QStringLiteral( "SettingsOverridesOnly" ) );
        m_pOverrides->setToolTip( QStringLiteral( "Show values stored in the selected Save to scope, including invalid overrides that can be reset." ) );
        pTop->addWidget( m_pSearch, 1 );
        pTop->addWidget( m_pOverrides );
        pTop->addWidget( new QLabel( QStringLiteral( "Save to" ), this ) );
        pTop->addWidget( m_pTarget );
        pRoot->addLayout( pTop );

        auto *pSplitter = new QSplitter( Qt::Horizontal, this );
        m_pPages = new QTreeWidget( pSplitter );
        m_pPages->setHeaderHidden( true );
        m_pPages->setObjectName( QStringLiteral( "SettingsCategories" ) );
        m_pPages->setIconSize( QSize( 20, 20 ) );
        m_pPages->setIndentation( 16 );
        m_pPages->setMinimumWidth( 190 );
        auto *pContent = new QWidget( pSplitter );
        auto *pContentLayout = new QVBoxLayout( pContent );
        pContentLayout->setContentsMargins( 6, 0, 0, 0 );
        pContentLayout->setSpacing( 10 );
        m_pBreadcrumb = new QLabel( pContent );
        m_pBreadcrumb->setObjectName( QStringLiteral( "SettingsBreadcrumb" ) );
        m_pBreadcrumb->setProperty( "muted", true );
        m_pTitle = new QLabel( pContent );
        m_pTitle->setObjectName( QStringLiteral( "SettingsPageTitle" ) );
        m_pTitle->setTextFormat( Qt::PlainText );
        m_pSummary = new QLabel( pContent );
        m_pSummary->setObjectName( QStringLiteral( "SettingsPageSummary" ) );
        m_pSummary->setProperty( "muted", true );
        m_pSummary->setWordWrap( true );
        pContentLayout->addWidget( m_pBreadcrumb );
        pContentLayout->addWidget( m_pTitle );
        pContentLayout->addWidget( m_pSummary );
        m_pStack = new QStackedWidget( pContent );
        m_pScroll = new QScrollArea( m_pStack );
        m_pScroll->setWidgetResizable( true );
        m_pScroll->setFrameShape( QFrame::NoFrame );
        m_pStack->addWidget( m_pScroll );
        pSplitter->addWidget( m_pPages );
        pContentLayout->addWidget( m_pStack, 1 );
        pSplitter->addWidget( pContent );
        pSplitter->setStretchFactor( 1, 1 );
        pSplitter->setSizes( { 220, 1040 } );
        pRoot->addWidget( pSplitter, 1 );

        auto *pFind = new QShortcut( QKeySequence::Find, this );
        QObject::connect( pFind, &QShortcut::activated, this, [this]() { m_pSearch->setFocus(); m_pSearch->selectAll(); } );

        auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close, this );
        auto *pImport = pButtons->addButton( QStringLiteral( "Import Settings…" ), QDialogButtonBox::ActionRole );
        pImport->setObjectName( QStringLiteral( "SettingsImport" ) );
        pImport->setIcon( Icon( "file-import" ) );
        pImport->setToolTip( QStringLiteral( "Review a .cysettings file before merging it into the selected Save to scope." ) );
        auto *pExport = pButtons->addButton( QStringLiteral( "Export Settings…" ), QDialogButtonBox::ActionRole );
        pExport->setObjectName( QStringLiteral( "SettingsExport" ) );
        pExport->setIcon( Icon( "file-export" ) );
        pExport->setToolTip( QStringLiteral( "Export every effective setting. Save and share theme/keymap files from Appearance and Keybindings separately." ) );
        pRoot->addWidget( pButtons );
        QObject::connect( pImport, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName( this, QStringLiteral( "Import Settings" ), {}, QStringLiteral( "Editor settings (*.cysettings)" ) );
            if ( path.isEmpty() ) { return; }
            std::unique_ptr<QDialog> review( EditorSettingsTransfer_CreateImport( this, m_pRegistry, path, Target(), m_transferHooks ) );
            ( void )review->exec();
        } );
        QObject::connect( pExport, &QPushButton::clicked, this, [this]() {
            QString path = QFileDialog::getSaveFileName( this, QStringLiteral( "Export Settings" ), QStringLiteral( "mason_settings.cysettings" ),
                                                        QStringLiteral( "Editor settings (*.cysettings)" ) );
            if ( path.isEmpty() ) { return; }
            if ( !path.endsWith( QStringLiteral( ".cysettings" ), Qt::CaseInsensitive ) ) { path += QStringLiteral( ".cysettings" ); }
            QString error;
            if ( EditorSettingsTransfer_Export( m_pRegistry, path, &error ) != settings_transfer_status_t::OK ) {
                QMessageBox::warning( this, QStringLiteral( "Export Settings" ), error );
            }
        } );
        QObject::connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
        QObject::connect( m_pSearch, &QLineEdit::textChanged, this, [this]( const QString & ) { Rebuild(); } );
        QObject::connect( m_pPages, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) {
            if ( pItem == nullptr ) { return; }
            m_page = pItem->data( 0, kPageRole ).toString();
            if ( !m_pSearch->text().isEmpty() ) {
                const QSignalBlocker blocker( m_pSearch );
                m_pSearch->clear();
            }
            Rebuild();
        } );
        QObject::connect( m_pTarget, &QComboBox::currentIndexChanged, this, [this]( int ) {
            if ( m_pOverrides->isChecked() ) { Rebuild(); } else { RefreshAll(); UpdateHeader(); }
        } );
        QObject::connect( m_pOverrides, &QCheckBox::toggled, this, [this]() { Rebuild(); } );

        FillTargets();
        FillPages();
        ( void )EditorSettings_AddListener( pRegistry, &settings_dialog_t::OnChanged, this );
    }

    ~settings_dialog_t() override { EditorSettings_RemoveListener( m_pRegistry, &settings_dialog_t::OnChanged, this ); }

    void ShowPage( const QString &page )
    {
        const QSignalBlocker blocker( m_pSearch );
        m_pSearch->clear();
        m_page = page;
        for ( QTreeWidgetItemIterator it( m_pPages ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, kPageRole ).toString() == page ) {
                const QSignalBlocker pagesBlocker( m_pPages );
                m_pPages->setCurrentItem( *it );
            }
        }
        Rebuild();
    }

    void SetSearch( const QString &text ) { m_pSearch->setText( text ); }
    void SetTransferHooks( const settings_transfer_hooks_t &hooks ) { m_transferHooks = hooks; }

    void AddPage( const QString &page, QWidget *pWidget, const QStringList &keywords, editor_settings_page_close_fn pfnCanClose )
    {
        CY_ASSERT( pWidget != nullptr && !page.isEmpty() && FindCustom( page ) == nullptr );
        if ( pWidget == nullptr || page.isEmpty() || FindCustom( page ) != nullptr ) { return; }
        m_pStack->addWidget( pWidget );
        m_custom.push_back( custom_page_t{ page, pWidget, keywords, pfnCanClose } );
        auto *pItem = new QTreeWidgetItem();
        pItem->setText( 0, page );
        pItem->setData( 0, kPageRole, page );
        pItem->setIcon( 0, Icon( PageIcon( page ) ) );
        QFont font = pItem->font( 0 );
        font.setBold( true ); // Custom pages are the hand-built ones; they lead the list.
        pItem->setFont( 0, font );
        const QSignalBlocker blocker( m_pPages );
        m_pPages->insertTopLevelItem( static_cast<int>( m_custom.size() ) - 1, pItem );
        if ( m_custom.size() == 1u ) { ShowPage( page ); }
    }

    QString CurrentPage() const { return m_pSearch->text().trimmed().isEmpty() ? m_page : QString(); }

    QStringList PageResults() const { return m_pageResults; }

    void SetTargetScope( settings_scope_t scope )
    {
        const int index = m_pTarget->findData( static_cast<int>( scope ) );
        if ( index >= 0 ) { m_pTarget->setCurrentIndex( index ); }
    }

    QStringList VisibleSettings() const
    {
        QStringList paths;
        for ( const row_t &row : m_rows ) { paths.append( QString::fromUtf8( row.pDescriptor->pPath ) ); }
        return paths;
    }

    QWidget *EditorFor( const QString &path ) const
    {
        const row_t *pRow = FindRow( path );
        return pRow != nullptr ? pRow->pEditor : nullptr;
    }

    QString SourceFor( const QString &path ) const
    {
        const row_t *pRow = FindRow( path );
        return pRow != nullptr ? pRow->pSource->text() : QString();
    }

    void Reset( const QString &path )
    {
        const row_t *pRow = FindRow( path );
        if ( pRow != nullptr ) { pRow->pReset->click(); }
    }

private:
    struct custom_page_t {
        QString page{};
        QWidget *pWidget{ nullptr };
        QStringList keywords{};
        editor_settings_page_close_fn pfnCanClose{ nullptr };
    };

    void done( int result ) override
    {
        for ( const custom_page_t &custom : m_custom ) {
            if ( custom.pfnCanClose != nullptr && !custom.pfnCanClose( custom.pWidget ) ) {
                ShowPage( custom.page ); // Keep heading, navigation and unsaved page together.
                return;
            }
        }
        QDialog::done( result );
    }

    const custom_page_t *FindCustom( const QString &page ) const
    {
        for ( const custom_page_t &custom : m_custom ) {
            if ( custom.page == page ) { return &custom; }
        }
        return nullptr;
    }

    struct row_t {
        const setting_descriptor_t *pDescriptor{ nullptr };
        QLabel *pLabel{ nullptr };
        QWidget *pEditor{ nullptr };
        QLabel *pSource{ nullptr };
        QToolButton *pReset{ nullptr };
        QLabel *pNotice{ nullptr };
    };

    QIcon Icon( const char *name ) const
    {
        return m_pStyle != nullptr ? EditorStyle_Icon( *m_pStyle, name ) : QIcon( QStringLiteral( ":/cypher/editor/icons/color/%1.svg" ).arg( QString::fromLatin1( name ) ) );
    }

    bool Owns( const setting_descriptor_t &descriptor ) const
    {
        const settings_scope_t target = Target();
        if ( target >= settings_scope_t::COUNT ) { return false; }
        const settings_document_t *store = m_pRegistry->scopes[static_cast<usize>( target )];
        setting_value_t own{};
        return store != nullptr && Setting_Read( SettingsDocument_Root( store ), descriptor, &own, nullptr ) != setting_read_status_t::ABSENT;
    }

    static i32 SearchScore( const setting_descriptor_t &descriptor, string_view_t query )
    {
        return std::max( { EditorFuzzy_Score( StringView_FromCString( descriptor.pLabel ), query ),
                          EditorFuzzy_Score( StringView_FromCString( descriptor.pPath ), query ),
                          EditorFuzzy_Score( StringView_FromCString( descriptor.pPage ), query ),
                          EditorFuzzy_Score( StringView_FromCString( descriptor.pDescription != nullptr ? descriptor.pDescription : "" ), query ) } );
    }

    void UpdateHeader()
    {
        const QString query = m_pSearch->text().trimmed();
        m_pTitle->setText( query.isEmpty() ? m_page.section( QLatin1Char( '/' ), -1 ) : QStringLiteral( "Search results" ) );
        m_pBreadcrumb->setText( query.isEmpty() ? QStringLiteral( "Settings › %1" ).arg( QString( m_page ).replace( QLatin1Char( '/' ), QStringLiteral( " › " ) ) ) : QStringLiteral( "Settings › Search" ) );
        const bool custom = query.isEmpty() && FindCustom( m_page ) != nullptr;
        m_pOverrides->setEnabled( !custom );
        if ( custom ) {
            m_pSummary->setText( QStringLiteral( "Manage %1 profiles and their saved settings." ).arg( m_page.toLower() ) );
        } else {
            const QString scope = QString::fromUtf8( EditorSettings_ScopeName( Target() ) );
            const QString suffix = m_pOverrides->isChecked() ? QStringLiteral( " · Overrides in %1" ).arg( scope ) : QStringLiteral( " · Save to %1" ).arg( scope );
            QString count = QStringLiteral( "%1 %2" ).arg( m_rows.size() ).arg( m_rows.size() == 1u ? QStringLiteral( "setting" ) : QStringLiteral( "settings" ) );
            if ( !m_pageResults.isEmpty() ) {
                count += QStringLiteral( " · %1 %2" ).arg( m_pageResults.size() ).arg( m_pageResults.size() == 1 ? QStringLiteral( "page" ) : QStringLiteral( "pages" ) );
            }
            m_pSummary->setText( count + suffix );
        }
    }

    // A reset or external import may remove a filtered row. Wait until the
    // editor's signal finishes before replacing its widget.
    void ScheduleFilteredRebuild()
    {
        if ( m_bRebuildPending || !m_pOverrides->isChecked() ) { return; }
        const QByteArray query = m_pSearch->text().trimmed().toUtf8();
        if ( query.isEmpty() && FindCustom( m_page ) != nullptr ) { return; }
        bool membershipChanged = false;
        for ( usize i = 0; i < EditorSettings_Count( m_pRegistry ); ++i ) {
            const auto &descriptor = *EditorSettings_At( m_pRegistry, i );
            const QString page = QString::fromUtf8( descriptor.pPage );
            const bool matches = query.isEmpty() ? page == m_page || page.startsWith( m_page + QLatin1Char( '/' ) )
                                                : SearchScore( descriptor, ViewOf( query ) ) != EDITOR_FUZZY_NO_MATCH;
            if ( ( matches && Owns( descriptor ) ) != ( FindRow( QString::fromUtf8( descriptor.pPath ) ) != nullptr ) ) {
                membershipChanged = true;
                break;
            }
        }
        if ( !membershipChanged ) { return; } // Ordinary edits keep their control and focus.
        m_bRebuildPending = true;
        QTimer::singleShot( 0, this, [this]() { m_bRebuildPending = false; Rebuild(); } );
    }

    static void OnChanged( void *pContext, string_view_t path ) noexcept
    {
        auto *pDialog = static_cast<settings_dialog_t *>( pContext );
        if ( path.cchLength == 0u ) {
            pDialog->FillTargets();
            pDialog->RefreshAll();
            pDialog->UpdateHeader();
            pDialog->ScheduleFilteredRebuild();
            return;
        }
        if ( row_t *pRow = pDialog->FindRow( FromView( path ) ) ) { pDialog->Refresh( *pRow ); }
        pDialog->ScheduleFilteredRebuild();
    }

    row_t *FindRow( const QString &path )
    {
        for ( row_t &row : m_rows ) {
            if ( QString::fromUtf8( row.pDescriptor->pPath ) == path ) { return &row; }
        }
        return nullptr;
    }

    const row_t *FindRow( const QString &path ) const { return const_cast<settings_dialog_t *>( this )->FindRow( path ); }

    settings_scope_t Target() const
    {
        return m_pTarget->count() != 0 ? static_cast<settings_scope_t>( m_pTarget->currentData().toInt() ) : settings_scope_t::DEFAULT;
    }

    void FillTargets()
    {
        const QSignalBlocker blocker( m_pTarget );
        const QVariant previous = m_pTarget->currentData();
        m_pTarget->clear();
        // Most commonly edited first: user settings follow the person.
        constexpr settings_scope_t kOrder[]{ settings_scope_t::USER, settings_scope_t::PROJECT, settings_scope_t::WORKSPACE };
        for ( settings_scope_t scope : kOrder ) {
            if ( m_pRegistry->scopes[static_cast<usize>( scope )] == nullptr ) { continue; }
            m_pTarget->addItem( QString::fromUtf8( EditorSettings_ScopeName( scope ) ), static_cast<int>( scope ) );
        }
        const int index = m_pTarget->findData( previous );
        if ( index >= 0 ) { m_pTarget->setCurrentIndex( index ); }
    }

    // Page tree from the registered pages, in registration order.
    void FillPages()
    {
        const QSignalBlocker blocker( m_pPages );
        m_pPages->clear();
        QHash<QString, QTreeWidgetItem *> items;
        for ( usize i = 0u; i < EditorSettings_Count( m_pRegistry ); ++i ) {
            const QString page = QString::fromUtf8( EditorSettings_At( m_pRegistry, i )->pPage );
            const QStringList parts = page.split( QLatin1Char( '/' ) );
            QString path;
            QTreeWidgetItem *pParent = nullptr;
            for ( const QString &part : parts ) {
                path = path.isEmpty() ? part : path + QLatin1Char( '/' ) + part;
                QTreeWidgetItem *pItem = items.value( path, nullptr );
                if ( pItem == nullptr ) {
                    pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( m_pPages );
                    pItem->setText( 0, part );
                    pItem->setData( 0, kPageRole, path );
                    pItem->setIcon( 0, Icon( PageIcon( path ) ) );
                    items.insert( path, pItem );
                }
                pParent = pItem;
            }
        }
        m_pPages->expandAll();
        if ( m_pPages->topLevelItemCount() != 0 ) {
            m_pPages->setCurrentItem( m_pPages->topLevelItem( 0 ) );
            m_page = m_pPages->topLevelItem( 0 )->data( 0, kPageRole ).toString();
        }
        Rebuild();
    }

    void Rebuild()
    {
        m_rows.clear();
        m_pageResults.clear();
        const QByteArray query = m_pSearch->text().trimmed().toUtf8();
        if ( query.isEmpty() ) {
            if ( const custom_page_t *pCustom = FindCustom( m_page ) ) {
                m_pStack->setCurrentWidget( pCustom->pWidget );
                UpdateHeader();
                return;
            }
        }
        m_pStack->setCurrentWidget( m_pScroll );
        auto *pForm = new QWidget();
        auto *pGrid = new QGridLayout( pForm );
        pGrid->setContentsMargins( 0, 0, 4, 0 );
        pGrid->setVerticalSpacing( 6 );
        pGrid->setColumnStretch( 1, 1 );
        int iRow = 0;
        const string_view_t queryView{ query.constData(), static_cast<usize>( query.size() ) };
        QString lastPage;
        struct match_t {
            const setting_descriptor_t *pDescriptor;
            i32 score;
        };
        std::vector<match_t> matches;
        for ( usize i = 0u; i < EditorSettings_Count( m_pRegistry ); ++i ) {
            const setting_descriptor_t *pDescriptor = EditorSettings_At( m_pRegistry, i );
            if ( m_pOverrides->isChecked() && !Owns( *pDescriptor ) ) { continue; }
            if ( query.isEmpty() ) {
                const QString page = QString::fromUtf8( pDescriptor->pPage );
                if ( page == m_page || page.startsWith( m_page + QLatin1Char( '/' ) ) ) { matches.push_back( { pDescriptor, 0 } ); }
                continue;
            }
            const i32 score = SearchScore( *pDescriptor, queryView );
            if ( score != EDITOR_FUZZY_NO_MATCH ) { matches.push_back( { pDescriptor, score } ); }
        }
        // Custom pages cannot show their insides as rows; they appear as
        // links when their title or keywords match.
        for ( const custom_page_t &custom : m_custom ) {
            if ( query.isEmpty() ) { break; }
            bool bMatch = EditorFuzzy_Score( ViewOf( custom.page.toUtf8() ), queryView ) != EDITOR_FUZZY_NO_MATCH;
            for ( const QString &keyword : custom.keywords ) {
                bMatch = bMatch || EditorFuzzy_Score( ViewOf( keyword.toUtf8() ), queryView ) != EDITOR_FUZZY_NO_MATCH;
            }
            if ( !bMatch ) { continue; }
            if ( m_pageResults.isEmpty() ) {
                auto *pHeader = new QLabel( QStringLiteral( "Pages" ), pForm );
                pHeader->setProperty( "muted", true );
                pGrid->addWidget( pHeader, iRow++, 0, 1, 4 );
            }
            m_pageResults.append( custom.page );
            auto *pLink = new QPushButton( QStringLiteral( "Open %1 \u203a" ).arg( custom.page ), pForm );
            pLink->setFlat( true );
            pLink->setCursor( Qt::PointingHandCursor );
            const QString page = custom.page;
            QObject::connect( pLink, &QPushButton::clicked, this, [this, page]() { ShowPage( page ); } );
            pGrid->addWidget( pLink, iRow++, 0, 1, 2, Qt::AlignLeft );
        }
        // Search results keep page grouping, best pages first.
        if ( !query.isEmpty() ) {
            QHash<QString, i32> bestPageScores;
            for ( const match_t &match : matches ) {
                const QString page = QString::fromUtf8( match.pDescriptor->pPage );
                bestPageScores[page] = std::max( bestPageScores.value( page, EDITOR_FUZZY_NO_MATCH ), match.score );
            }
            std::stable_sort( matches.begin(), matches.end(), [&bestPageScores]( const match_t &a, const match_t &b ) {
                const QString pageA = QString::fromUtf8( a.pDescriptor->pPage ), pageB = QString::fromUtf8( b.pDescriptor->pPage );
                if ( pageA == pageB ) { return a.score > b.score; }
                if ( bestPageScores[pageA] != bestPageScores[pageB] ) { return bestPageScores[pageA] > bestPageScores[pageB]; }
                return pageA < pageB;
            } );
        }
        for ( const match_t &match : matches ) {
            const QString page = QString::fromUtf8( match.pDescriptor->pPage );
            if ( ( !query.isEmpty() || page != m_page ) && page != lastPage ) {
                auto *pHeader = new QLabel( page, pForm );
                pHeader->setObjectName( QStringLiteral( "SettingsSectionTitle" ) );
                pHeader->setTextFormat( Qt::PlainText );
                pGrid->addWidget( pHeader, iRow++, 0, 1, 4 );
                lastPage = page;
            }
            AddRow( pGrid, iRow++, *match.pDescriptor );
        }
        if ( matches.empty() && m_pageResults.isEmpty() ) {
            auto *pEmpty = new QLabel( m_pOverrides->isChecked() ? QStringLiteral( "No overrides here. Turn off Overrides only to see inherited settings." ) : QStringLiteral( "No settings match. Try a setting name, category, or keyword." ), pForm );
            pEmpty->setWordWrap( true );
            pEmpty->setProperty( "muted", true );
            pGrid->addWidget( pEmpty, iRow++, 0, 1, 4 );
        }
        pGrid->setRowStretch( iRow, 1 );
        m_pScroll->setWidget( pForm ); // Deletes the previous form.
        RefreshAll();
        UpdateHeader();
    }

    void AddRow( QGridLayout *pGrid, int iRow, const setting_descriptor_t &descriptor )
    {
        auto *pForm = new QWidget( pGrid->parentWidget() );
        pForm->setObjectName( QStringLiteral( "SettingsRow" ) );
        auto *pRowGrid = new QGridLayout( pForm );
        pRowGrid->setContentsMargins( 12, 10, 10, 10 );
        pRowGrid->setHorizontalSpacing( 10 );
        pRowGrid->setVerticalSpacing( 3 );
        pRowGrid->setColumnStretch( 0, 1 );
        row_t row{};
        row.pDescriptor = &descriptor;
        row.pLabel = new QLabel( QString::fromUtf8( descriptor.pLabel ), pForm );
        row.pLabel->setTextFormat( Qt::PlainText );
        row.pLabel->setWordWrap( true );
        const QString tip = QStringLiteral( "%1\n%2" ).arg( QString::fromUtf8( descriptor.pDescription != nullptr ? descriptor.pDescription : "" ),
                                                             QString::fromUtf8( descriptor.pPath ) );
        row.pLabel->setToolTip( tip );
        row.pEditor = CreateEditor( descriptor, pForm );
        row.pEditor->setToolTip( tip );
        row.pEditor->setObjectName( QString::fromUtf8( descriptor.pPath ) );
        row.pEditor->setAccessibleName( QString::fromUtf8( descriptor.pLabel ) );
        if ( descriptor.type != setting_type_t::BOOL ) { row.pEditor->setMinimumWidth( 150 ); row.pEditor->setMaximumWidth( 210 ); }
        row.pSource = new QLabel( pForm );
        row.pSource->setObjectName( QStringLiteral( "SettingsScopeBadge" ) );
        row.pSource->setAlignment( Qt::AlignCenter );
        row.pSource->setMinimumWidth( 52 );
        row.pReset = new QToolButton( pForm );
        row.pReset->setText( QStringLiteral( "Reset" ) );
        row.pReset->setIcon( Icon( "edit-undo" ) );
        row.pReset->setToolButtonStyle( Qt::ToolButtonIconOnly );
        row.pReset->setAccessibleName( QStringLiteral( "Reset %1" ).arg( QString::fromUtf8( descriptor.pLabel ) ) );
        row.pReset->setToolTip( QStringLiteral( "Remove this scope's value so it inherits again" ) );
        const setting_descriptor_t *pDescriptor = &descriptor;
        QObject::connect( row.pReset, &QToolButton::clicked, this, [this, pDescriptor]() {
            if ( EditorSettings_Reset( m_pRegistry, Target(), *pDescriptor ) != settings_registry_status_t::OK ) {
                CY_LOG_WRITE( Warning, Editor, "Setting could not be reset in that scope" );
            }
        } );
        pRowGrid->addWidget( row.pLabel, 0, 0 );
        pRowGrid->addWidget( row.pEditor, 0, 1, Qt::AlignRight | Qt::AlignVCenter );
        pRowGrid->addWidget( row.pSource, 0, 2 );
        pRowGrid->addWidget( row.pReset, 0, 3 );
        if ( descriptor.pDescription != nullptr && descriptor.pDescription[0] != '\0' ) {
            auto *pDescription = new QLabel( QString::fromUtf8( descriptor.pDescription ), pForm );
            pDescription->setObjectName( QStringLiteral( "SettingsDescription" ) );
            pDescription->setTextFormat( Qt::PlainText );
            pDescription->setProperty( "muted", true );
            pDescription->setWordWrap( true );
            pDescription->setToolTip( QString::fromUtf8( descriptor.pPath ) );
            pRowGrid->addWidget( pDescription, 1, 0, 1, 4 );
        }
        row.pNotice = new QLabel( pForm );
        row.pNotice->setObjectName( QStringLiteral( "SettingsScopeNotice" ) );
        row.pNotice->setProperty( "muted", true );
        row.pNotice->setWordWrap( true );
        pRowGrid->addWidget( row.pNotice, 2, 0, 1, 4 );
        pGrid->addWidget( pForm, iRow, 0, 1, 4 );
        m_rows.push_back( row );
    }

    void Commit( const setting_descriptor_t &descriptor, const setting_value_t &value )
    {
        if ( m_bRefreshing ) { return; }
        const settings_registry_status_t status = EditorSettings_Write( m_pRegistry, Target(), descriptor, value );
        if ( status != settings_registry_status_t::OK ) {
            CY_LOG_WRITE( Warning, Editor, "Setting was not written; no scope to save it to" );
            if ( row_t *pRow = FindRow( QString::fromUtf8( descriptor.pPath ) ) ) { Refresh( *pRow ); }
        }
    }

    QWidget *CreateEditor( const setting_descriptor_t &descriptor, QWidget *pParent )
    {
        const setting_descriptor_t *pDescriptor = &descriptor;
        switch ( descriptor.type ) {
            case setting_type_t::BOOL: {
                auto *pBox = new QCheckBox( pParent );
                QObject::connect( pBox, &QCheckBox::toggled, this, [this, pDescriptor]( bool bChecked ) {
                    setting_value_t value{};
                    value.type = setting_type_t::BOOL;
                    value.bValue = bChecked;
                    Commit( *pDescriptor, value );
                } );
                return pBox;
            }
            case setting_type_t::INTEGER: {
                auto *pSpin = new QSpinBox( pParent );
                pSpin->setRange( static_cast<int>( std::max<i64>( descriptor.nMin, INT_MIN ) ), static_cast<int>( std::min<i64>( descriptor.nMax, INT_MAX ) ) );
                pSpin->setKeyboardTracking( false ); // Commit on Enter or focus loss, not per keystroke.
                QObject::connect( pSpin, &QSpinBox::valueChanged, this, [this, pDescriptor]( int nValue ) {
                    setting_value_t value{};
                    value.type = setting_type_t::INTEGER;
                    value.nValue = nValue;
                    Commit( *pDescriptor, value );
                } );
                return pSpin;
            }
            case setting_type_t::REAL: {
                auto *pSpin = new real_spin_t( pParent );
                pSpin->setRange( std::max( descriptor.flMin, -1.0e12 ), std::min( descriptor.flMax, 1.0e12 ) );
                pSpin->setKeyboardTracking( false );
                QObject::connect( pSpin, &QDoubleSpinBox::valueChanged, this, [this, pDescriptor]( double flValue ) {
                    setting_value_t value{};
                    value.type = setting_type_t::REAL;
                    value.flValue = flValue;
                    Commit( *pDescriptor, value );
                } );
                return pSpin;
            }
            case setting_type_t::STRING: {
                auto *pEdit = new QLineEdit( pParent );
                pEdit->setMaxLength( static_cast<int>( std::min<usize>( descriptor.cbMaxText, 4096u ) ) );
                QObject::connect( pEdit, &QLineEdit::editingFinished, this, [this, pDescriptor, pEdit]() {
                    const QByteArray utf8 = pEdit->text().toUtf8();
                    setting_value_t value{};
                    value.type = setting_type_t::STRING;
                    value.text = string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) };
                    Commit( *pDescriptor, value );
                } );
                return pEdit;
            }
            case setting_type_t::ENUM: {
                auto *pCombo = new QComboBox( pParent );
                for ( usize i = 0u; i < descriptor.nEnumValues; ++i ) { pCombo->addItem( QString::fromUtf8( descriptor.ppEnumValues[i] ) ); }
                QObject::connect( pCombo, &QComboBox::currentTextChanged, this, [this, pDescriptor]( const QString &text ) {
                    const QByteArray utf8 = text.toUtf8();
                    setting_value_t value{};
                    value.type = setting_type_t::ENUM;
                    value.text = string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) };
                    Commit( *pDescriptor, value );
                } );
                return pCombo;
            }
            case setting_type_t::COLOR: {
                auto *pButton = new QPushButton( pParent );
                QObject::connect( pButton, &QPushButton::clicked, this, [this, pDescriptor, pButton]() {
                    const QColor chosen = QColorDialog::getColor( pButton->property( "color" ).value<QColor>(), this, QString(),
                                                                  QColorDialog::ShowAlphaChannel );
                    if ( !chosen.isValid() ) { return; }
                    setting_value_t value{};
                    value.type = setting_type_t::COLOR;
                    value.rgba = ( static_cast<u32>( chosen.red() ) << 24u ) | ( static_cast<u32>( chosen.green() ) << 16u ) |
                                 ( static_cast<u32>( chosen.blue() ) << 8u ) | static_cast<u32>( chosen.alpha() );
                    Commit( *pDescriptor, value );
                } );
                return pButton;
            }
        }
        return new QWidget( pParent );
    }

    void RefreshAll()
    {
        for ( row_t &row : m_rows ) { Refresh( row ); }
    }

    // Shows the effective value, where it comes from, and whether the
    // target scope has its own value to reset.
    void Refresh( row_t &row )
    {
        const setting_descriptor_t &descriptor = *row.pDescriptor;
        const settings_value_source_t resolved = EditorSettings_Resolve( m_pRegistry, descriptor );
        m_bRefreshing = true;
        switch ( descriptor.type ) {
            case setting_type_t::BOOL: {
                auto *pBox = static_cast<QCheckBox *>( row.pEditor );
                const QSignalBlocker blocker( pBox );
                pBox->setChecked( resolved.value.bValue );
                pBox->setText( resolved.value.bValue ? QStringLiteral( "Enabled" ) : QStringLiteral( "Disabled" ) );
                break;
            }
            case setting_type_t::INTEGER: {
                auto *pSpin = static_cast<QSpinBox *>( row.pEditor );
                const QSignalBlocker blocker( pSpin );
                pSpin->setValue( static_cast<int>( resolved.value.nValue ) );
                break;
            }
            case setting_type_t::REAL: {
                auto *pSpin = static_cast<QDoubleSpinBox *>( row.pEditor );
                const QSignalBlocker blocker( pSpin );
                pSpin->setValue( resolved.value.flValue );
                break;
            }
            case setting_type_t::STRING: {
                auto *pEdit = static_cast<QLineEdit *>( row.pEditor );
                const QSignalBlocker blocker( pEdit );
                pEdit->setText( FromView( resolved.value.text ) );
                break;
            }
            case setting_type_t::ENUM: {
                auto *pCombo = static_cast<QComboBox *>( row.pEditor );
                const QSignalBlocker blocker( pCombo );
                pCombo->setCurrentText( FromView( resolved.value.text ) );
                break;
            }
            case setting_type_t::COLOR: {
                auto *pButton = static_cast<QPushButton *>( row.pEditor );
                const QColor color = FromRgba( resolved.value.rgba );
                pButton->setProperty( "color", color );
                pButton->setText( color.name( color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb ) );
                pButton->setStyleSheet( QStringLiteral( "QPushButton { border-left: 16px solid %1; }" ).arg( color.name() ) );
                break;
            }
        }
        m_bRefreshing = false;
        row.pSource->setText( QString::fromUtf8( EditorSettings_ScopeName( resolved.source ) ) );
        // Bold labels mark values the target scope sets itself.
        const settings_scope_t target = Target();
        const bool bOwn = Owns( descriptor );
        const bool writable = target < settings_scope_t::COUNT && m_pRegistry->scopes[static_cast<usize>( target )] != nullptr;
        row.pEditor->setEnabled( writable );
        const QString source = QString::fromUtf8( EditorSettings_ScopeName( resolved.source ) );
        const QString scope = QString::fromUtf8( EditorSettings_ScopeName( target ) );
        row.pSource->setToolTip( QStringLiteral( "Effective value from %1. %2" ).arg( source, bOwn ? QStringLiteral( "%1 has its own override; Reset removes it." ).arg( scope ) : QStringLiteral( "%1 inherits this value." ).arg( scope ) ) );
        row.pReset->setToolTip( QStringLiteral( "Remove the %1 override and inherit again" ).arg( scope ) );
        QString notice;
        if ( resolved.nInvalid != 0u ) { notice = QStringLiteral( "An invalid override was skipped. Reset its scope to inherit a valid value." ); }
        else if ( writable && resolved.source < target ) { notice = QStringLiteral( "%1 overrides changes saved to %2." ).arg( source, scope ); }
        row.pNotice->setText( notice );
        row.pNotice->setVisible( !notice.isEmpty() );
        QFont font = row.pLabel->font();
        font.setBold( bOwn );
        row.pLabel->setFont( font );
        row.pReset->setEnabled( bOwn );
    }

    settings_registry_t *m_pRegistry{ nullptr };
    const editor_style_t *m_pStyle{ nullptr };
    settings_transfer_hooks_t m_transferHooks{};
    QLineEdit *m_pSearch{ nullptr };
    QComboBox *m_pTarget{ nullptr };
    QCheckBox *m_pOverrides{ nullptr };
    QLabel *m_pBreadcrumb{ nullptr };
    QLabel *m_pTitle{ nullptr };
    QLabel *m_pSummary{ nullptr };
    QTreeWidget *m_pPages{ nullptr };
    QStackedWidget *m_pStack{ nullptr };
    QScrollArea *m_pScroll{ nullptr };
    QString m_page{};
    std::vector<custom_page_t> m_custom{};
    QStringList m_pageResults{};
    std::vector<row_t> m_rows{};
    bool m_bRefreshing{ false };
    bool m_bRebuildPending{ false };
};

settings_dialog_t *AsDialog( QDialog *pDialog )
{
    auto *pImpl = dynamic_cast<settings_dialog_t *>( pDialog );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QDialog *EditorSettingsDialog_Create( QWidget *pParent, settings_registry_t *pRegistry, const editor_style_t *pStyle )
{
    return new settings_dialog_t( pParent, pRegistry, pStyle );
}

void EditorSettingsDialog_SetTransferHooks( QDialog *pDialog, const settings_transfer_hooks_t &hooks )
{
    if ( settings_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->SetTransferHooks( hooks ); }
}

void EditorSettingsDialog_ShowPage( QDialog *pDialog, const QString &page )
{
    if ( settings_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->ShowPage( page ); }
}

void EditorSettingsDialog_SetSearch( QDialog *pDialog, const QString &text )
{
    if ( settings_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->SetSearch( text ); }
}

void EditorSettingsDialog_AddPage( QDialog *pDialog, const QString &page, QWidget *pWidget, const QStringList &keywords,
                                   editor_settings_page_close_fn pfnCanClose )
{
    if ( settings_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->AddPage( page, pWidget, keywords, pfnCanClose ); }
}

QString EditorSettingsDialog_CurrentPage( QDialog *pDialog )
{
    settings_dialog_t *pImpl = AsDialog( pDialog );
    return pImpl != nullptr ? pImpl->CurrentPage() : QString();
}

QStringList EditorSettingsDialog_PageResults( QDialog *pDialog )
{
    settings_dialog_t *pImpl = AsDialog( pDialog );
    return pImpl != nullptr ? pImpl->PageResults() : QStringList{};
}

void EditorSettingsDialog_SetTargetScope( QDialog *pDialog, settings_scope_t scope )
{
    if ( settings_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->SetTargetScope( scope ); }
}

QStringList EditorSettingsDialog_VisibleSettings( QDialog *pDialog )
{
    settings_dialog_t *pImpl = AsDialog( pDialog );
    return pImpl != nullptr ? pImpl->VisibleSettings() : QStringList{};
}

QWidget *EditorSettingsDialog_EditorFor( QDialog *pDialog, const QString &path )
{
    settings_dialog_t *pImpl = AsDialog( pDialog );
    return pImpl != nullptr ? pImpl->EditorFor( path ) : nullptr;
}

QString EditorSettingsDialog_SourceFor( QDialog *pDialog, const QString &path )
{
    settings_dialog_t *pImpl = AsDialog( pDialog );
    return pImpl != nullptr ? pImpl->SourceFor( path ) : QString();
}

void EditorSettingsDialog_Reset( QDialog *pDialog, const QString &path )
{
    if ( settings_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->Reset( path ); }
}

} // namespace cypher::editor::gui
