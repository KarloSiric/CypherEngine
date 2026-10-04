//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AppearancePage.cpp
//  Purpose: Implements the Appearance page of Settings.
//  Details: Every control reads from and writes to one theme draft, and the
//           page refreshes from the draft after each edit, so the theme
//           list, the interface sizes, and the colour rows never disagree.
//           A theme chosen elsewhere (the Theme Editor saving, the settings
//           file) rebases the draft through the style listener; the page's
//           own previews are recognised and ignored there.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_AppearancePage.h"

#include "CypherEditorGui_SettingControl.h"
#include "CypherEditorGui_ThemeDraft.h"
#include "CypherEditor_FuzzyMatch.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr int kIdRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kPreviewDelayMs = 30; // Coalesces colour-dialog drags; a full restyle per mouse move stutters.
constexpr int kColorColumns = 2;
constexpr QSize kRowSwatch( 28, 14 );

enum theme_kind_t : int { THEME_BUILTIN = 1, THEME_PRESET, THEME_USER, THEME_MODIFIED };

// Friendlier titles for the framework's token groups, in the spirit of the
// TileEditor's "Viewports and grid", "Geometry and selection".
struct group_title_t {
    const char *pGroup;
    const char *pTitle;
};

constexpr group_title_t kGroupTitles[]{
    { "Interface/Base", "Interface" },
    { "Interface/Chrome", "Interface chrome" },
    { "Interface/Status", "Status colours" },
    { "Viewport/Frame", "Viewport frame and overlays" },
    { "Viewport/Grid", "Viewports and grid" },
    { "Viewport/Objects", "Geometry and objects" },
    { "Viewport/Tools", "Tools and gizmos" },
    { "Console", "Console" },
    { "Code", "Code editor" },
};

string_view_t ViewOf( const QByteArray &bytes ) noexcept
{
    return string_view_t{ bytes.constData(), static_cast<usize>( bytes.size() ) };
}

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

QString GroupTitle( const char *pGroup )
{
    const QString group = QString::fromUtf8( pGroup != nullptr ? pGroup : "Other" );
    for ( const group_title_t &title : kGroupTitles ) {
        if ( group == QLatin1String( title.pGroup ) ) { return QString::fromLatin1( title.pTitle ); }
    }
    return QString( group ).replace( QLatin1Char( '/' ), QStringLiteral( " / " ) );
}

// "My Dark Theme!" -> "my_dark_theme": the ID rules of CYTHEME.md 2.
QString IdFromName( const QString &name )
{
    QString id;
    bool bGap = false;
    for ( const QChar c : name.toLower() ) {
        if ( ( c >= QLatin1Char( 'a' ) && c <= QLatin1Char( 'z' ) ) || ( c >= QLatin1Char( '0' ) && c <= QLatin1Char( '9' ) ) ) {
            if ( bGap && !id.isEmpty() ) { id += QLatin1Char( '_' ); }
            id += c;
            bGap = false;
        } else {
            bGap = true;
        }
    }
    id.truncate( 64 );
    return id.isEmpty() ? QStringLiteral( "custom" ) : id;
}

QIcon Swatch( u32 rgba, QSize size )
{
    QPixmap swatch( size );
    swatch.fill( Qt::transparent );
    QPainter painter( &swatch );
    // A checkerboard shows through translucent colours.
    const int cell = 4;
    for ( int y = 0; y < size.height(); y += cell ) {
        for ( int x = 0; x < size.width(); x += cell ) {
            painter.fillRect( x, y, cell, cell, ( ( x + y ) / cell ) % 2 == 0 ? QColor( 0xCC, 0xCC, 0xCC ) : QColor( 0x88, 0x88, 0x88 ) );
        }
    }
    painter.fillRect( swatch.rect(), EditorThemeDraft_ToColor( rgba ) );
    painter.setPen( QColor( 0, 0, 0, 140 ) );
    painter.drawRect( swatch.rect().adjusted( 0, 0, -1, -1 ) );
    return QIcon( swatch );
}

struct color_row_t {
    const theme_token_t *pToken{ nullptr };
    QGroupBox *pGroup{ nullptr };
    QWidget *pRow{ nullptr };
    QLabel *pLabel{ nullptr };
    QToolButton *pColor{ nullptr };
    QToolButton *pAuto{ nullptr }; // Derived colours only.
    QToolButton *pRevert{ nullptr };
};

struct color_group_t {
    QGroupBox *pBox{ nullptr };
    QGridLayout *pGrid{ nullptr };
    std::vector<usize> rows{}; // Indices into the page's rows.
};

class appearance_page_t final : public QWidget {
public:
    appearance_page_t( QWidget *pParent, editor_gui_t *pGui, QApplication *pApplication, const QString &folder )
        : QWidget( pParent ), m_pGui( pGui ), m_pApplication( pApplication ), m_folder( folder )
    {
        CY_ASSERT( pGui != nullptr && pGui->bInitialized && pApplication != nullptr );
        setObjectName( QStringLiteral( "EditorAppearancePage" ) );
        const bool bBegun = EditorThemeDraft_Begin( &m_draft, pGui, pApplication );
        CY_ASSERT( bBegun );
        ( void )bBegun;

        auto *pRoot = new QVBoxLayout( this );
        pRoot->setContentsMargins( 0, 0, 0, 0 );
        pRoot->addWidget( BuildThemeGroup() );

        auto *pScroll = new QScrollArea( this );
        pScroll->setObjectName( QStringLiteral( "AppearanceScroll" ) );
        pScroll->setWidgetResizable( true );
        pScroll->setFrameShape( QFrame::NoFrame );
        auto *pContent = new QWidget( pScroll );
        auto *pContentLayout = new QVBoxLayout( pContent );
        pContentLayout->setContentsMargins( 0, 0, 4, 0 );
        pContentLayout->addWidget( BuildInterfaceGroup( pContent ) );
        BuildColorSection( pContent, pContentLayout );
        pContentLayout->addStretch( 1 );
        pScroll->setWidget( pContent );
        pRoot->addWidget( pScroll, 1 );

        auto *pFolder = new QLabel( QStringLiteral( "User themes are .cytheme files in %1; Save As writes there and Import copies there." )
                                        .arg( QDir::toNativeSeparators( m_folder ) ),
                                    this );
        pFolder->setObjectName( QStringLiteral( "AppearanceThemeFolder" ) );
        pFolder->setWordWrap( true );
        pFolder->setTextInteractionFlags( Qt::TextSelectableByMouse );
        pFolder->setProperty( "muted", true );
        pRoot->addWidget( pFolder );

        m_pPreviewTimer = new QTimer( this );
        m_pPreviewTimer->setSingleShot( true );
        m_pPreviewTimer->setInterval( kPreviewDelayMs );
        QObject::connect( m_pPreviewTimer, &QTimer::timeout, this, [this]() { EditorThemeDraft_Preview( &m_draft ); } );
        ( void )EditorGui_AddStyleListener( pGui, &appearance_page_t::OnStyleChanged, this );
        Changed();
        SetStatus( QStringLiteral( "Choosing a theme applies it at once. Colour edits preview live; save them as a theme to keep them." ) );
    }

    ~appearance_page_t() override
    {
        EditorGui_RemoveStyleListener( m_pGui, &appearance_page_t::OnStyleChanged, this );
        m_pPreviewTimer->stop();
        EditorThemeDraft_End( &m_draft ); // Never leave the style pointing into the draft.
    }

    // ---- Automation ------------------------------------------------------

    QStringList Themes() const
    {
        QStringList ids;
        for ( int i = 0; i < m_pThemes->count(); ++i ) {
            const QString id = m_pThemes->itemData( i, kIdRole ).toString();
            if ( !id.isEmpty() ) { ids.append( id ); }
        }
        return ids;
    }

    QString CurrentTheme() const { return EditorThemeDraft_HasChanges( &m_draft ) ? QString() : m_draft.previousId; }

    QStringList ColorTokens() const
    {
        QStringList ids;
        for ( const color_row_t &row : m_rows ) {
            if ( !row.pRow->isHidden() ) { ids.append( QString::fromUtf8( row.pToken->pId ) ); }
        }
        return ids;
    }

    void SetFilter( const QString &text ) { m_pFilter->setText( text ); }

    void SetColor( const QString &id, const QColor &color )
    {
        if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, id ) ) {
            EditorThemeDraft_SetColor( &m_draft, *pToken, color );
            Changed();
        }
    }

    void SetAuto( const QString &id )
    {
        if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, id ) ) {
            EditorThemeDraft_SetFormula( &m_draft, *pToken );
            Changed();
        }
    }

    void RevertColor( const QString &id )
    {
        if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, id ) ) {
            EditorThemeDraft_RevertToken( &m_draft, *pToken );
            Changed();
        }
    }

    void Revert()
    {
        m_pPreviewTimer->stop();
        m_bApplying = true; // Putting the theme back restyles; the draft is reset right after.
        EditorThemeDraft_RevertAll( &m_draft );
        m_bApplying = false;
        Changed();
        SetStatus( QStringLiteral( "Changes reverted to %1." ).arg( ThemeName( m_draft.previousId ) ) );
    }

    bool HasChanges() const { return EditorThemeDraft_HasChanges( &m_draft ); }

    void SelectTheme( const QString &id ) { ChooseTheme( id ); }

    QString SaveAs( const QString &id, const QString &name ) { return SaveDraft( id, name ); }

    bool DeleteTheme( const QString &id )
    {
        if ( !IsUserTheme( id ) ) {
            SetStatus( QStringLiteral( "Only user themes can be deleted; presets and the default theme ship with the editor." ), true );
            return false;
        }
        const QString path = ThemePath( id );
        // Interactive deletes go to the trash so they can be restored;
        // automation (hidden page) removes the file outright.
        const bool bRemoved = isVisible() ? ( QFile::moveToTrash( path ) || QFile::remove( path ) ) : QFile::remove( path );
        if ( !bRemoved ) {
            SetStatus( QStringLiteral( "The theme file could not be removed: %1" ).arg( QDir::toNativeSeparators( path ) ), true );
            return false;
        }
        const QString name = ThemeName( id );
        const QByteArray utf8 = id.toUtf8();
        m_bApplying = true;
        EditorThemeDraft_RevertAll( &m_draft );
        const editor_gui_status_t status = EditorGui_RemoveTheme( m_pGui, m_pApplication, ViewOf( utf8 ) );
        if ( status != editor_gui_status_t::OK ) { CY_LOG_WRITE( Warning, Gui, "Deleted theme could not be unloaded" ); }
        if ( FromView( EditorGui_ActiveThemeId( m_pGui ) ) == m_draft.builtinId ) { RecordTheme( m_draft.builtinId ); }
        EditorThemeDraft_Rebase( &m_draft );
        m_bApplying = false;
        Changed();
        SetStatus( QStringLiteral( "Deleted %1." ).arg( name ) );
        return true;
    }

    QString Status() const { return m_pStatus->text(); }

    // The settings dialog asks before it closes.
    bool CanClose()
    {
        m_pPreviewTimer->stop();
        if ( !HasChanges() ) { return true; }
        if ( !isVisible() ) {
            Revert();
            return true;
        }
        const QMessageBox::StandardButton choice =
            QMessageBox::question( this, QStringLiteral( "Appearance" ), QStringLiteral( "Save the colour changes as a theme?" ),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
        if ( choice == QMessageBox::Cancel ) { return false; }
        if ( choice == QMessageBox::Discard ) {
            Revert();
            return true;
        }
        return IsUserTheme( m_draft.previousId ) ? !SaveDraft( m_draft.previousId, ThemeName( m_draft.previousId ) ).isEmpty() : AskSaveAs();
    }

private:
    // ---- Building --------------------------------------------------------

    QGroupBox *BuildThemeGroup()
    {
        auto *pGroup = new QGroupBox( QStringLiteral( "Colour themes" ), this );
        pGroup->setObjectName( QStringLiteral( "AppearanceThemeStudio" ) );
        auto *pLayout = new QVBoxLayout( pGroup );
        auto *pChoose = new QHBoxLayout();
        m_pThemes = new QComboBox( pGroup );
        m_pThemes->setObjectName( QStringLiteral( "AppearanceThemes" ) );
        m_pThemes->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
        m_pThemes->setMinimumContentsLength( 24 );
        m_pThemes->setIconSize( QSize( 36, 16 ) );
        m_pThemes->setToolTip( QStringLiteral( "Choosing a theme applies it at once and makes it the theme the editor starts with." ) );
        pChoose->addWidget( new QLabel( QStringLiteral( "Theme" ), pGroup ) );
        pChoose->addWidget( m_pThemes, 1 );
        pLayout->addLayout( pChoose );

        auto *pActions = new QHBoxLayout();
        m_pSave = new QPushButton( QStringLiteral( "Save" ), pGroup );
        m_pSave->setObjectName( QStringLiteral( "AppearanceSave" ) );
        m_pSave->setToolTip( QStringLiteral( "Save the changes over this user theme." ) );
        auto *pSaveAs = new QPushButton( QStringLiteral( "Save As..." ), pGroup );
        pSaveAs->setObjectName( QStringLiteral( "AppearanceSaveAs" ) );
        pSaveAs->setToolTip( QStringLiteral( "Save the current colours, sizes, and fonts as a new user theme." ) );
        auto *pImport = new QPushButton( QStringLiteral( "Import..." ), pGroup );
        pImport->setObjectName( QStringLiteral( "AppearanceImport" ) );
        pImport->setToolTip( QStringLiteral( "Copy a .cytheme file into your themes and use it." ) );
        auto *pExport = new QPushButton( QStringLiteral( "Export..." ), pGroup );
        pExport->setObjectName( QStringLiteral( "AppearanceExport" ) );
        pExport->setToolTip( QStringLiteral( "Write the current look, changes included, to a .cytheme file to share." ) );
        m_pDelete = new QPushButton( QStringLiteral( "Delete" ), pGroup );
        m_pDelete->setObjectName( QStringLiteral( "AppearanceDelete" ) );
        m_pDelete->setToolTip( QStringLiteral( "Move this user theme's file to the trash." ) );
        m_pThemeEditor = new QPushButton( QStringLiteral( "Theme Editor..." ), pGroup );
        m_pThemeEditor->setObjectName( QStringLiteral( "AppearanceThemeEditor" ) );
        m_pThemeEditor->setVisible( EditorCommands_Find( &m_pGui->commands, StringView_FromCString( "tools.theme_editor" ) ) != nullptr );
        pActions->addWidget( m_pSave );
        pActions->addWidget( pSaveAs );
        pActions->addWidget( pImport );
        pActions->addWidget( pExport );
        pActions->addStretch( 1 );
        pActions->addWidget( m_pThemeEditor );
        pActions->addWidget( m_pDelete );
        pLayout->addLayout( pActions );

        m_pStatus = new QLabel( pGroup );
        m_pStatus->setObjectName( QStringLiteral( "AppearanceStatus" ) );
        m_pStatus->setWordWrap( true );
        m_pStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_pStatus->setProperty( "muted", true );
        pLayout->addWidget( m_pStatus );

        QObject::connect( m_pThemes, &QComboBox::activated, this, [this]( int index ) {
            ChooseTheme( m_pThemes->itemData( index, kIdRole ).toString() );
        } );
        QObject::connect( m_pSave, &QPushButton::clicked, this, [this]() { ( void )SaveDraft( m_draft.previousId, ThemeName( m_draft.previousId ) ); } );
        QObject::connect( pSaveAs, &QPushButton::clicked, this, [this]() { ( void )AskSaveAs(); } );
        QObject::connect( pImport, &QPushButton::clicked, this, [this]() { Import(); } );
        QObject::connect( pExport, &QPushButton::clicked, this, [this]() { Export(); } );
        QObject::connect( m_pDelete, &QPushButton::clicked, this, [this]() {
            const QString id = m_draft.previousId;
            if ( QMessageBox::question( this, QStringLiteral( "Delete Theme" ),
                                        QStringLiteral( "Move the theme \"%1\" to the trash?" ).arg( ThemeName( id ) ) ) == QMessageBox::Yes ) {
                ( void )DeleteTheme( id );
            }
        } );
        QObject::connect( m_pThemeEditor, &QPushButton::clicked, this, [this]() {
            if ( EditorCommands_Execute( &m_pGui->commands, StringView_FromCString( "tools.theme_editor" ), command_args_t{} ) != command_result_t::OK ) {
                SetStatus( QStringLiteral( "The Theme Editor could not be opened." ), true );
            }
        } );
        return pGroup;
    }

    QSpinBox *AddSpin( QFormLayout *pForm, const char *pName, const QString &label, int nMin, int nMax, int nStep, const QString &suffix,
                       const QString &tip )
    {
        auto *pSpin = new QSpinBox( pForm->parentWidget() );
        pSpin->setObjectName( QString::fromLatin1( pName ) );
        pSpin->setRange( nMin, nMax );
        pSpin->setSingleStep( nStep );
        pSpin->setSuffix( suffix );
        pSpin->setKeyboardTracking( false ); // A restyle per keystroke would fight the typing.
        pSpin->setToolTip( tip );
        pSpin->setMinimumWidth( 90 );
        pForm->addRow( label, pSpin );
        return pSpin;
    }

    QGroupBox *BuildInterfaceGroup( QWidget *pParent )
    {
        auto *pGroup = new QGroupBox( QStringLiteral( "Interface scale and focus" ), pParent );
        pGroup->setObjectName( QStringLiteral( "AppearanceInterface" ) );
        auto *pColumns = new QHBoxLayout( pGroup );
        auto *pLeft = new QFormLayout();
        auto *pRight = new QFormLayout();
        pColumns->addLayout( pLeft, 1 );
        pColumns->addSpacing( 24 );
        pColumns->addLayout( pRight, 1 );

        m_pTextSize = AddSpin( pLeft, "AppearanceTextSize", QStringLiteral( "Interface text" ), 8, 20, 1, QStringLiteral( " pt" ),
                               QStringLiteral( "Menus, panels, and dialogs; headings follow and small text stays one point smaller." ) );
        m_pConsoleSize = AddSpin( pLeft, "AppearanceConsoleTextSize", QStringLiteral( "Console text" ), 7, 24, 1, QStringLiteral( " pt" ),
                                  QStringLiteral( "Console, Output, and Problems." ) );
        m_pDensity = new QComboBox( pGroup );
        m_pDensity->setObjectName( QStringLiteral( "AppearanceDensity" ) );
        m_pDensity->setToolTip( QStringLiteral( "Padding, spacing, and row height." ) );
        if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, QStringLiteral( "ui.density" ) ) ) {
            for ( usize i = 0u; i < pToken->nChoices; ++i ) { m_pDensity->addItem( QString::fromUtf8( pToken->ppChoices[i] ) ); }
        }
        pLeft->addRow( QStringLiteral( "Density" ), m_pDensity );

        m_pIconSize = AddSpin( pRight, "AppearanceIconSize", QStringLiteral( "Toolbar icons" ), 16, 48, 2, QStringLiteral( " px" ),
                               QStringLiteral( "Main, Select, and View toolbars." ) );
        m_pPaletteIconSize = AddSpin( pRight, "AppearancePaletteIconSize", QStringLiteral( "Tool palette icons" ), 16, 48, 2, QStringLiteral( " px" ),
                                      QStringLiteral( "The two-column tool palette on the left." ) );
        m_pMenuIconSize = AddSpin( pRight, "AppearanceMenuIconSize", QStringLiteral( "Menu icons" ), 12, 32, 2, QStringLiteral( " px" ),
                                   QStringLiteral( "Icons beside menu entries." ) );

        // Pane cues are editor settings, not theme values: they apply at once
        // and are kept whatever theme is chosen.
        for ( const char *pPath : { "editor.viewport.active_border", "editor.viewport.highlight_active" } ) {
            if ( QWidget *pControl = EditorSettingControl_Create( pGroup, &m_pGui->settings, pPath ) ) {
                pLeft->addRow( EditorSettingControl_Label( &m_pGui->settings, pPath ), pControl );
            }
        }

        const auto fontEdit = [this]( QSpinBox *pSpin, std::initializer_list<std::pair<const char *, int>> tokens ) {
            QObject::connect( pSpin, &QSpinBox::valueChanged, this, [this, tokens = std::vector( tokens )]( int nSize ) {
                if ( m_bRefreshing ) { return; }
                for ( const auto &[pId, nOffset] : tokens ) { WriteFontSize( pId, std::max( 1, nSize + nOffset ) ); }
                EditorThemeDraft_Preview( &m_draft );
                Changed();
            } );
        };
        fontEdit( m_pTextSize, { { "ui", 0 }, { "ui.heading", 0 }, { "ui.small", -1 } } );
        fontEdit( m_pConsoleSize, { { "console", 0 } } );
        const auto metricEdit = [this]( QSpinBox *pSpin, const char *pId ) {
            QObject::connect( pSpin, &QSpinBox::valueChanged, this, [this, pId]( int nValue ) {
                const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, QString::fromLatin1( pId ) );
                if ( m_bRefreshing || pToken == nullptr ) { return; }
                theme_value_t value{};
                value.flMetric = static_cast<f64>( nValue );
                EditorThemeDraft_Write( &m_draft, *pToken, value );
                Changed();
            } );
        };
        metricEdit( m_pIconSize, "ui.icon_size" );
        metricEdit( m_pPaletteIconSize, "ui.tool_strip.icon_size" );
        metricEdit( m_pMenuIconSize, "ui.menu.icon_size" );
        QObject::connect( m_pDensity, &QComboBox::currentTextChanged, this, [this]( const QString &text ) {
            const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, QStringLiteral( "ui.density" ) );
            if ( m_bRefreshing || pToken == nullptr ) { return; }
            const QByteArray utf8 = text.toUtf8();
            theme_value_t value{};
            value.choice = ViewOf( utf8 );
            EditorThemeDraft_Write( &m_draft, *pToken, value );
            Changed();
        } );
        return pGroup;
    }

    void BuildColorSection( QWidget *pParent, QVBoxLayout *pLayout )
    {
        auto *pTools = new QHBoxLayout();
        m_pFilter = new QLineEdit( pParent );
        m_pFilter->setObjectName( QStringLiteral( "AppearanceColorFilter" ) );
        m_pFilter->setPlaceholderText( QStringLiteral( "Filter colours by name or token, or #hex to find a colour" ) );
        m_pFilter->setClearButtonEnabled( true );
        m_pDerived = new QCheckBox( QStringLiteral( "Show derived colours" ), pParent );
        m_pDerived->setObjectName( QStringLiteral( "AppearanceShowDerived" ) );
        m_pDerived->setChecked( true );
        m_pDerived->setToolTip( QStringLiteral( "Derived colours follow a formula of other colours until you pin them." ) );
        m_pRevertAll = new QPushButton( QStringLiteral( "Revert Changes" ), pParent );
        m_pRevertAll->setObjectName( QStringLiteral( "AppearanceRevert" ) );
        m_pRevertAll->setToolTip( QStringLiteral( "Back to the chosen theme as it was; colours, sizes, and fonts." ) );
        pTools->addWidget( m_pFilter, 1 );
        pTools->addWidget( m_pDerived );
        pTools->addWidget( m_pRevertAll );
        pLayout->addLayout( pTools );
        QObject::connect( m_pFilter, &QLineEdit::textChanged, this, [this]( const QString & ) { Relayout(); } );
        QObject::connect( m_pDerived, &QCheckBox::toggled, this, [this]( bool ) { Relayout(); } );
        QObject::connect( m_pRevertAll, &QPushButton::clicked, this, [this]() { Revert(); } );

        QHash<QString, usize> groupIndex;
        for ( const theme_token_t *pToken : m_draft.order ) {
            if ( pToken->kind != theme_token_kind_t::COLOR ) { continue; }
            const QString title = GroupTitle( pToken->pGroup );
            if ( !groupIndex.contains( title ) ) {
                color_group_t group{};
                group.pBox = new QGroupBox( title, pParent );
                group.pBox->setObjectName( QStringLiteral( "AppearanceColorGroup_%1" ).arg( QString( title ).replace( QLatin1Char( ' ' ), QLatin1Char( '_' ) ) ) );
                group.pGrid = new QGridLayout( group.pBox );
                group.pGrid->setHorizontalSpacing( 18 );
                group.pGrid->setVerticalSpacing( 3 );
                for ( int c = 0; c < kColorColumns; ++c ) { group.pGrid->setColumnStretch( c, 1 ); }
                groupIndex.insert( title, m_groups.size() );
                m_groups.push_back( group );
                pLayout->addWidget( group.pBox );
            }
            color_group_t &group = m_groups[groupIndex.value( title )];
            group.rows.push_back( m_rows.size() );
            m_rows.push_back( BuildRow( group.pBox, *pToken ) );
        }
        Relayout();
    }

    color_row_t BuildRow( QGroupBox *pGroup, const theme_token_t &token )
    {
        color_row_t row{};
        row.pToken = &token;
        row.pGroup = pGroup;
        row.pRow = new QWidget( pGroup );
        row.pRow->setObjectName( QStringLiteral( "AppearanceColorRow" ) );
        auto *pLayout = new QHBoxLayout( row.pRow );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 4 );
        const QString id = QString::fromUtf8( token.pId );
        row.pLabel = new QLabel( QString::fromUtf8( token.pLabel != nullptr ? token.pLabel : token.pId ), row.pRow );
        row.pLabel->setMinimumWidth( 60 );
        row.pColor = new QToolButton( row.pRow );
        row.pColor->setObjectName( QStringLiteral( "AppearanceColor_%1" ).arg( id ) );
        row.pColor->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        row.pColor->setIconSize( kRowSwatch );
        row.pColor->setFixedWidth( 118 );
        row.pColor->setContextMenuPolicy( Qt::CustomContextMenu );
        const bool bDerived = token.derive.op != theme_derive_op_t::NONE;
        row.pAuto = new QToolButton( row.pRow );
        row.pAuto->setText( QStringLiteral( "Auto" ) );
        row.pAuto->setCheckable( true );
        row.pAuto->setFixedWidth( 42 );
        if ( bDerived ) {
            row.pAuto->setToolTip( QStringLiteral( "Follow the formula %1. Uncheck to pin today's colour." )
                                       .arg( EditorThemeDraft_FormulaText( token.derive ) ) );
        } else {
            // Base colours have no formula; the button keeps the columns aligned.
            QSizePolicy policy = row.pAuto->sizePolicy();
            policy.setRetainSizeWhenHidden( true );
            row.pAuto->setSizePolicy( policy );
            row.pAuto->hide();
        }
        row.pRevert = new QToolButton( row.pRow );
        row.pRevert->setText( QStringLiteral( "↺" ) );
        row.pRevert->setFixedWidth( 24 );
        pLayout->addWidget( row.pLabel, 1 );
        pLayout->addWidget( row.pColor );
        pLayout->addWidget( row.pAuto );
        pLayout->addWidget( row.pRevert );

        const theme_token_t *pToken = &token;
        QObject::connect( row.pColor, &QToolButton::clicked, this, [this, pToken]() { PickColor( *pToken ); } );
        QObject::connect( row.pColor, &QToolButton::customContextMenuRequested, this, [this, pToken, pButton = row.pColor]( const QPoint &at ) {
            ColorMenu( *pToken, pButton->mapToGlobal( at ) );
        } );
        QObject::connect( row.pAuto, &QToolButton::clicked, this, [this, pToken]( bool bChecked ) {
            if ( bChecked ) {
                EditorThemeDraft_SetFormula( &m_draft, *pToken );
            } else {
                theme_value_t pinned = EditorThemeDraft_Value( &m_draft, *pToken );
                pinned.bFormula = false; // Keep the colour the formula gives now.
                EditorThemeDraft_Write( &m_draft, *pToken, pinned );
            }
            Changed();
        } );
        QObject::connect( row.pRevert, &QToolButton::clicked, this, [this, id]() { RevertColor( id ); } );
        return row;
    }

    // ---- Editing ---------------------------------------------------------

    void WriteFontSize( const char *pId, int nSize )
    {
        const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, QString::fromLatin1( pId ) );
        if ( pToken == nullptr ) { return; }
        const theme_value_t current = EditorThemeDraft_Value( &m_draft, *pToken );
        // The family borrows the draft document, which the write replaces.
        const QByteArray family = FromView( current.font.family ).toUtf8();
        theme_value_t value{};
        value.font = theme_font_t{ ViewOf( family ), static_cast<f64>( nSize ), current.font.nWeight };
        EditorThemeDraft_Write( &m_draft, *pToken, value, false );
    }

    // Live: every colour the dialog shows is previewed through the whole
    // editor; cancelling puts the earlier value back.
    void PickColor( const theme_token_t &token )
    {
        const theme_value_t before = EditorThemeDraft_Value( &m_draft, token );
        QColorDialog dialog( EditorThemeDraft_ToColor( before.rgba ), this );
        dialog.setOptions( QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog );
        dialog.setWindowTitle( QString::fromUtf8( token.pLabel != nullptr ? token.pLabel : token.pId ) );
        const theme_token_t *pToken = &token;
        QObject::connect( &dialog, &QColorDialog::currentColorChanged, this, [this, pToken]( const QColor &color ) {
            EditorThemeDraft_SetColor( &m_draft, *pToken, color, false );
            m_pPreviewTimer->start();
        } );
        m_pPreviewTimer->stop();
        if ( dialog.exec() == QDialog::Accepted ) {
            EditorThemeDraft_SetColor( &m_draft, token, dialog.selectedColor() );
        } else {
            EditorThemeDraft_Write( &m_draft, token, before );
        }
        m_pPreviewTimer->stop();
        Changed();
    }

    void ColorMenu( const theme_token_t &token, const QPoint &at )
    {
        const theme_value_t value = EditorThemeDraft_Value( &m_draft, token );
        QMenu menu( this );
        QAction *pCopy = menu.addAction( QStringLiteral( "Copy %1" ).arg( EditorThemeDraft_ColorText( value.rgba ) ) );
        QAction *pPaste = menu.addAction( QStringLiteral( "Paste Colour" ) );
        u32 pasted = 0u;
        const QByteArray clip = QApplication::clipboard()->text().trimmed().toUtf8();
        pPaste->setEnabled( SettingColor_Parse( ViewOf( clip ), &pasted ) );
        menu.addSeparator();
        QAction *pFormula = menu.addAction( QStringLiteral( "Follow Formula" ) );
        pFormula->setEnabled( token.derive.op != theme_derive_op_t::NONE && !value.bFormula );
        QAction *pRevert = menu.addAction( QStringLiteral( "Revert" ) );
        pRevert->setEnabled( EditorThemeDraft_IsChanged( &m_draft, token ) );
        QAction *pChosen = menu.exec( at );
        if ( pChosen == pCopy ) {
            QApplication::clipboard()->setText( EditorThemeDraft_ColorText( value.rgba ) );
        } else if ( pChosen == pPaste ) {
            theme_value_t next{};
            next.rgba = pasted;
            EditorThemeDraft_Write( &m_draft, token, next );
            Changed();
        } else if ( pChosen == pFormula ) {
            EditorThemeDraft_SetFormula( &m_draft, token );
            Changed();
        } else if ( pChosen == pRevert ) {
            EditorThemeDraft_RevertToken( &m_draft, token );
            Changed();
        }
    }

    // ---- Themes ----------------------------------------------------------

    QString ThemePath( const QString &id ) const { return QDir( m_folder ).filePath( id + QStringLiteral( ".cytheme" ) ); }

    bool IsUserTheme( const QString &id ) const
    {
        return !id.isEmpty() && id != m_draft.builtinId && !m_folder.isEmpty() && QFileInfo::exists( ThemePath( id ) );
    }

    const key_value_t *ThemeRoot( const QString &id ) const
    {
        const QByteArray utf8 = id.toUtf8();
        for ( usize i = 0u; i < m_pGui->themeLibrary.roots.nCount; ++i ) {
            const key_value_t *pRoot = m_pGui->themeLibrary.roots.pData[i];
            if ( StringView_Equals( EditorTheme_Header( pRoot ).id, ViewOf( utf8 ) ) ) { return pRoot; }
        }
        return nullptr;
    }

    QString ThemeName( const QString &id ) const
    {
        const key_value_t *pRoot = ThemeRoot( id );
        const QString name = pRoot != nullptr ? FromView( EditorTheme_Header( pRoot ).name ) : QString();
        return name.isEmpty() ? id : name;
    }

    // Three stripes - interface, accent, 2D view with a grid line - so the
    // list reads like the palette it applies.
    QIcon ThemeIcon( const key_value_t *pRoot ) const
    {
        const key_value_t *chain[EDITOR_THEME_MAX_BASE_DEPTH]{};
        bool_t bComplete = CY_FALSE;
        const usize nChain = EditorTheme_BuildChain( &m_pGui->themeLibrary, pRoot, chain, std::size( chain ), &bComplete );
        const auto color = [&]( const char *pId ) {
            const theme_token_t *pToken = EditorThemeRegistry_Find( &m_pGui->themeTokens, StringView_FromCString( pId ) );
            return pToken != nullptr ? EditorThemeDraft_ToColor( EditorTheme_ResolveColorIn( &m_pGui->themeTokens, chain, nChain, *pToken, nullptr ) )
                                     : QColor( Qt::gray );
        };
        QPixmap pixmap( 36, 16 );
        pixmap.fill( Qt::transparent );
        QPainter painter( &pixmap );
        painter.fillRect( 0, 0, 14, 16, color( "ui.background" ) );
        painter.fillRect( 2, 11, 10, 3, color( "ui.accent" ) );
        painter.fillRect( 14, 0, 22, 16, color( "viewport.background.2d" ) );
        painter.setPen( color( "viewport.grid.major" ) );
        painter.drawLine( 14, 8, 35, 8 );
        painter.drawLine( 25, 0, 25, 15 );
        painter.fillRect( 28, 3, 5, 3, color( "viewport.selection" ) );
        painter.setPen( QColor( 0, 0, 0, 150 ) );
        painter.drawRect( pixmap.rect().adjusted( 0, 0, -1, -1 ) );
        return QIcon( pixmap );
    }

    void RefreshThemeList()
    {
        struct entry_t {
            QString id, name, description;
            int kind;
            const key_value_t *pRoot;
        };
        std::vector<entry_t> entries;
        for ( usize i = 0u; i < m_pGui->themeLibrary.roots.nCount; ++i ) {
            const key_value_t *pRoot = m_pGui->themeLibrary.roots.pData[i];
            const theme_header_t header = EditorTheme_Header( pRoot );
            const QString id = FromView( header.id );
            // The library lists the newest theme of an ID first; it is the one in use.
            if ( id.isEmpty() || std::any_of( entries.begin(), entries.end(), [&id]( const entry_t &e ) { return e.id == id; } ) ) { continue; }
            const int kind = id == m_draft.builtinId ? THEME_BUILTIN : IsUserTheme( id ) ? THEME_USER : THEME_PRESET;
            entries.push_back( { id, FromView( header.name ), FromView( header.description ), kind, pRoot } );
        }
        std::stable_sort( entries.begin(), entries.end(), []( const entry_t &a, const entry_t &b ) {
            return a.kind != b.kind ? a.kind < b.kind : a.name.localeAwareCompare( b.name ) < 0;
        } );
        const QSignalBlocker blocker( m_pThemes );
        m_pThemes->clear();
        int lastKind = 0;
        int iCurrent = -1;
        for ( const entry_t &entry : entries ) {
            if ( lastKind != 0 && entry.kind == THEME_USER && lastKind != THEME_USER ) { m_pThemes->insertSeparator( m_pThemes->count() ); }
            lastKind = entry.kind;
            const QString text = entry.kind == THEME_BUILTIN ? QStringLiteral( "%1 (default)" ).arg( entry.name ) : entry.name;
            m_pThemes->addItem( ThemeIcon( entry.pRoot ), text );
            const int index = m_pThemes->count() - 1;
            m_pThemes->setItemData( index, entry.id, kIdRole );
            m_pThemes->setItemData( index, entry.kind, kKindRole );
            QString tip = entry.description.isEmpty() ? entry.name : entry.description;
            tip += entry.kind == THEME_USER ? QStringLiteral( "\nUser theme: %1" ).arg( QDir::toNativeSeparators( ThemePath( entry.id ) ) )
                                            : QStringLiteral( "\nShips with the editor (ID %1)." ).arg( entry.id );
            m_pThemes->setItemData( index, tip, Qt::ToolTipRole );
            if ( entry.id == m_draft.previousId ) { iCurrent = index; }
        }
        if ( EditorThemeDraft_HasChanges( &m_draft ) ) {
            m_pThemes->insertSeparator( m_pThemes->count() );
            m_pThemes->addItem( QStringLiteral( "%1 (modified)" ).arg( ThemeName( m_draft.previousId ) ) );
            iCurrent = m_pThemes->count() - 1;
            m_pThemes->setItemData( iCurrent, THEME_MODIFIED, kKindRole );
            m_pThemes->setItemData( iCurrent, QStringLiteral( "Unsaved changes to %1. Save keeps them; Revert Changes drops them." )
                                                  .arg( ThemeName( m_draft.previousId ) ),
                                    Qt::ToolTipRole );
        }
        m_pThemes->setCurrentIndex( iCurrent );
    }

    void RecordTheme( const QString &id )
    {
        const setting_descriptor_t *pSetting = EditorSettings_Find( &m_pGui->settings, StringView_FromCString( "editor.ui.theme" ) );
        if ( pSetting == nullptr ) { return; }
        const QByteArray utf8 = id.toUtf8();
        setting_value_t value{};
        value.type = setting_type_t::STRING;
        value.text = ViewOf( utf8 );
        const settings_registry_status_t status = EditorSettings_Write( &m_pGui->settings, settings_scope_t::USER, *pSetting, value );
        if ( status != settings_registry_status_t::OK && status != settings_registry_status_t::NO_SCOPE ) {
            CY_LOG_WRITE( Warning, Gui, "Chosen theme could not be recorded in the settings" );
        }
    }

    void ChooseTheme( const QString &id )
    {
        if ( id.isEmpty() ) { return; } // The "(modified)" entry is where we already are.
        if ( id == m_draft.previousId && !HasChanges() ) { return; }
        if ( HasChanges() && isVisible() &&
             QMessageBox::question( this, QStringLiteral( "Appearance" ),
                                    QStringLiteral( "Discard the unsaved colour changes and use %1?" ).arg( ThemeName( id ) ) ) != QMessageBox::Yes ) {
            RefreshThemeList(); // Back to the "(modified)" entry.
            return;
        }
        m_pPreviewTimer->stop();
        m_bApplying = true;
        EditorThemeDraft_RevertAll( &m_draft );
        const QByteArray utf8 = id.toUtf8();
        const editor_gui_status_t status = EditorGui_SelectTheme( m_pGui, m_pApplication, ViewOf( utf8 ) );
        if ( status == editor_gui_status_t::OK ) { RecordTheme( id ); }
        EditorThemeDraft_Rebase( &m_draft );
        m_bApplying = false;
        Changed();
        if ( status == editor_gui_status_t::OK ) {
            SetStatus( QStringLiteral( "Using %1. It is also the theme the editor starts with." ).arg( ThemeName( id ) ) );
        } else {
            SetStatus( QStringLiteral( "The theme \"%1\" could not be applied." ).arg( id ), true );
        }
    }

    QString SaveDraft( const QString &id, const QString &name )
    {
        m_pPreviewTimer->stop();
        const key_value_t *pRoot = ThemeRoot( id );
        const theme_header_t header = pRoot != nullptr ? EditorTheme_Header( pRoot ) : theme_header_t{};
        QString error;
        m_bApplying = true;
        const QString path = EditorThemeDraft_Save( &m_draft, id, name, FromView( header.author ), FromView( header.description ), m_folder, &error );
        m_bApplying = false;
        Changed();
        if ( path.isEmpty() ) {
            SetStatus( error, true );
            if ( isVisible() ) { QMessageBox::warning( this, QStringLiteral( "Save Theme" ), error ); }
            return {};
        }
        SetStatus( QStringLiteral( "Saved %1 to %2." ).arg( name, QDir::toNativeSeparators( path ) ) );
        return path;
    }

    bool AskSaveAs()
    {
        const QString suggested = QStringLiteral( "%1 Custom" ).arg( ThemeName( m_draft.previousId ) );
        bool bOk = false;
        const QString name = QInputDialog::getText( this, QStringLiteral( "Save Theme As" ), QStringLiteral( "Theme name" ), QLineEdit::Normal, suggested, &bOk )
                                 .trimmed();
        if ( !bOk || name.isEmpty() ) { return false; }
        QString id = IdFromName( name );
        // Presets and the default theme ship with the editor; a user theme of
        // the same ID would hide them.
        if ( id == m_draft.builtinId || ( ThemeRoot( id ) != nullptr && !IsUserTheme( id ) ) ) { id += QStringLiteral( "_custom" ); }
        if ( IsUserTheme( id ) &&
             QMessageBox::question( this, QStringLiteral( "Save Theme As" ), QStringLiteral( "Replace your theme \"%1\"?" ).arg( ThemeName( id ) ) ) !=
                 QMessageBox::Yes ) {
            return false;
        }
        return !SaveDraft( id, name ).isEmpty();
    }

    void Import()
    {
        const QString source = QFileDialog::getOpenFileName( this, QStringLiteral( "Import Theme" ), QDir::homePath(), QStringLiteral( "Themes (*.cytheme)" ) );
        if ( source.isEmpty() ) { return; }
        QFile file( source );
        if ( !file.open( QIODevice::ReadOnly ) ) {
            SetStatus( QStringLiteral( "The file could not be read: %1" ).arg( QDir::toNativeSeparators( source ) ), true );
            return;
        }
        const QByteArray contents = file.readAll();
        settings_document_t probe{};
        QString id;
        if ( SettingsDocument_Init( &probe, m_pGui->pAllocator, EditorTheme_Identity() ) == settings_document_status_t::OK &&
             SettingsDocument_Load( &probe, ViewOf( contents ) ).status == settings_document_status_t::OK ) {
            id = FromView( EditorTheme_Header( SettingsDocument_Root( &probe ) ).id );
        }
        SettingsDocument_Shutdown( &probe );
        if ( id.isEmpty() || id == m_draft.builtinId ) {
            SetStatus( id.isEmpty() ? QStringLiteral( "That file is not a theme with an ID." ) : QStringLiteral( "A theme cannot replace the default theme." ), true );
            return;
        }
        const QString target = ThemePath( id );
        if ( QFileInfo::exists( target ) && QFileInfo( target ) != QFileInfo( source ) &&
             QMessageBox::question( this, QStringLiteral( "Import Theme" ), QStringLiteral( "Replace your theme \"%1\"?" ).arg( ThemeName( id ) ) ) !=
                 QMessageBox::Yes ) {
            return;
        }
        QSaveFile copy( target );
        if ( !QDir().mkpath( m_folder ) || !copy.open( QIODevice::WriteOnly ) || copy.write( contents ) < 0 || !copy.commit() ||
             EditorGui_AddTheme( m_pGui, QString::fromUtf8( contents ) ) != editor_gui_status_t::OK ) {
            SetStatus( QStringLiteral( "The theme could not be imported into %1." ).arg( QDir::toNativeSeparators( m_folder ) ), true );
            return;
        }
        ChooseTheme( id );
    }

    void Export()
    {
        const bool bModified = HasChanges();
        const QString id = bModified || m_draft.previousId == m_draft.builtinId ? m_draft.previousId + QStringLiteral( "_custom" ) : m_draft.previousId;
        const QString name = bModified ? QStringLiteral( "%1 Custom" ).arg( ThemeName( m_draft.previousId ) ) : ThemeName( m_draft.previousId );
        const QString target = QFileDialog::getSaveFileName( this, QStringLiteral( "Export Theme" ), QDir( QDir::homePath() ).filePath( id + QStringLiteral( ".cytheme" ) ),
                                                             QStringLiteral( "Themes (*.cytheme)" ) );
        if ( target.isEmpty() ) { return; }
        const QString contents = EditorThemeDraft_Text( &m_draft, id, name, QString(), QString() );
        QSaveFile file( target );
        if ( contents.isEmpty() || !file.open( QIODevice::WriteOnly ) || file.write( contents.toUtf8() ) < 0 || !file.commit() ) {
            SetStatus( QStringLiteral( "The theme could not be exported to %1." ).arg( QDir::toNativeSeparators( target ) ), true );
            return;
        }
        SetStatus( QStringLiteral( "Exported %1 to %2." ).arg( name, QDir::toNativeSeparators( target ) ) );
    }

    // ---- Refreshing ------------------------------------------------------

    static void OnStyleChanged( void *pContext ) noexcept
    {
        auto *pPage = static_cast<appearance_page_t *>( pContext );
        if ( pPage->m_bApplying || EditorThemeDraft_IsPreviewing( &pPage->m_draft ) ) { return; }
        const QString active = FromView( EditorGui_ActiveThemeId( pPage->m_pGui ) );
        // Another editor's working copy is not a theme to rebase on.
        if ( pPage->ThemeRoot( active ) == nullptr ) { return; }
        if ( active != pPage->m_draft.previousId ) { EditorThemeDraft_Rebase( &pPage->m_draft ); }
        pPage->Changed(); // New themes may have been saved too.
    }

    void Changed()
    {
        RefreshThemeList();
        RefreshInterface();
        RefreshRows();
        const bool bChanges = HasChanges();
        const bool bUser = IsUserTheme( m_draft.previousId );
        m_pSave->setEnabled( bChanges && bUser );
        m_pDelete->setEnabled( bUser );
        m_pRevertAll->setEnabled( bChanges );
        // The Theme Editor starts from the applied theme; unsaved edits here
        // would be lost to it.
        m_pThemeEditor->setEnabled( !bChanges );
        m_pThemeEditor->setToolTip( bChanges ? QStringLiteral( "Save or revert the changes here first." )
                                             : QStringLiteral( "Every token - colours, fonts, and sizes - in a searchable tree." ) );
    }

    void RefreshInterface()
    {
        m_bRefreshing = true;
        const auto font = [this]( const char *pId ) {
            const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, QString::fromLatin1( pId ) );
            return pToken != nullptr ? static_cast<int>( EditorThemeDraft_Value( &m_draft, *pToken ).font.flSize + 0.5 ) : 0;
        };
        const auto metric = [this]( const char *pId ) {
            const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, QString::fromLatin1( pId ) );
            return pToken != nullptr ? static_cast<int>( EditorThemeDraft_Value( &m_draft, *pToken ).flMetric + 0.5 ) : 0;
        };
        m_pTextSize->setValue( font( "ui" ) );
        m_pConsoleSize->setValue( font( "console" ) );
        m_pIconSize->setValue( metric( "ui.icon_size" ) );
        m_pPaletteIconSize->setValue( metric( "ui.tool_strip.icon_size" ) );
        m_pMenuIconSize->setValue( metric( "ui.menu.icon_size" ) );
        if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, QStringLiteral( "ui.density" ) ) ) {
            m_pDensity->setCurrentText( FromView( EditorThemeDraft_Value( &m_draft, *pToken ).choice ) );
        }
        m_bRefreshing = false;
    }

    void RefreshRows()
    {
        for ( color_row_t &row : m_rows ) {
            const theme_value_t value = EditorThemeDraft_Value( &m_draft, *row.pToken );
            const bool bChanged = EditorThemeDraft_IsChanged( &m_draft, *row.pToken );
            const QString hex = EditorThemeDraft_ColorText( value.rgba );
            row.pColor->setIcon( Swatch( value.rgba, kRowSwatch ) );
            row.pColor->setText( hex );
            const QString id = QString::fromUtf8( row.pToken->pId );
            QString tip = QStringLiteral( "%1\nClick to choose; right-click to copy or paste." ).arg( id );
            if ( row.pToken->derive.op != theme_derive_op_t::NONE ) {
                tip += value.bFormula ? QStringLiteral( "\nFollows %1." ).arg( EditorThemeDraft_FormulaText( row.pToken->derive ) )
                                      : QStringLiteral( "\nPinned; Auto would follow %1." ).arg( EditorThemeDraft_FormulaText( row.pToken->derive ) );
            }
            row.pColor->setToolTip( tip );
            row.pLabel->setToolTip( tip );
            row.pAuto->setChecked( value.bFormula );
            row.pRevert->setEnabled( bChanged );
            row.pRevert->setToolTip(
                bChanged ? QStringLiteral( "Back to %1" ).arg( EditorThemeDraft_ColorText( EditorThemeDraft_Value( &m_draft, *row.pToken, true ).rgba ) )
                         : QStringLiteral( "Unchanged" ) );
            QFont font = row.pLabel->font();
            font.setBold( bChanged );
            row.pLabel->setFont( font );
        }
    }

    bool RowMatches( const color_row_t &row, const QString &query ) const
    {
        if ( !m_pDerived->isChecked() && row.pToken->derive.op != theme_derive_op_t::NONE ) { return false; }
        if ( query.isEmpty() ) { return true; }
        if ( query.startsWith( QLatin1Char( '#' ) ) ) { return row.pColor->text().startsWith( query, Qt::CaseInsensitive ); }
        const QByteArray utf8 = query.toUtf8();
        const auto matches = [&utf8]( const QString &text ) {
            return EditorFuzzy_Score( ViewOf( text.toUtf8() ), ViewOf( utf8 ) ) != EDITOR_FUZZY_NO_MATCH;
        };
        return matches( row.pLabel->text() ) || matches( QString::fromUtf8( row.pToken->pId ) ) || matches( row.pGroup->title() );
    }

    // Visible rows flow into the columns in catalogue order; a filter would
    // otherwise leave holes in the grid.
    void Relayout()
    {
        const QString query = m_pFilter->text().trimmed();
        for ( color_group_t &group : m_groups ) {
            int iVisible = 0;
            for ( usize iRow : group.rows ) {
                color_row_t &row = m_rows[iRow];
                group.pGrid->removeWidget( row.pRow );
                const bool bShow = RowMatches( row, query );
                row.pRow->setVisible( bShow );
                if ( !bShow ) { continue; }
                group.pGrid->addWidget( row.pRow, iVisible / kColorColumns, iVisible % kColorColumns );
                ++iVisible;
            }
            group.pBox->setVisible( iVisible != 0 );
        }
    }

    void SetStatus( const QString &text, bool bError = false )
    {
        m_pStatus->setText( text );
        m_pStatus->setProperty( "error", bError );
        m_pStatus->style()->unpolish( m_pStatus );
        m_pStatus->style()->polish( m_pStatus );
    }

    editor_gui_t *m_pGui{ nullptr };
    QApplication *m_pApplication{ nullptr };
    QString m_folder{};
    editor_theme_draft_t m_draft{};
    std::vector<color_row_t> m_rows{};
    std::vector<color_group_t> m_groups{};
    QComboBox *m_pThemes{ nullptr };
    QPushButton *m_pSave{ nullptr };
    QPushButton *m_pDelete{ nullptr };
    QPushButton *m_pThemeEditor{ nullptr };
    QLabel *m_pStatus{ nullptr };
    QSpinBox *m_pTextSize{ nullptr };
    QSpinBox *m_pConsoleSize{ nullptr };
    QSpinBox *m_pIconSize{ nullptr };
    QSpinBox *m_pPaletteIconSize{ nullptr };
    QSpinBox *m_pMenuIconSize{ nullptr };
    QComboBox *m_pDensity{ nullptr };
    QLineEdit *m_pFilter{ nullptr };
    QCheckBox *m_pDerived{ nullptr };
    QPushButton *m_pRevertAll{ nullptr };
    QTimer *m_pPreviewTimer{ nullptr };
    bool m_bRefreshing{ false };
    bool m_bApplying{ false }; // The page itself is switching themes; the style listener stands aside.
};

appearance_page_t *AsPage( QWidget *pPage )
{
    auto *pImpl = dynamic_cast<appearance_page_t *>( pPage );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QWidget *EditorAppearancePage_Create( QWidget *pParent, editor_gui_t *pGui, QApplication *pApplication, const QString &userThemeFolder )
{
    CY_ASSERT( pGui != nullptr && pGui->bInitialized && pApplication != nullptr );
    if ( pGui == nullptr || !pGui->bInitialized || pApplication == nullptr ) { return nullptr; }
    return new appearance_page_t( pParent, pGui, pApplication, userThemeFolder );
}

bool EditorAppearancePage_CanClose( QWidget *pPage )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl == nullptr || pImpl->CanClose();
}

QStringList EditorAppearancePage_Keywords()
{
    return { QStringLiteral( "theme" ),      QStringLiteral( "colour" ),  QStringLiteral( "color" ), QStringLiteral( "dark" ),
             QStringLiteral( "light" ),      QStringLiteral( "font" ),    QStringLiteral( "icon size" ), QStringLiteral( "density" ),
             QStringLiteral( "background" ), QStringLiteral( "accent" ),  QStringLiteral( "grid colour" ), QStringLiteral( "palette" ) };
}

QStringList EditorAppearancePage_Themes( QWidget *pPage )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl != nullptr ? pImpl->Themes() : QStringList{};
}

QString EditorAppearancePage_CurrentTheme( QWidget *pPage )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl != nullptr ? pImpl->CurrentTheme() : QString();
}

void EditorAppearancePage_SelectTheme( QWidget *pPage, const QString &id )
{
    if ( appearance_page_t *pImpl = AsPage( pPage ) ) { pImpl->SelectTheme( id ); }
}

QStringList EditorAppearancePage_ColorTokens( QWidget *pPage )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl != nullptr ? pImpl->ColorTokens() : QStringList{};
}

void EditorAppearancePage_SetFilter( QWidget *pPage, const QString &text )
{
    if ( appearance_page_t *pImpl = AsPage( pPage ) ) { pImpl->SetFilter( text ); }
}

void EditorAppearancePage_SetColor( QWidget *pPage, const QString &tokenId, const QColor &color )
{
    if ( appearance_page_t *pImpl = AsPage( pPage ) ) { pImpl->SetColor( tokenId, color ); }
}

void EditorAppearancePage_SetAuto( QWidget *pPage, const QString &tokenId )
{
    if ( appearance_page_t *pImpl = AsPage( pPage ) ) { pImpl->SetAuto( tokenId ); }
}

void EditorAppearancePage_RevertColor( QWidget *pPage, const QString &tokenId )
{
    if ( appearance_page_t *pImpl = AsPage( pPage ) ) { pImpl->RevertColor( tokenId ); }
}

void EditorAppearancePage_Revert( QWidget *pPage )
{
    if ( appearance_page_t *pImpl = AsPage( pPage ) ) { pImpl->Revert(); }
}

bool EditorAppearancePage_HasChanges( QWidget *pPage )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl != nullptr && pImpl->HasChanges();
}

QString EditorAppearancePage_SaveAs( QWidget *pPage, const QString &id, const QString &name )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl != nullptr ? pImpl->SaveAs( id, name ) : QString();
}

bool EditorAppearancePage_DeleteTheme( QWidget *pPage, const QString &id )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl != nullptr && pImpl->DeleteTheme( id );
}

QString EditorAppearancePage_Status( QWidget *pPage )
{
    appearance_page_t *pImpl = AsPage( pPage );
    return pImpl != nullptr ? pImpl->Status() : QString();
}

} // namespace cypher::editor::gui
