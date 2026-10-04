//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ThemeEditor.cpp
//  Purpose: Implements the theme editor dialog.
//  Details: The working copy, preview, revert, and save live in the shared
//           theme draft (CypherEditorGui_ThemeDraft.h); this file is the
//           token tree and the per-kind editors around it.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//  - 2026-09-30: built on the shared theme draft
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_ThemeEditor.h"

#include "CypherEditorGui_ThemeDraft.h"
#include "CypherEditor_FuzzyMatch.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr int kTokenRole = Qt::UserRole;
constexpr QSize kPickSwatch( 44, 16 );
constexpr int kPreviewDelayMs = 30; // Coalesces colour-dialog drags; a full restyle per mouse move stutters.

enum editor_page_t : int { PAGE_NONE = 0, PAGE_COLOR, PAGE_FONT, PAGE_METRIC, PAGE_CHOICE };

string_view_t ViewOf( const QByteArray &bytes ) noexcept
{
    return string_view_t{ bytes.constData(), static_cast<usize>( bytes.size() ) };
}

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

// A checkerboard shows through translucent colours, and a thin outline
// keeps a swatch visible when it matches the background.
QIcon Swatch( u32 rgba, QSize size = QSize( 14, 14 ) )
{
    QPixmap swatch( size );
    swatch.fill( Qt::transparent );
    QPainter painter( &swatch );
    const int cell = 4;
    for ( int y = 0; y < size.height(); y += cell ) {
        for ( int x = 0; x < size.width(); x += cell ) {
            painter.fillRect( x, y, cell, cell, ( ( x + y ) / cell ) % 2 == 0 ? QColor( 0xCC, 0xCC, 0xCC ) : QColor( 0x88, 0x88, 0x88 ) );
        }
    }
    painter.fillRect( swatch.rect(), EditorThemeDraft_ToColor( rgba ) );
    painter.setPen( QColor( 0, 0, 0, 110 ) );
    painter.drawRect( swatch.rect().adjusted( 0, 0, -1, -1 ) );
    return QIcon( swatch );
}

QString ValueText( const theme_token_t &token, const theme_value_t &value )
{
    switch ( token.kind ) {
        case theme_token_kind_t::COLOR: return EditorThemeDraft_ColorText( value.rgba );
        case theme_token_kind_t::FONT:
            return QStringLiteral( "%1, %2 pt, %3" ).arg( FromView( value.font.family ) ).arg( value.font.flSize ).arg( value.font.nWeight );
        case theme_token_kind_t::METRIC: return QString::number( value.flMetric );
        case theme_token_kind_t::CHOICE: return FromView( value.choice );
    }
    return {};
}

class theme_editor_t final : public QDialog {
public:
    theme_editor_t( QWidget *pParent, editor_gui_t *pGui, QApplication *pApplication, const QString &folder )
        : QDialog( pParent ), m_pGui( pGui ), m_folder( folder )
    {
        CY_ASSERT( pGui != nullptr && pGui->bInitialized && pApplication != nullptr );
        setObjectName( QStringLiteral( "EditorThemeEditor" ) );
        setWindowTitle( QStringLiteral( "Theme Editor" ) );
        resize( 1080, 720 );
        const theme_header_t active = pGui->nThemeChain != 0u ? EditorTheme_Header( pGui->themeChain[0] ) : theme_header_t{};
        const bool bBegun = EditorThemeDraft_Begin( &m_draft, pGui, pApplication );
        CY_ASSERT( bBegun );
        ( void )bBegun;

        // Editing the built-in theme starts a copy; editing a user theme
        // saves over it by default.
        const bool bBuiltin = m_draft.previousId == m_draft.builtinId;
        auto *pRoot = new QVBoxLayout( this );
        auto *pHeader = new QFormLayout();
        m_pId = new QLineEdit( bBuiltin ? m_draft.previousId + QStringLiteral( "_custom" ) : m_draft.previousId, this );
        m_pName = new QLineEdit( bBuiltin ? FromView( active.name ) + QStringLiteral( " Custom" ) : FromView( active.name ), this );
        m_pAuthor = new QLineEdit( bBuiltin ? QString() : FromView( active.author ), this );
        m_pDescription = new QLineEdit( bBuiltin ? QString() : FromView( active.description ), this );
        m_pId->setToolTip( QStringLiteral( "Stable identifier: lower-case letters, digits, and underscores; also the file name." ) );
        pHeader->addRow( QStringLiteral( "ID" ), m_pId );
        pHeader->addRow( QStringLiteral( "Name" ), m_pName );
        pHeader->addRow( QStringLiteral( "Author" ), m_pAuthor );
        pHeader->addRow( QStringLiteral( "Description" ), m_pDescription );
        pRoot->addLayout( pHeader );

        m_pSearch = new QLineEdit( this );
        m_pSearch->setPlaceholderText( QStringLiteral( "Search tokens, or #rrggbb to find where a colour is used" ) );
        m_pSearch->setClearButtonEnabled( true );
        pRoot->addWidget( m_pSearch );

        auto *pSplitter = new QSplitter( Qt::Horizontal, this );
        m_pTree = new QTreeWidget( pSplitter );
        m_pTree->setColumnCount( 3 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Token" ), QStringLiteral( "Value" ), QStringLiteral( "State" ) } );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        m_pTree->header()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
        m_pTree->header()->setStretchLastSection( false );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setAlternatingRowColors( true );
        pSplitter->addWidget( m_pTree );
        pSplitter->addWidget( BuildEditorPane( pSplitter ) );
        pSplitter->setStretchFactor( 0, 5 );
        pSplitter->setStretchFactor( 1, 3 );
        pRoot->addWidget( pSplitter, 1 );

        auto *pButtons = new QDialogButtonBox( this );
        QPushButton *pSave = pButtons->addButton( QStringLiteral( "Save Theme" ), QDialogButtonBox::AcceptRole );
        QPushButton *pRevert = pButtons->addButton( QStringLiteral( "Revert All" ), QDialogButtonBox::ResetRole );
        pButtons->addButton( QDialogButtonBox::Close );
        pRoot->addWidget( pButtons );

        m_pPreviewTimer = new QTimer( this );
        m_pPreviewTimer->setSingleShot( true );
        m_pPreviewTimer->setInterval( kPreviewDelayMs );
        QObject::connect( m_pPreviewTimer, &QTimer::timeout, this, [this]() { EditorThemeDraft_Preview( &m_draft ); } );
        QObject::connect( pSave, &QPushButton::clicked, this, [this]() { ( void )Save( m_pId->text().trimmed(), m_pName->text().trimmed() ); } );
        QObject::connect( pRevert, &QPushButton::clicked, this, [this]() { Revert(); } );
        QObject::connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
        QObject::connect( m_pSearch, &QLineEdit::textChanged, this, [this]( const QString & ) { FillTree(); } );
        QObject::connect( m_pTree, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) { ShowEditor( pItem ); } );
        FillTree();
        m_pSearch->setFocus(); // Finding a token is the usual first step, not renaming the theme.
    }

    ~theme_editor_t() override
    {
        // Destroyed without done() (its parent went first): the style must
        // not keep pointing into the draft.
        m_pPreviewTimer->stop();
        EditorThemeDraft_End( &m_draft );
    }

    void SetSearch( const QString &text ) { m_pSearch->setText( text ); }

    QStringList VisibleTokens() const
    {
        QStringList ids;
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            const QString id = ( *it )->data( 0, kTokenRole ).toString();
            if ( !id.isEmpty() ) { ids.append( id ); }
        }
        return ids;
    }

    void SetColor( const QString &id, const QColor &color )
    {
        if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, id ) ) {
            EditorThemeDraft_SetColor( &m_draft, *pToken, color );
            RefreshValues();
        }
    }

    void SetAuto( const QString &id )
    {
        if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, id ) ) {
            EditorThemeDraft_SetFormula( &m_draft, *pToken );
            RefreshValues();
        }
    }

    void Revert()
    {
        m_pPreviewTimer->stop();
        EditorThemeDraft_RevertAll( &m_draft );
        RefreshValues();
    }

    QString Save( const QString &id, const QString &name )
    {
        m_pPreviewTimer->stop();
        QString error;
        const QString path = EditorThemeDraft_Save( &m_draft, id, name, m_pAuthor->text(), m_pDescription->text(), m_folder, &error );
        if ( path.isEmpty() ) {
            if ( isVisible() ) { QMessageBox::warning( this, windowTitle(), error ); }
            return {};
        }
        RefreshValues();
        return path;
    }

    bool HasChanges() const { return EditorThemeDraft_HasChanges( &m_draft ); }

protected:
    void done( int result ) override
    {
        if ( isVisible() && HasChanges() ) {
            const QMessageBox::StandardButton choice =
                QMessageBox::question( this, windowTitle(), QStringLiteral( "Save the changes to this theme?" ),
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
            if ( choice == QMessageBox::Cancel ) { return; }
            if ( choice == QMessageBox::Save && Save( m_pId->text().trimmed(), m_pName->text().trimmed() ).isEmpty() ) { return; }
        }
        m_pPreviewTimer->stop();
        EditorThemeDraft_RestorePrevious( &m_draft );
        QDialog::done( result );
    }

private:
    bool Matches( const theme_token_t &token, const QString &query ) const
    {
        if ( query.isEmpty() ) { return true; }
        // "#3a" finds every colour token whose value starts with it.
        if ( query.startsWith( QLatin1Char( '#' ) ) ) {
            return token.kind == theme_token_kind_t::COLOR &&
                   EditorThemeDraft_ColorText( EditorThemeDraft_Value( &m_draft, token ).rgba ).startsWith( query, Qt::CaseInsensitive );
        }
        const QByteArray utf8 = query.toUtf8();
        const auto matches = [&utf8]( const char *pText ) {
            return pText != nullptr && EditorFuzzy_Score( StringView_FromCString( pText ), ViewOf( utf8 ) ) != EDITOR_FUZZY_NO_MATCH;
        };
        return matches( token.pId ) || matches( token.pLabel ) || matches( token.pGroup );
    }

    void FillTree()
    {
        const QString current = m_currentId;
        const QSignalBlocker blocker( m_pTree );
        m_pTree->clear();
        const QString query = m_pSearch->text().trimmed();
        QHash<QString, QTreeWidgetItem *> groups;
        QTreeWidgetItem *pCurrent = nullptr;
        for ( const theme_token_t *pToken : m_draft.order ) {
            if ( !Matches( *pToken, query ) ) { continue; }
            // "Viewport/Grid" nests under "Viewport".
            QTreeWidgetItem *pParent = nullptr;
            QString path;
            const QString group = QString::fromUtf8( pToken->pGroup != nullptr ? pToken->pGroup : "Other" );
            for ( const QString &part : group.split( QLatin1Char( '/' ), Qt::SkipEmptyParts ) ) {
                path = path.isEmpty() ? part : path + QLatin1Char( '/' ) + part;
                QTreeWidgetItem *pGroup = groups.value( path, nullptr );
                if ( pGroup == nullptr ) {
                    pGroup = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( m_pTree );
                    pGroup->setText( 0, part );
                    pGroup->setFlags( Qt::ItemIsEnabled );
                    groups.insert( path, pGroup );
                    pGroup->setFirstColumnSpanned( true );
                }
                pParent = pGroup;
            }
            auto *pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( m_pTree );
            const QString id = QString::fromUtf8( pToken->pId );
            pItem->setText( 0, QString::fromUtf8( pToken->pLabel != nullptr ? pToken->pLabel : pToken->pId ) );
            pItem->setToolTip( 0, id );
            pItem->setData( 0, kTokenRole, id );
            if ( id == current ) { pCurrent = pItem; }
        }
        m_pTree->expandAll();
        if ( pCurrent != nullptr ) { m_pTree->setCurrentItem( pCurrent ); }
        RefreshValues();
    }

    void RefreshValues()
    {
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            const QString id = ( *it )->data( 0, kTokenRole ).toString();
            const theme_token_t *pToken = id.isEmpty() ? nullptr : EditorThemeDraft_Token( &m_draft, id );
            if ( pToken == nullptr ) { continue; }
            const theme_value_t value = EditorThemeDraft_Value( &m_draft, *pToken );
            const bool bChanged = EditorThemeDraft_IsChanged( &m_draft, *pToken );
            ( *it )->setText( 1, ValueText( *pToken, value ) );
            ( *it )->setIcon( 1, pToken->kind == theme_token_kind_t::COLOR ? Swatch( value.rgba ) : QIcon() );
            QString state = value.bFormula ? QStringLiteral( "Formula" ) : QString();
            if ( bChanged ) { state = state.isEmpty() ? QStringLiteral( "Changed" ) : state + QStringLiteral( ", changed" ); }
            ( *it )->setText( 2, state );
            ( *it )->setToolTip( 2, bChanged ? QStringLiteral( "Was %1" ).arg( ValueText( *pToken, EditorThemeDraft_Value( &m_draft, *pToken, true ) ) ) : QString() );
            QFont font = ( *it )->font( 0 );
            font.setBold( bChanged );
            ( *it )->setFont( 0, font );
        }
        ShowEditor( m_pTree->currentItem() );
    }

    QWidget *BuildEditorPane( QWidget *pParent )
    {
        auto *pPane = new QWidget( pParent );
        auto *pLayout = new QVBoxLayout( pPane );
        m_pTitle = new QLabel( pPane );
        QFont titleFont = m_pTitle->font();
        titleFont.setBold( true );
        m_pTitle->setFont( titleFont );
        m_pTokenId = new QLabel( pPane );
        m_pTokenId->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_pEditors = new QStackedWidget( pPane );
        m_pRevertToken = new QPushButton( QStringLiteral( "Revert Token" ), pPane );
        m_pRevertToken->setToolTip( QStringLiteral( "Back to the value this token had when the editor opened." ) );
        pLayout->addWidget( m_pTitle );
        pLayout->addWidget( m_pTokenId );
        pLayout->addWidget( m_pEditors );
        pLayout->addWidget( m_pRevertToken, 0, Qt::AlignLeft );
        pLayout->addStretch( 1 );
        QObject::connect( m_pRevertToken, &QPushButton::clicked, this, [this]() {
            if ( const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, m_currentId ) ) {
                EditorThemeDraft_RevertToken( &m_draft, *pToken );
                RefreshValues();
            }
        } );

        auto *pNone = new QLabel( QStringLiteral( "Select a token to edit it." ), m_pEditors );
        pNone->setAlignment( Qt::AlignCenter );
        m_pEditors->insertWidget( PAGE_NONE, pNone );
        m_pEditors->insertWidget( PAGE_COLOR, BuildColorPage() );
        m_pEditors->insertWidget( PAGE_FONT, BuildFontPage() );
        m_pEditors->insertWidget( PAGE_METRIC, BuildMetricPage() );
        m_pEditors->insertWidget( PAGE_CHOICE, BuildChoicePage() );
        return pPane;
    }

    QWidget *BuildColorPage()
    {
        auto *pPage = new QWidget( m_pEditors );
        auto *pLayout = new QFormLayout( pPage );
        m_pColorPick = new QPushButton( QStringLiteral( "Pick..." ), pPage );
        m_pColorPick->setIconSize( kPickSwatch );
        m_pColorHex = new QLineEdit( pPage );
        m_pColorHex->setPlaceholderText( QStringLiteral( "#rrggbb or #rrggbbaa" ) );
        m_pFormula = new QLabel( pPage );
        m_pFormula->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_pColorAuto = new QPushButton( QStringLiteral( "Follow Formula" ), pPage );
        m_pColorAuto->setToolTip( QStringLiteral( "Store \"auto\": the colour follows the tokens its formula names." ) );
        pLayout->addRow( QStringLiteral( "Colour" ), m_pColorPick );
        pLayout->addRow( QStringLiteral( "Hex" ), m_pColorHex );
        pLayout->addRow( QStringLiteral( "Formula" ), m_pFormula );
        pLayout->addRow( QString(), m_pColorAuto );
        QObject::connect( m_pColorPick, &QPushButton::clicked, this, [this]() { PickColor(); } );
        QObject::connect( m_pColorHex, &QLineEdit::editingFinished, this, [this]() {
            if ( m_bRefreshing ) { return; }
            u32 rgba = 0u;
            const QByteArray utf8 = m_pColorHex->text().trimmed().toUtf8();
            const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, m_currentId );
            if ( pToken == nullptr || !SettingColor_Parse( ViewOf( utf8 ), &rgba ) ) {
                ShowEditor( m_pTree->currentItem() ); // Put the valid value back.
                return;
            }
            const theme_value_t current = EditorThemeDraft_Value( &m_draft, *pToken );
            if ( !current.bFormula && current.rgba == rgba ) { return; } // Focus left without an edit.
            SetColor( m_currentId, EditorThemeDraft_ToColor( rgba ) );
        } );
        QObject::connect( m_pColorAuto, &QPushButton::clicked, this, [this]() { SetAuto( m_currentId ); } );
        return pPage;
    }

    // Live: every colour the dialog shows is previewed through the whole
    // editor; cancelling puts the token's earlier value back.
    void PickColor()
    {
        const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, m_currentId );
        if ( pToken == nullptr ) { return; }
        const theme_value_t before = EditorThemeDraft_Value( &m_draft, *pToken );
        QColorDialog dialog( EditorThemeDraft_ToColor( before.rgba ), this );
        dialog.setOptions( QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog );
        dialog.setWindowTitle( QString::fromUtf8( pToken->pLabel != nullptr ? pToken->pLabel : pToken->pId ) );
        QObject::connect( &dialog, &QColorDialog::currentColorChanged, this, [this, pToken]( const QColor &color ) {
            EditorThemeDraft_SetColor( &m_draft, *pToken, color, false );
            m_pPreviewTimer->start();
        } );
        if ( dialog.exec() == QDialog::Accepted ) {
            SetColor( m_currentId, dialog.selectedColor() );
        } else {
            EditorThemeDraft_Write( &m_draft, *pToken, before );
            RefreshValues();
        }
    }

    QWidget *BuildFontPage()
    {
        auto *pPage = new QWidget( m_pEditors );
        auto *pLayout = new QFormLayout( pPage );
        m_pFontFamily = new QComboBox( pPage );
        m_pFontFamily->setEditable( true );
        m_pFontFamily->setInsertPolicy( QComboBox::NoInsert );
        m_pFontFamily->addItems( { QString::fromLatin1( EDITOR_STYLE_FONT_SYSTEM ), QString::fromLatin1( EDITOR_STYLE_FONT_SYSTEM_MONO ) } );
        m_pFontFamily->insertSeparator( m_pFontFamily->count() );
        m_pFontFamily->addItems( QFontDatabase::families() );
        m_pFontFamily->setToolTip( QStringLiteral( "\"system\" and \"system-mono\" follow the platform; a .cyfont path loads a bundled font." ) );
        m_pFontSize = new QDoubleSpinBox( pPage );
        m_pFontSize->setRange( 1.0, 200.0 );
        m_pFontSize->setDecimals( 1 );
        m_pFontSize->setSuffix( QStringLiteral( " pt" ) );
        m_pFontSize->setKeyboardTracking( false );
        m_pFontWeight = new QSpinBox( pPage );
        m_pFontWeight->setRange( 1, 1000 );
        m_pFontWeight->setSingleStep( 100 );
        m_pFontWeight->setKeyboardTracking( false );
        pLayout->addRow( QStringLiteral( "Family" ), m_pFontFamily );
        pLayout->addRow( QStringLiteral( "Size" ), m_pFontSize );
        pLayout->addRow( QStringLiteral( "Weight" ), m_pFontWeight );
        const auto commit = [this]() {
            const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, m_currentId );
            if ( m_bRefreshing || pToken == nullptr || pToken->kind != theme_token_kind_t::FONT ) { return; }
            const QByteArray family = m_pFontFamily->currentText().trimmed().toUtf8();
            if ( family.isEmpty() ) { return; }
            theme_value_t value{};
            value.font = theme_font_t{ ViewOf( family ), m_pFontSize->value(), static_cast<u32>( m_pFontWeight->value() ) };
            const theme_value_t current = EditorThemeDraft_Value( &m_draft, *pToken );
            if ( StringView_Equals( current.font.family, value.font.family ) && current.font.flSize == value.font.flSize &&
                 current.font.nWeight == value.font.nWeight ) {
                return;
            }
            EditorThemeDraft_Write( &m_draft, *pToken, value );
            RefreshValues();
        };
        // Not currentTextChanged: typing a family name would restyle the
        // whole editor on every keystroke.
        QObject::connect( m_pFontFamily, &QComboBox::textActivated, this, commit );
        QObject::connect( m_pFontFamily->lineEdit(), &QLineEdit::editingFinished, this, commit );
        QObject::connect( m_pFontSize, &QDoubleSpinBox::valueChanged, this, commit );
        QObject::connect( m_pFontWeight, &QSpinBox::valueChanged, this, commit );
        return pPage;
    }

    QWidget *BuildMetricPage()
    {
        auto *pPage = new QWidget( m_pEditors );
        auto *pLayout = new QFormLayout( pPage );
        m_pMetric = new QDoubleSpinBox( pPage );
        m_pMetric->setDecimals( 2 );
        m_pMetric->setKeyboardTracking( false );
        m_pMetricRange = new QLabel( pPage );
        pLayout->addRow( QStringLiteral( "Value" ), m_pMetric );
        pLayout->addRow( QStringLiteral( "Range" ), m_pMetricRange );
        QObject::connect( m_pMetric, &QDoubleSpinBox::valueChanged, this, [this]( double flValue ) {
            const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, m_currentId );
            if ( m_bRefreshing || pToken == nullptr || pToken->kind != theme_token_kind_t::METRIC ) { return; }
            theme_value_t value{};
            value.flMetric = flValue;
            EditorThemeDraft_Write( &m_draft, *pToken, value );
            RefreshValues();
        } );
        return pPage;
    }

    QWidget *BuildChoicePage()
    {
        auto *pPage = new QWidget( m_pEditors );
        auto *pLayout = new QFormLayout( pPage );
        m_pChoice = new QComboBox( pPage );
        pLayout->addRow( QStringLiteral( "Value" ), m_pChoice );
        QObject::connect( m_pChoice, &QComboBox::currentTextChanged, this, [this]( const QString &text ) {
            const theme_token_t *pToken = EditorThemeDraft_Token( &m_draft, m_currentId );
            if ( m_bRefreshing || pToken == nullptr || pToken->kind != theme_token_kind_t::CHOICE ) { return; }
            const QByteArray utf8 = text.toUtf8();
            theme_value_t value{};
            value.choice = ViewOf( utf8 );
            EditorThemeDraft_Write( &m_draft, *pToken, value );
            RefreshValues();
        } );
        return pPage;
    }

    void ShowEditor( QTreeWidgetItem *pItem )
    {
        const QString id = pItem != nullptr ? pItem->data( 0, kTokenRole ).toString() : QString();
        const theme_token_t *pToken = id.isEmpty() ? nullptr : EditorThemeDraft_Token( &m_draft, id );
        m_currentId = pToken != nullptr ? id : QString();
        m_pTitle->setVisible( pToken != nullptr );
        m_pTokenId->setVisible( pToken != nullptr );
        m_pRevertToken->setVisible( pToken != nullptr );
        if ( pToken == nullptr ) {
            m_pEditors->setCurrentIndex( PAGE_NONE );
            return;
        }
        m_bRefreshing = true;
        const theme_value_t value = EditorThemeDraft_Value( &m_draft, *pToken );
        m_pTitle->setText( QString::fromUtf8( pToken->pLabel != nullptr ? pToken->pLabel : pToken->pId ) );
        m_pTokenId->setText( id );
        m_pRevertToken->setEnabled( EditorThemeDraft_IsChanged( &m_draft, *pToken ) );
        switch ( pToken->kind ) {
            case theme_token_kind_t::COLOR: {
                const bool bDerived = pToken->derive.op != theme_derive_op_t::NONE;
                m_pColorPick->setIcon( Swatch( value.rgba, kPickSwatch ) );
                m_pColorHex->setText( EditorThemeDraft_ColorText( value.rgba ) );
                m_pFormula->setText( bDerived ? EditorThemeDraft_FormulaText( pToken->derive ) : QStringLiteral( "None (a base colour)" ) );
                m_pColorAuto->setEnabled( bDerived && !value.bFormula );
                m_pEditors->setCurrentIndex( PAGE_COLOR );
                break;
            }
            case theme_token_kind_t::FONT:
                m_pFontFamily->setCurrentText( FromView( value.font.family ) );
                m_pFontSize->setValue( value.font.flSize );
                m_pFontWeight->setValue( static_cast<int>( value.font.nWeight ) );
                m_pEditors->setCurrentIndex( PAGE_FONT );
                break;
            case theme_token_kind_t::METRIC:
                m_pMetric->setRange( pToken->flMin, pToken->flMax );
                m_pMetric->setValue( value.flMetric );
                m_pMetricRange->setText( QStringLiteral( "%1 to %2" ).arg( pToken->flMin ).arg( pToken->flMax ) );
                m_pEditors->setCurrentIndex( PAGE_METRIC );
                break;
            case theme_token_kind_t::CHOICE:
                m_pChoice->clear();
                for ( usize i = 0u; i < pToken->nChoices; ++i ) { m_pChoice->addItem( QString::fromUtf8( pToken->ppChoices[i] ) ); }
                m_pChoice->setCurrentText( FromView( value.choice ) );
                m_pEditors->setCurrentIndex( PAGE_CHOICE );
                break;
        }
        m_bRefreshing = false;
    }

    editor_gui_t *m_pGui{ nullptr };
    QString m_folder{};
    QString m_currentId{};
    editor_theme_draft_t m_draft{};
    QLineEdit *m_pId{ nullptr };
    QLineEdit *m_pName{ nullptr };
    QLineEdit *m_pAuthor{ nullptr };
    QLineEdit *m_pDescription{ nullptr };
    QLineEdit *m_pSearch{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QLabel *m_pTitle{ nullptr };
    QLabel *m_pTokenId{ nullptr };
    QStackedWidget *m_pEditors{ nullptr };
    QPushButton *m_pRevertToken{ nullptr };
    QPushButton *m_pColorPick{ nullptr };
    QLineEdit *m_pColorHex{ nullptr };
    QLabel *m_pFormula{ nullptr };
    QPushButton *m_pColorAuto{ nullptr };
    QComboBox *m_pFontFamily{ nullptr };
    QDoubleSpinBox *m_pFontSize{ nullptr };
    QSpinBox *m_pFontWeight{ nullptr };
    QDoubleSpinBox *m_pMetric{ nullptr };
    QLabel *m_pMetricRange{ nullptr };
    QComboBox *m_pChoice{ nullptr };
    QTimer *m_pPreviewTimer{ nullptr };
    bool m_bRefreshing{ false };
};

theme_editor_t *AsEditor( QDialog *pDialog )
{
    auto *pImpl = dynamic_cast<theme_editor_t *>( pDialog );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QDialog *EditorThemeEditor_Create( QWidget *pParent, editor_gui_t *pGui, QApplication *pApplication, const QString &userThemeFolder )
{
    CY_ASSERT( pGui != nullptr && pGui->bInitialized && pApplication != nullptr );
    if ( pGui == nullptr || !pGui->bInitialized || pApplication == nullptr ) { return nullptr; }
    return new theme_editor_t( pParent, pGui, pApplication, userThemeFolder );
}

void EditorThemeEditor_SetSearch( QDialog *pEditor, const QString &text )
{
    if ( theme_editor_t *pImpl = AsEditor( pEditor ) ) { pImpl->SetSearch( text ); }
}

QStringList EditorThemeEditor_VisibleTokens( QDialog *pEditor )
{
    theme_editor_t *pImpl = AsEditor( pEditor );
    return pImpl != nullptr ? pImpl->VisibleTokens() : QStringList{};
}

void EditorThemeEditor_SetColor( QDialog *pEditor, const QString &tokenId, const QColor &color )
{
    if ( theme_editor_t *pImpl = AsEditor( pEditor ) ) { pImpl->SetColor( tokenId, color ); }
}

void EditorThemeEditor_SetAuto( QDialog *pEditor, const QString &tokenId )
{
    if ( theme_editor_t *pImpl = AsEditor( pEditor ) ) { pImpl->SetAuto( tokenId ); }
}

void EditorThemeEditor_Revert( QDialog *pEditor )
{
    if ( theme_editor_t *pImpl = AsEditor( pEditor ) ) { pImpl->Revert(); }
}

bool EditorThemeEditor_HasChanges( QDialog *pEditor )
{
    theme_editor_t *pImpl = AsEditor( pEditor );
    return pImpl != nullptr && pImpl->HasChanges();
}

QString EditorThemeEditor_Save( QDialog *pEditor, const QString &id, const QString &name )
{
    theme_editor_t *pImpl = AsEditor( pEditor );
    return pImpl != nullptr ? pImpl->Save( id, name ) : QString();
}

} // namespace cypher::editor::gui
