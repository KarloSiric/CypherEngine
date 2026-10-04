//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_KeymapSettings.cpp
//  Purpose: Implements Settings > Keybindings with an owned working copy,
//           contextual input browsing and validation before publication.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_KeymapSettings.h"

#include "CypherEditorGui_Actions.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

QString Text( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

string_view_t View( const QByteArray &bytes ) noexcept { return { bytes.constData(), static_cast<usize>( bytes.size() ) }; }

const char *SectionName( keymap_section_t section )
{
    switch ( section ) {
        case keymap_section_t::HELD: return "held";
        case keymap_section_t::MOUSE: return "mouse";
        default: return "bindings";
    }
}

QString SectionLabel( keymap_section_t section )
{
    switch ( section ) {
        case keymap_section_t::HELD: return QStringLiteral( "Held key" );
        case keymap_section_t::MOUSE: return QStringLiteral( "Mouse" );
        default: return QStringLiteral( "Keyboard" );
    }
}

const char *PlatformName( keymap_platform_t platform )
{
    switch ( platform ) {
        case keymap_platform_t::MACOS: return "macos";
        case keymap_platform_t::WINDOWS: return "windows";
        case keymap_platform_t::LINUX: return "linux";
        default: return "";
    }
}

const key_value_t *Section( const key_value_t *pRoot, keymap_section_t section, keymap_platform_t platform )
{
    if ( platform != keymap_platform_t::NONE ) {
        pRoot = KeyValue_Find( KeyValue_Find( pRoot, StringView_FromCString( "platforms" ) ), StringView_FromCString( PlatformName( platform ) ) );
    }
    return KeyValue_Find( pRoot, StringView_FromCString( SectionName( section ) ) );
}

const key_value_t *Declaration( const key_value_t *pRoot, keymap_section_t section, keymap_platform_t platform,
                               const QString &context, const QString &id )
{
    const QByteArray contextBytes = context.toUtf8(), idBytes = id.toUtf8();
    return KeyValue_Find( KeyValue_Find( Section( pRoot, section, platform ), View( contextBytes ) ), View( idBytes ) );
}

QString Category( const QString &id, const QString &context )
{
    if ( id.startsWith( QStringLiteral( "map.camera." ) ) ) { return QStringLiteral( "Camera" ); }
    if ( id.startsWith( QStringLiteral( "map.view." ) ) || id.startsWith( QStringLiteral( "map.render." ) ) ||
         id.startsWith( QStringLiteral( "map.grid." ) ) || id == QStringLiteral( "map.context_menu" ) ||
         id == QStringLiteral( "map.go_to" ) ) { return QStringLiteral( "Viewports" ); }
    if ( id.startsWith( QStringLiteral( "map.select" ) ) || id.startsWith( QStringLiteral( "edit.select" ) ) ||
         id == QStringLiteral( "edit.invert_selection" ) ) { return QStringLiteral( "Selection" ); }
    if ( id.startsWith( QStringLiteral( "map.transform." ) ) || id.startsWith( QStringLiteral( "map.nudge" ) ) ||
         id == QStringLiteral( "map.tool.translate" ) || id == QStringLiteral( "map.tool.rotate" ) ||
         id == QStringLiteral( "map.tool.scale" ) ) { return QStringLiteral( "Transforms" ); }
    if ( id.startsWith( QStringLiteral( "map.mesh." ) ) || id.startsWith( QStringLiteral( "map.vertex." ) ) ) { return QStringLiteral( "Meshes" ); }
    if ( id.startsWith( QStringLiteral( "map.texture." ) ) || context == QStringLiteral( "map.tool.texture" ) ) { return QStringLiteral( "Materials" ); }
    if ( id.startsWith( QStringLiteral( "map.brush." ) ) || id.startsWith( QStringLiteral( "map.clip." ) ) ||
         id.startsWith( QStringLiteral( "map.tool." ) ) || id.startsWith( QStringLiteral( "map.block." ) ) ) { return QStringLiteral( "Geometry tools" ); }
    if ( id.startsWith( QStringLiteral( "map.entity." ) ) ) { return QStringLiteral( "Objects and entities" ); }
    if ( id.startsWith( QStringLiteral( "map.group." ) ) || id.startsWith( QStringLiteral( "map.layer." ) ) ||
         id.startsWith( QStringLiteral( "map.hide." ) ) || id.startsWith( QStringLiteral( "map.visgroup." ) ) ) { return QStringLiteral( "Organisation" ); }
    if ( id.startsWith( QStringLiteral( "file." ) ) ) { return QStringLiteral( "Files" ); }
    if ( id.startsWith( QStringLiteral( "edit." ) ) ) { return QStringLiteral( "Editing" ); }
    if ( context == QStringLiteral( "console" ) || context == QStringLiteral( "outliner" ) || context == QStringLiteral( "properties" ) ||
         id.startsWith( QStringLiteral( "view." ) ) ) { return QStringLiteral( "Panels" ); }
    return QStringLiteral( "Other commands" );
}

QString Humanize( QString id )
{
    id.replace( QLatin1Char( '.' ), QLatin1Char( ' ' ) ).replace( QLatin1Char( '_' ), QLatin1Char( ' ' ) );
    if ( id.startsWith( QStringLiteral( "map " ) ) ) { id.remove( 0, 4 ); }
    if ( !id.isEmpty() ) { id[0] = id[0].toUpper(); }
    return id;
}

bool Serialize( const settings_document_t &document, QByteArray &out )
{
    text_buffer_t buffer{};
    if ( !TextBuffer_Init( &buffer, document.pAllocator ) || SettingsDocument_Write( &document, &buffer ) != settings_document_status_t::OK ) {
        return false;
    }
    out = QByteArray( TextBuffer_CStr( &buffer ), static_cast<qsizetype>( TextBuffer_Length( &buffer ) ) );
    return true;
}

std::unique_ptr<settings_document_t> LoadDocument( const allocator_t *pAllocator, const QByteArray &text )
{
    auto document = std::make_unique<settings_document_t>();
    if ( SettingsDocument_Init( document.get(), pAllocator, EditorKeymap_Identity() ) != settings_document_status_t::OK ||
         SettingsDocument_Load( document.get(), View( text ) ).status != settings_document_status_t::OK ) { return {}; }
    const auto header = EditorKeymap_Header( SettingsDocument_Root( document.get() ) );
    if ( header.id.cchLength == 0u || header.name.cchLength == 0u ) { return {}; }
    return document;
}

QString Canonical( keymap_section_t section, const QString &input )
{
    const QByteArray bytes = input.trimmed().toUtf8();
    char keyText[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    if ( section == keymap_section_t::BINDINGS ) {
        key_chord_t chord{};
        if ( !EditorKeyChord_Parse( View( bytes ), &chord ) ) { return {}; }
        return QString::fromUtf8( keyText, static_cast<qsizetype>( EditorKeyChord_Format( chord, keyText ) ) );
    }
    if ( section == keymap_section_t::HELD ) {
        key_stroke_t stroke{};
        if ( !EditorHeldKey_Parse( View( bytes ), &stroke ) ) { return {}; }
        return QString::fromUtf8( keyText, static_cast<qsizetype>( EditorHeldKey_Format( stroke, keyText ) ) );
    }
    mouse_gesture_t gesture{};
    char mouseText[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY]{};
    if ( !EditorMouseGesture_Parse( View( bytes ), &gesture ) ) { return {}; }
    return QString::fromUtf8( mouseText, static_cast<qsizetype>( EditorMouseGesture_Format( gesture, mouseText ) ) );
}

bool ValidateDeclarations( const key_value_t *pRoot, QString &error )
{
    for ( const auto platform : { keymap_platform_t::NONE, keymap_platform_t::MACOS, keymap_platform_t::WINDOWS, keymap_platform_t::LINUX } ) {
        for ( const auto section : { keymap_section_t::BINDINGS, keymap_section_t::HELD, keymap_section_t::MOUSE } ) {
            const key_value_t *pContexts = Section( pRoot, section, platform );
            if ( pContexts != nullptr && KeyValue_Type( pContexts ) != key_value_type_t::OBJECT ) {
                error = QStringLiteral( "The %1 section must be an object." ).arg( QString::fromLatin1( SectionName( section ) ) ); return false;
            }
            for ( usize c = 0u; pContexts != nullptr && c < KeyValue_ChildCount( pContexts ); ++c ) {
                const key_value_t *pContext = KeyValue_ChildAt( pContexts, c );
                if ( KeyValue_Type( pContext ) != key_value_type_t::OBJECT ) {
                    error = QStringLiteral( "Context %1 must be an object." ).arg( Text( KeyValue_Name( pContext ) ) ); return false;
                }
                for ( usize a = 0u; a < KeyValue_ChildCount( pContext ); ++a ) {
                    const key_value_t *pAction = KeyValue_ChildAt( pContext, a );
                    if ( KeyValue_Type( pAction ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pAction ) > EDITOR_KEYMAP_MAX_CHORDS ) {
                        error = QStringLiteral( "%1 needs an array of at most four triggers." ).arg( Text( KeyValue_Name( pAction ) ) ); return false;
                    }
                    for ( usize t = 0u; t < KeyValue_ChildCount( pAction ); ++t ) {
                        const key_value_t *pTrigger = KeyValue_ChildAt( pAction, t );
                        string_view_t triggerText{};
                        if ( !KeyValue_GetString( pTrigger, &triggerText ) || Canonical( section, Text( triggerText ) ).isEmpty() ) {
                            error = QStringLiteral( "%1 contains an invalid %2 trigger." ).arg( Text( KeyValue_Name( pAction ) ), SectionLabel( section ) ); return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

struct binding_row_t {
    keymap_section_t section{ keymap_section_t::BINDINGS };
    QString context, id, category, label, description, origin, availability;
    QStringList triggers;
    QString Key() const { return QString::number( static_cast<int>( section ) ) + QLatin1Char( '\t' ) + context + QLatin1Char( '\t' ) + id; }
};

bool ChordPrefix( const QString &a, const QString &b )
{
    key_chord_t first{}, second{};
    const QByteArray aa = a.toUtf8(), bb = b.toUtf8();
    if ( !EditorKeyChord_Parse( View( aa ), &first ) || !EditorKeyChord_Parse( View( bb ), &second ) ) { return false; }
    for ( usize i = 0u; i < std::min( first.nStrokes, second.nStrokes ); ++i ) {
        if ( first.strokes[i].key != second.strokes[i].key || first.strokes[i].modifiers != second.strokes[i].modifiers ) { return false; }
    }
    return true;
}

bool TriggerCollision( keymap_section_t section, const QString &a, const QString &b )
{
    if ( section == keymap_section_t::BINDINGS ) { return ChordPrefix( a, b ); }
    if ( a == b ) { return true; }
    if ( section != keymap_section_t::MOUSE ) { return false; }
    mouse_gesture_t first{}, second{};
    const QByteArray aa = a.toUtf8(), bb = b.toUtf8();
    if ( !EditorMouseGesture_Parse( View( aa ), &first ) || !EditorMouseGesture_Parse( View( bb ), &second ) ) { return false; }
    const bool sameModifiers = first.modifiers == second.modifiers && first.heldKey == second.heldKey && first.button == second.button;
    const auto wheel = []( u8 action ) { return action >= MOUSE_ACTION_WHEEL && action <= MOUSE_ACTION_WHEEL_RIGHT; };
    return sameModifiers && wheel( first.action ) && wheel( second.action ) &&
           ( first.action == MOUSE_ACTION_WHEEL || second.action == MOUSE_ACTION_WHEEL );
}

// Selection on empty space and transformation on existing geometry are
// deliberately dispatched by the same LeftDrag route in the viewport.
bool SharedSelectionDrag( const binding_row_t &a, const binding_row_t &b )
{
    return a.section == keymap_section_t::MOUSE &&
           ( ( a.id == QStringLiteral( "map.select.box" ) && b.id == QStringLiteral( "map.transform.drag" ) ) ||
             ( b.id == QStringLiteral( "map.select.box" ) && a.id == QStringLiteral( "map.transform.drag" ) ) );
}

class held_capture_t final : public QLineEdit {
public:
    explicit held_capture_t( QWidget *pParent ) : QLineEdit( pParent )
    {
        setObjectName( QStringLiteral( "KeymapHeldCapture" ) );
        setPlaceholderText( QStringLiteral( "Press a key; release a modifier to bind it alone" ) );
    }
protected:
    bool event( QEvent *pEvent ) override
    {
        if ( pEvent->type() == QEvent::ShortcutOverride ) { pEvent->accept(); return true; }
        if ( pEvent->type() == QEvent::KeyPress && ( static_cast<QKeyEvent *>( pEvent )->key() == Qt::Key_Tab ||
                                                  static_cast<QKeyEvent *>( pEvent )->key() == Qt::Key_Backtab ) ) {
            keyPressEvent( static_cast<QKeyEvent *>( pEvent ) ); return true;
        }
        return QLineEdit::event( pEvent );
    }
    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->isAutoRepeat() ) { pEvent->accept(); return; }
        const bool modifierKey = pEvent->key() == Qt::Key_Control || pEvent->key() == Qt::Key_Alt ||
                                 pEvent->key() == Qt::Key_Shift || pEvent->key() == Qt::Key_Meta;
        if ( modifierKey && m_normalKeysDown.isEmpty() ) { m_bCaptureComplete = false; }
        key_chord_t chord{};
        if ( EditorKeyChord_FromKeySequence( QKeySequence( pEvent->keyCombination() ), &chord ) && chord.nStrokes == 1u ) {
            char buffer[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
            setText( QString::fromUtf8( buffer, static_cast<qsizetype>( EditorHeldKey_Format( chord.strokes[0], buffer ) ) ) );
            m_normalKeysDown.insert( pEvent->key() ); m_bCaptureComplete = true;
        }
        pEvent->accept();
    }
    void keyReleaseEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->isAutoRepeat() ) { pEvent->accept(); return; }
        m_normalKeysDown.remove( pEvent->key() );
        if ( !m_bCaptureComplete ) {
            key_stroke_t stroke{};
            u8 ownModifier = KEY_MODIFIER_NONE;
            switch ( pEvent->key() ) {
                case Qt::Key_Control: stroke.key = KEY_MODIFIER_KEY_CTRL; ownModifier = KEY_MODIFIER_CTRL; break;
                case Qt::Key_Alt: stroke.key = KEY_MODIFIER_KEY_ALT; ownModifier = KEY_MODIFIER_ALT; break;
                case Qt::Key_Shift: stroke.key = KEY_MODIFIER_KEY_SHIFT; ownModifier = KEY_MODIFIER_SHIFT; break;
                case Qt::Key_Meta: stroke.key = KEY_MODIFIER_KEY_META; ownModifier = KEY_MODIFIER_META; break;
                default: break;
            }
            if ( pEvent->modifiers().testFlag( Qt::ControlModifier ) ) { stroke.modifiers |= KEY_MODIFIER_CTRL; }
            if ( pEvent->modifiers().testFlag( Qt::AltModifier ) ) { stroke.modifiers |= KEY_MODIFIER_ALT; }
            if ( pEvent->modifiers().testFlag( Qt::ShiftModifier ) ) { stroke.modifiers |= KEY_MODIFIER_SHIFT; }
            if ( pEvent->modifiers().testFlag( Qt::MetaModifier ) ) { stroke.modifiers |= KEY_MODIFIER_META; }
            stroke.modifiers &= static_cast<u8>( ~ownModifier );
            char buffer[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
            const usize count = EditorHeldKey_Format( stroke, buffer );
            if ( count != 0u ) { setText( QString::fromUtf8( buffer, static_cast<qsizetype>( count ) ) ); m_bCaptureComplete = true; }
        }
        pEvent->accept();
    }
private:
    QSet<int> m_normalKeysDown;
    bool m_bCaptureComplete{ false };
};

class keymap_settings_t final : public QWidget {
public:
    keymap_settings_t( QWidget *pParent, editor_gui_t *pGui, QString folder ) : QWidget( pParent ), m_pGui( pGui ), m_folder( std::move( folder ) )
    {
        setObjectName( QStringLiteral( "EditorKeymapSettings" ) );
        auto *pRoot = new QVBoxLayout( this );
        auto *pTop = new QHBoxLayout();
        pTop->addWidget( new QLabel( QStringLiteral( "Profile" ), this ) );
        m_pPresets = new QComboBox( this ); m_pPresets->setObjectName( QStringLiteral( "KeymapPreset" ) );
        m_pPresets->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
        pTop->addWidget( m_pPresets, 1 );
        auto *pNew = new QPushButton( QStringLiteral( "New Profile…" ), this ); pNew->setObjectName( QStringLiteral( "KeymapNewProfile" ) );
        auto *pDuplicate = new QPushButton( QStringLiteral( "Duplicate Profile…" ), this ); pDuplicate->setObjectName( QStringLiteral( "KeymapDuplicateProfile" ) );
        pNew->setToolTip( QStringLiteral( "Name a new profile that inherits an existing profile's bindings." ) );
        pDuplicate->setToolTip( QStringLiteral( "Copy this working profile, including staged edits and metadata, under a new name." ) );
        pTop->addWidget( pNew ); pTop->addWidget( pDuplicate ); pRoot->addLayout( pTop );
        auto *pFiles = new QHBoxLayout();
        auto *pImport = new QPushButton( QStringLiteral( "Import profile…" ), this ); pImport->setObjectName( QStringLiteral( "KeymapImport" ) );
        auto *pExport = new QPushButton( QStringLiteral( "Export portable…" ), this ); pExport->setObjectName( QStringLiteral( "KeymapExportPortable" ) );
        auto *pExportSource = new QPushButton( QStringLiteral( "Export source…" ), this ); pExportSource->setObjectName( QStringLiteral( "KeymapExportSource" ) );
        pExport->setToolTip( QStringLiteral( "Resolve inherited bindings into one self-contained .cykeymap for all supported platforms." ) );
        pExportSource->setToolTip( QStringLiteral( "Keep the authored base dependency; the recipient must also install its base profiles." ) );
        m_pReference = new QPushButton( QStringLiteral( "Shortcuts reference" ), this );
        m_pReference->setObjectName( QStringLiteral( "KeymapShortcutsReference" ) ); m_pReference->setEnabled( false );
        pFiles->addWidget( pImport ); pFiles->addWidget( pExport ); pFiles->addWidget( pExportSource ); pFiles->addStretch();
        pFiles->addWidget( m_pReference ); pRoot->addLayout( pFiles );
        auto *pIdentity = new QHBoxLayout();
        m_pId = new QLineEdit( this ); m_pId->setObjectName( QStringLiteral( "KeymapId" ) ); m_pId->setPlaceholderText( QStringLiteral( "my_profile" ) );
        m_pName = new QLineEdit( this ); m_pName->setObjectName( QStringLiteral( "KeymapName" ) );
        pIdentity->addWidget( new QLabel( QStringLiteral( "Profile ID" ), this ) ); pIdentity->addWidget( m_pId, 1 );
        pIdentity->addWidget( new QLabel( QStringLiteral( "Profile name" ), this ) ); pIdentity->addWidget( m_pName, 2 ); pRoot->addLayout( pIdentity );
        m_pBase = new QLabel( this ); m_pBase->setProperty( "muted", true ); pRoot->addWidget( m_pBase );
        auto *pFilter = new QHBoxLayout();
        m_pSearch = new QLineEdit( this ); m_pSearch->setObjectName( QStringLiteral( "KeymapSearch" ) );
        m_pSearch->setClearButtonEnabled( true ); m_pSearch->setPlaceholderText( QStringLiteral( "Search actions, keys or contexts (e.g. camera or map.viewport.2d)" ) );
        m_pPlatform = new QComboBox( this ); m_pPlatform->setObjectName( QStringLiteral( "KeymapPlatform" ) );
        m_pPlatform->addItem( QStringLiteral( "Common · all platforms" ), static_cast<int>( keymap_platform_t::NONE ) );
        m_pPlatform->addItem( QStringLiteral( "macOS override" ), static_cast<int>( keymap_platform_t::MACOS ) );
        m_pPlatform->addItem( QStringLiteral( "Windows override" ), static_cast<int>( keymap_platform_t::WINDOWS ) );
        m_pPlatform->addItem( QStringLiteral( "Linux override" ), static_cast<int>( keymap_platform_t::LINUX ) );
        pFilter->addWidget( m_pSearch, 1 ); pFilter->addWidget( m_pPlatform ); pRoot->addLayout( pFilter );
        auto *pSplitter = new QSplitter( Qt::Horizontal, this );
        m_pCategories = new QTreeWidget( pSplitter ); m_pCategories->setObjectName( QStringLiteral( "KeymapCategories" ) );
        m_pCategories->setHeaderHidden( true ); m_pCategories->setMinimumWidth( 110 );
        for ( const char *pName : { "All actions", "Viewports", "Camera", "Selection", "Transforms", "Geometry tools", "Meshes", "Materials",
                                  "Objects and entities", "Organisation", "Files", "Editing", "Panels", "Other commands" } ) {
            auto *pItem = new QTreeWidgetItem( m_pCategories ); pItem->setText( 0, QString::fromLatin1( pName ) );
        }
        m_pCategories->setCurrentItem( m_pCategories->topLevelItem( 0 ) );
        m_pRows = new QTreeWidget( pSplitter ); m_pRows->setObjectName( QStringLiteral( "KeymapBindings" ) );
        m_pRows->setRootIsDecorated( false ); m_pRows->setAlternatingRowColors( true ); m_pRows->setUniformRowHeights( true );
        m_pRows->setHeaderLabels( { QStringLiteral( "Action" ), QStringLiteral( "Input" ), QStringLiteral( "Context" ), QStringLiteral( "Bindings" ),
                                  QStringLiteral( "Origin" ), QStringLiteral( "Availability" ) } );
        m_pRows->header()->setSectionResizeMode( QHeaderView::ResizeToContents );
        m_pRows->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pRows->setSelectionMode( QAbstractItemView::SingleSelection );
        pSplitter->setStretchFactor( 1, 1 ); pSplitter->setSizes( { 135, 635 } ); pRoot->addWidget( pSplitter, 1 );

        m_pDetails = new QLabel( this ); m_pDetails->setObjectName( QStringLiteral( "KeymapDetails" ) );
        m_pDetails->setWordWrap( true ); m_pDetails->setProperty( "muted", true ); pRoot->addWidget( m_pDetails );
        auto *pAddress = new QHBoxLayout();
        m_pContext = new QComboBox( this ); m_pContext->setEditable( true ); m_pContext->setObjectName( QStringLiteral( "KeymapContext" ) );
        m_pAction = new QComboBox( this ); m_pAction->setEditable( true ); m_pAction->setObjectName( QStringLiteral( "KeymapAction" ) );
        m_pKind = new QComboBox( this ); m_pKind->setObjectName( QStringLiteral( "KeymapInputKind" ) );
        for ( const auto section : { keymap_section_t::BINDINGS, keymap_section_t::HELD, keymap_section_t::MOUSE } ) {
            m_pKind->addItem( SectionLabel( section ), static_cast<int>( section ) );
        }
        pAddress->addWidget( new QLabel( QStringLiteral( "Context" ), this ) ); pAddress->addWidget( m_pContext, 2 );
        pAddress->addWidget( new QLabel( QStringLiteral( "Action ID" ), this ) ); pAddress->addWidget( m_pAction, 3 ); pAddress->addWidget( m_pKind );
        pRoot->addLayout( pAddress );
        auto *pTriggers = new QHBoxLayout();
        for ( int i = 0; i < 4; ++i ) {
            auto *pSlot = new QVBoxLayout();
            m_pTriggers[i] = new QLineEdit( this ); m_pTriggers[i]->setObjectName( QStringLiteral( "KeymapTrigger%1" ).arg( i ) );
            m_pTriggers[i]->setPlaceholderText( QStringLiteral( "Alternative %1" ).arg( i + 1 ) );
            m_pRecord[i] = new QPushButton( QStringLiteral( "Record" ), this ); m_pRecord[i]->setMaximumHeight( 23 );
            m_pRecord[i]->setObjectName( QStringLiteral( "KeymapRecord%1" ).arg( i ) );
            pSlot->addWidget( m_pTriggers[i] ); pSlot->addWidget( m_pRecord[i] ); pTriggers->addLayout( pSlot, 1 );
            QObject::connect( m_pRecord[i], &QPushButton::clicked, this, [this, i]() { Record( i ); } );
        }
        pRoot->addLayout( pTriggers );
        auto *pEdits = new QHBoxLayout();
        auto *pSet = new QPushButton( QStringLiteral( "Set binding" ), this ); pSet->setObjectName( QStringLiteral( "KeymapSetBinding" ) );
        auto *pUnbind = new QPushButton( QStringLiteral( "Unbind" ), this );
        auto *pReset = new QPushButton( QStringLiteral( "Reset to inherited" ), this );
        pEdits->addWidget( pSet ); pEdits->addWidget( pUnbind ); pEdits->addWidget( pReset ); pEdits->addStretch(); pRoot->addLayout( pEdits );
        auto *pHelp = new QLabel( QStringLiteral( "Keyboard: Ctrl+K, Ctrl+C · Held: W or Shift · Mouse: RightDrag, Alt+LeftDrag, Space+LeftDrag or Wheel. "
                                               "Up to four alternatives. Contexts declare input; the active tool and view determine routing. "
                                               "Editing does not enable unavailable commands. Geometry and 2D mouse declarations are not remapped yet." ), this );
        pHelp->setWordWrap( true ); pHelp->setProperty( "muted", true ); pRoot->addWidget( pHelp );
        auto *pBottom = new QHBoxLayout();
        m_pStatus = new QLabel( this ); m_pStatus->setWordWrap( true ); m_pStatus->setObjectName( QStringLiteral( "KeymapStatus" ) );
        pBottom->addWidget( m_pStatus, 1 );
        m_pRevert = new QPushButton( QStringLiteral( "Discard changes" ), this );
        m_pApply = new QPushButton( QStringLiteral( "Save and activate profile" ), this ); m_pApply->setObjectName( QStringLiteral( "KeymapApply" ) );
        pBottom->addWidget( m_pRevert ); pBottom->addWidget( m_pApply ); pRoot->addLayout( pBottom );

        QObject::connect( m_pSearch, &QLineEdit::textChanged, this, [this]() { FillRows(); } );
        QObject::connect( m_pPlatform, &QComboBox::currentIndexChanged, this, [this]() { Refresh(); } );
        QObject::connect( m_pCategories, &QTreeWidget::currentItemChanged, this, [this]() { FillRows(); } );
        QObject::connect( m_pRows, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem ) { SelectRow( pItem ); } );
        QObject::connect( m_pKind, &QComboBox::currentIndexChanged, this, [this]() { RefreshRecordButtons(); } );
        QObject::connect( m_pId, &QLineEdit::textChanged, this, [this]() { UpdateState(); } );
        QObject::connect( m_pName, &QLineEdit::textChanged, this, [this]() { UpdateState(); } );
        QObject::connect( m_pPresets, &QComboBox::currentIndexChanged, this, [this]() {
            if ( HasChanges() && !ConfirmDiscard() ) { FillPresets(); return; }
            ( void )Select( m_pPresets->currentData().toString() );
        } );
        QObject::connect( pSet, &QPushButton::clicked, this, [this]() {
            QStringList triggers; for ( const auto *pEdit : m_pTriggers ) { if ( !pEdit->text().trimmed().isEmpty() ) { triggers.append( pEdit->text() ); } }
            ( void )SetTriggers( Kind(), Platform(), m_pContext->currentText(), m_pAction->currentText(), triggers );
        } );
        QObject::connect( pUnbind, &QPushButton::clicked, this, [this]() {
            ( void )SetTriggers( Kind(), Platform(), m_pContext->currentText(), m_pAction->currentText(), {} );
        } );
        QObject::connect( pReset, &QPushButton::clicked, this, [this]() {
            ( void )Reset( Kind(), Platform(), m_pContext->currentText(), m_pAction->currentText() );
        } );
        QObject::connect( m_pRevert, &QPushButton::clicked, this, [this]() { Revert(); } );
        QObject::connect( m_pApply, &QPushButton::clicked, this, [this]() { ( void )Apply( {}, {} ); } );
        QObject::connect( pNew, &QPushButton::clicked, this, [this]() { ProfileDialog( false ); } );
        QObject::connect( pDuplicate, &QPushButton::clicked, this, [this]() { ProfileDialog( true ); } );
        QObject::connect( m_pReference, &QPushButton::clicked, this, [this]() { if ( m_pReferenceCallback != nullptr ) { m_pReferenceCallback( m_pReferenceContext ); } } );
        QObject::connect( pImport, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName( this, QStringLiteral( "Import keybinding profile" ), {}, QStringLiteral( "Cypher keymaps (*.cykeymap)" ) );
            if ( !path.isEmpty() && ( !HasChanges() || ConfirmDiscard() ) ) { ( void )Import( path ); }
        } );
        QObject::connect( pExport, &QPushButton::clicked, this, [this]() {
            ExportDialog( true );
        } );
        QObject::connect( pExportSource, &QPushButton::clicked, this, [this]() { ExportDialog( false ); } );
        m_subscribed = EditorGui_AddKeymapListener( m_pGui, OnKeymap, this );
        ( void )Select( Text( EditorGui_ActiveKeymapId( m_pGui ) ) );
    }

    ~keymap_settings_t() override { if ( m_subscribed ) { EditorGui_RemoveKeymapListener( m_pGui, OnKeymap, this ); } }

    keymap_platform_t Platform() const { return static_cast<keymap_platform_t>( m_pPlatform->currentData().toInt() ); }
    keymap_section_t Kind() const { return static_cast<keymap_section_t>( m_pKind->currentData().toInt() ); }
    QString Current() const { return m_sourceId; }
    QString Status() const { return m_pStatus->text(); }
    void SetFilter( const QString &text ) { m_pSearch->setText( text ); }
    void SetPlatform( keymap_platform_t platform ) { m_pPlatform->setCurrentIndex( m_pPlatform->findData( static_cast<int>( platform ) ) ); }
    void SetReferenceCallback( void ( *pCallback )( void * ), void *pContext )
    {
        m_pReferenceCallback = pCallback; m_pReferenceContext = pContext; m_pReference->setEnabled( pCallback != nullptr );
    }

    bool Select( const QString &id )
    {
        const key_value_t *pSource = FindRoot( id );
        if ( pSource == nullptr ) { return Fail( QStringLiteral( "Keymap %1 is not in the library." ).arg( id ) ); }
        // Serialize the library's full semantic document, preserving unknown
        // members. The actual store is found rather than reconstructing keys.
        const settings_document_t *pStore = nullptr;
        if ( SettingsDocument_Root( &m_pGui->builtinKeymap ) == pSource ) { pStore = &m_pGui->builtinKeymap; }
        for ( usize i = 0u; i < m_pGui->keymaps.nCount && pStore == nullptr; ++i ) {
            if ( SettingsDocument_Root( m_pGui->keymaps.pData[i] ) == pSource ) { pStore = m_pGui->keymaps.pData[i]; }
        }
        QByteArray contents;
        if ( pStore == nullptr || !Serialize( *pStore, contents ) ) { return Fail( QStringLiteral( "The keymap could not be copied." ) ); }
        const bool builtin = pStore == &m_pGui->builtinKeymap;
        if ( builtin ) {
            // Editing a bundled preset creates a sparse child rather than
            // replacing the preset. Pick an unused default identity so an
            // existing user keymap is never overwritten by an automatic ID.
            QString customId = QStringLiteral( "mason_custom" );
            for ( int suffix = 2; FindRoot( customId ) != nullptr || QFile::exists( QDir( m_folder ).filePath( customId + QStringLiteral( ".cykeymap" ) ) ); ++suffix ) {
                customId = QStringLiteral( "mason_custom_%1" ).arg( suffix );
            }
            contents = QByteArray( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"draft\" name = \"Custom keys\" }\n" );
            auto child = LoadDocument( m_pGui->pAllocator, contents );
            const QByteArray childId = customId.toUtf8(), baseId = id.toUtf8();
            if ( !child || EditorKeymap_SetHeader( child.get(), View( childId ), StringView_FromCString( "Custom keys" ), View( baseId ) ) != keymap_status_t::OK ||
                 !Serialize( *child, contents ) ) { return Fail( QStringLiteral( "The preset's working copy could not be allocated." ) ); }
        }
        auto draft = LoadDocument( m_pGui->pAllocator, contents );
        if ( !draft ) { return Fail( QStringLiteral( "The working copy could not be allocated." ) ); }
        QByteArray snapshot;
        if ( !Serialize( *draft, snapshot ) ) { return Fail( QStringLiteral( "The working copy could not be serialized." ) ); }
        m_draft = std::move( draft ); m_snapshot = snapshot; m_sourceId = id; m_imported = false; m_createdProfile = false;
        m_pendingSelection = id != Text( EditorGui_ActiveKeymapId( m_pGui ) );
        LoadIdentity(); FillPresets(); Refresh();
        m_pStatus->setText( QStringLiteral( "Choose a profile, or create one with New Profile / Duplicate Profile. Changes are staged until Save and activate profile. "
                                           "The bundled default uses Hammer-style Mason bindings." ) );
        return true;
    }

    bool NewProfile( const QString &id, const QString &name, const QString &baseId )
    {
        if ( FindRoot( baseId ) == nullptr ) { return Fail( QStringLiteral( "Choose an installed starting profile." ) ); }
        auto candidate = LoadDocument( m_pGui->pAllocator, QByteArray( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"draft\" name = \"Draft\" }\n" ) );
        if ( !candidate ) { return Fail( QStringLiteral( "The new profile could not be allocated; your working copy is unchanged." ) ); }
        return AdoptProfile( std::move( candidate ), id, name, baseId, false );
    }

    bool DuplicateProfile( const QString &id, const QString &name )
    {
        const QString text = DraftText();
        if ( text.isEmpty() ) { return Fail( QStringLiteral( "The current profile identity is invalid or could not be copied." ) ); }
        auto candidate = LoadDocument( m_pGui->pAllocator, text.toUtf8() );
        if ( !candidate ) { return Fail( QStringLiteral( "The duplicate could not be allocated; your working copy is unchanged." ) ); }
        const QString base = Text( EditorKeymap_Header( SettingsDocument_Root( candidate.get() ) ).base );
        return AdoptProfile( std::move( candidate ), id, name, base, true );
    }

    bool HasChanges() const
    {
        if ( !m_draft ) { return false; }
        QByteArray current;
        if ( !Serialize( *m_draft, current ) ) { return true; }
        return current != m_snapshot || m_imported || m_createdProfile || m_pendingSelection ||
               m_pId->text() != m_defaultId || m_pName->text() != m_defaultName;
    }

    void Revert() { ( void )Select( Text( EditorGui_ActiveKeymapId( m_pGui ) ) ); }

    bool SetTriggers( keymap_section_t section, keymap_platform_t platform, const QString &context, const QString &id, const QStringList &input )
    {
        if ( !m_draft || input.size() > static_cast<qsizetype>( EDITOR_KEYMAP_MAX_CHORDS ) ) { return Fail( QStringLiteral( "Choose an action and at most four alternatives." ) ); }
        std::vector<QByteArray> storage; storage.reserve( static_cast<usize>( input.size() ) );
        for ( const QString &trigger : input ) {
            const QString canonical = Canonical( section, trigger );
            if ( canonical.isEmpty() ) { return Fail( QStringLiteral( "Invalid %1 trigger: %2. See the syntax examples below." ).arg( SectionLabel( section ), trigger ) ); }
            const QByteArray bytes = canonical.toUtf8();
            if ( std::find( storage.begin(), storage.end(), bytes ) == storage.end() ) { storage.push_back( bytes ); }
        }
        string_view_t texts[EDITOR_KEYMAP_MAX_CHORDS]{};
        for ( usize i = 0u; i < storage.size(); ++i ) { texts[i] = View( storage[i] ); }
        const QByteArray contextBytes = context.trimmed().toUtf8(), idBytes = id.trimmed().toUtf8();
        // Core setters can allocate after removing their old entry. Mutate a
        // full private candidate so any allocation failure preserves the draft.
        QByteArray before;
        if ( !Serialize( *m_draft, before ) ) { return Fail( QStringLiteral( "The binding could not be copied; no changes were made." ) ); }
        auto candidate = LoadDocument( m_pGui->pAllocator, before );
        if ( !candidate ) { return Fail( QStringLiteral( "The binding could not be copied; no changes were made." ) ); }
        const auto result = EditorKeymap_SetTriggersOn( candidate.get(), platform, section, View( contextBytes ), View( idBytes ), texts, storage.size() );
        if ( result != keymap_status_t::OK ) { return Fail( QStringLiteral( "The binding was rejected (%1); the working copy is unchanged." ).arg( static_cast<int>( result ) ) ); }
        m_draft = std::move( candidate );
        m_selectedKey = QString::number( static_cast<int>( section ) ) + QLatin1Char( '\t' ) + context.trimmed() + QLatin1Char( '\t' ) + id.trimmed();
        Refresh();
        const QStringList conflicts = Conflicts();
        m_pStatus->setText( conflicts.isEmpty() ? QStringLiteral( "Binding staged. Apply to save and activate it." ) :
                           QStringLiteral( "Binding staged, but resolve this conflict before Apply: %1" ).arg( conflicts.first() ) );
        return true;
    }

    bool Reset( keymap_section_t section, keymap_platform_t platform, const QString &context, const QString &id )
    {
        if ( !m_draft ) { return false; }
        const QByteArray contextBytes = context.toUtf8(), idBytes = id.toUtf8();
        if ( EditorKeymap_ResetOn( m_draft.get(), platform, section, View( contextBytes ), View( idBytes ) ) != keymap_status_t::OK ) {
            return Fail( QStringLiteral( "The inherited binding could not be restored." ) );
        }
        Refresh(); m_pStatus->setText( QStringLiteral( "Override removed; this action now inherits from the base or common platform section." ) ); return true;
    }

    QStringList Rows() const
    {
        QStringList result;
        for ( int i = 0; i < m_pRows->topLevelItemCount(); ++i ) {
            const auto *pItem = m_pRows->topLevelItem( i ); const auto &row = m_rows[static_cast<usize>( pItem->data( 0, Qt::UserRole ).toInt() )];
            result.append( QStringList{ row.category, row.id, SectionLabel( row.section ), row.context, row.triggers.join( QStringLiteral( "; " ) ),
                                        row.origin, row.availability }.join( QLatin1Char( '\t' ) ) );
        }
        return result;
    }

    QStringList Conflicts() const
    {
        QStringList result;
        const auto append = [&result]( const std::vector<binding_row_t> &rows, keymap_platform_t platform, bool windowStack ) {
            for ( usize a = 0u; a < rows.size(); ++a ) {
                for ( usize b = a + 1u; b < rows.size(); ++b ) {
                    const auto &first = rows[a], &second = rows[b];
                    if ( first.section != second.section || ( !windowStack && first.context != second.context ) ||
                         first.id == second.id || SharedSelectionDrag( first, second ) ) { continue; }
                    bool found = false;
                    for ( const QString &aa : first.triggers ) {
                        for ( const QString &bb : second.triggers ) {
                            if ( !TriggerCollision( first.section, aa, bb ) ) { continue; }
                            result.append( QStringLiteral( "%1 · %2 · %3: %4 and %5 (%6 / %7)" )
                                           .arg( platform == keymap_platform_t::NONE ? QStringLiteral( "Common" ) : QString::fromLatin1( PlatformName( platform ) ),
                                                 windowStack ? QStringLiteral( "Window (map → global)" ) : first.context,
                                                 SectionLabel( first.section ), first.id, second.id, aa, bb ) );
                            found = true; break;
                        }
                        if ( found ) { break; }
                    }
                }
            }
        };
        const auto chain = Chain();
        for ( const auto platform : { keymap_platform_t::NONE, keymap_platform_t::MACOS, keymap_platform_t::WINDOWS, keymap_platform_t::LINUX } ) {
            append( Collect( platform ), platform, false );
            // Mason installs map + global declarations simultaneously as
            // window QActions. Resolve each registered command through that
            // stack first, including an explicit map-level unbinding, then
            // check the resulting installed alternatives for ambiguity.
            std::vector<binding_row_t> windowRows;
            for ( usize i = 0u; i < EditorCommands_Count( &m_pGui->commands ); ++i ) {
                const auto *pCommand = EditorCommands_At( &m_pGui->commands, i );
                if ( ( pCommand->flags & COMMAND_FLAG_CONSOLE_ONLY ) != 0u ) { continue; }
                binding_row_t row{}; row.id = QString::fromUtf8( pCommand->pId );
                keymap_binding_t binding{}; keymap_lookup_t lookup = keymap_lookup_t::NOT_DEFINED;
                for ( const char *pContext : { "map", "global" } ) {
                    lookup = EditorKeymap_FindBindingOn( chain.data(), chain.size(), platform, StringView_FromCString( pContext ),
                                                        StringView_FromCString( pCommand->pId ), &binding );
                    if ( lookup != keymap_lookup_t::NOT_DEFINED ) { row.context = QString::fromLatin1( pContext ); break; }
                }
                if ( lookup != keymap_lookup_t::BOUND ) { continue; }
                for ( usize k = 0u; k < binding.nChords; ++k ) {
                    char text[EDITOR_KEY_CHORD_TEXT_CAPACITY]{}; const usize count = EditorKeyChord_Format( binding.chords[k], text );
                    if ( count != 0u ) { row.triggers.append( QString::fromUtf8( text, static_cast<qsizetype>( count ) ) ); }
                }
                windowRows.push_back( std::move( row ) );
            }
            append( windowRows, platform, true );
        }
        result.removeDuplicates(); return result;
    }

    QString DraftText() const
    {
        if ( !m_draft ) { return {}; }
        QByteArray contents;
        if ( !Serialize( *m_draft, contents ) ) { return {}; }
        auto copy = LoadDocument( m_pGui->pAllocator, contents );
        if ( !copy ) { return {}; }
        const QByteArray id = m_pId->text().trimmed().toUtf8(), name = m_pName->text().trimmed().toUtf8();
        const QByteArray base = Text( EditorKeymap_Header( SettingsDocument_Root( copy.get() ) ).base ).toUtf8();
        if ( EditorKeymap_SetHeader( copy.get(), View( id ), View( name ), View( base ) ) != keymap_status_t::OK || !Serialize( *copy, contents ) ) { return {}; }
        return QString::fromUtf8( contents );
    }

    bool Import( const QString &path )
    {
        QFile file( path );
        if ( !file.open( QIODevice::ReadOnly ) || file.size() > static_cast<qint64>( CY_SETTINGS_TEXT_MAX_BYTES ) ) {
            return Fail( QStringLiteral( "The keymap could not be opened or exceeds the 16 MiB limit." ) );
        }
        const QByteArray contents = file.read( static_cast<qint64>( CY_SETTINGS_TEXT_MAX_BYTES ) + 1 );
        if ( file.error() != QFileDevice::NoError || contents.size() > static_cast<qsizetype>( CY_SETTINGS_TEXT_MAX_BYTES ) ) {
            return Fail( QStringLiteral( "The keymap could not be read completely." ) );
        }
        auto imported = LoadDocument( m_pGui->pAllocator, contents );
        QString error;
        if ( !imported || !ValidateDeclarations( SettingsDocument_Root( imported.get() ), error ) ) {
            return Fail( error.isEmpty() ? QStringLiteral( "Import requires a valid Cypher .cykeymap document, not a foreign editor config." ) : error );
        }
        QByteArray snapshot;
        if ( !Serialize( *imported, snapshot ) ) { return Fail( QStringLiteral( "The imported keymap could not be copied." ) ); }
        m_draft = std::move( imported ); m_snapshot = snapshot;
        m_sourceId = Text( EditorKeymap_Header( SettingsDocument_Root( m_draft.get() ) ).id ); m_imported = true; m_createdProfile = false;
        m_pendingSelection = true;
        LoadIdentity(); FillPresets(); Refresh();
        bool complete = false; ( void )Chain( nullptr, &complete );
        m_pStatus->setText( complete ? QStringLiteral( "Imported into the working copy. Apply to save and activate; unknown entries are retained." ) :
                                      QStringLiteral( "Imported, but its base is missing or cyclic. Load the base before Apply; export preserves the document." ) );
        return true;
    }

    bool Export( const QString &path )
    {
        const QString text = DraftText();
        if ( text.isEmpty() ) { return Fail( QStringLiteral( "The profile identity is invalid or serialization failed." ) ); }
        if ( !WriteExport( path, text.toUtf8() ) ) { return false; }
        bool complete = false; const auto chain = Chain( nullptr, &complete ); QStringList dependencies;
        for ( usize i = 1u; i < chain.size(); ++i ) { dependencies.append( Text( EditorKeymap_Header( chain[i] ).id ) ); }
        const QString base = Text( EditorKeymap_Header( SettingsDocument_Root( m_draft.get() ) ).base );
        if ( !complete ) { dependencies.append( QStringLiteral( "unresolved base chain starting at %1" ).arg( base ) ); }
        m_pStatus->setText( dependencies.isEmpty() ? QStringLiteral( "Exported source profile %1 with its metadata and platform declarations; no base dependency." ).arg( QDir::toNativeSeparators( path ) ) :
                           QStringLiteral( "Exported source profile %1. The recipient must also install: %2. Use Export portable for a self-contained file." )
                           .arg( QDir::toNativeSeparators( path ), dependencies.join( QStringLiteral( ", " ) ) ) );
        return true;
    }

    bool ExportPortable( const QString &path )
    {
        const QString text = DraftText();
        if ( text.isEmpty() ) { return Fail( QStringLiteral( "The profile identity is invalid or serialization failed." ) ); }
        auto source = LoadDocument( m_pGui->pAllocator, text.toUtf8() );
        if ( !source ) { return Fail( QStringLiteral( "Portable export could not allocate a source copy; no file was changed." ) ); }
        // Startup intentionally tolerates invalid declarations and falls back
        // to a base. WriteComplete keeps authored output entries, so removing
        // base from that malformed document would lose its working fallback.
        // Preserve its authored source, but refuse a misleading portable file.
        QString declarationError;
        if ( !ValidateDeclarations( SettingsDocument_Root( source.get() ), declarationError ) ) {
            return Fail( QStringLiteral( "Cannot export a portable profile with invalid declarations: %1 Correct them or use Export source to preserve the authored document. No file was changed." )
                         .arg( declarationError ) );
        }
        bool complete = false; const auto chain = Chain( SettingsDocument_Root( source.get() ), &complete );
        if ( !complete ) { return Fail( QStringLiteral( "Cannot export a portable profile: its base is missing, cyclic or exceeds eight levels. No file was changed." ) ); }
        // Own valid declarations already decide the effective entry on their
        // layer. WriteComplete fills only absent entries with the source
        // chain's resolved decisions, so the full tree (including unknown
        // metadata, platform members and future records) can stay intact.
        auto resolved = LoadDocument( m_pGui->pAllocator, text.toUtf8() );
        if ( !resolved ) { return Fail( QStringLiteral( "Portable export could not allocate the output copy; no file was changed." ) ); }
        const auto header = EditorKeymap_Header( SettingsDocument_Root( source.get() ) );
        QByteArray bytes;
        if ( EditorKeymap_SetHeader( resolved.get(), header.id, header.name, {} ) != keymap_status_t::OK ||
             EditorKeymap_WriteComplete( resolved.get(), chain.data(), chain.size() ) != keymap_status_t::OK ||
             !Serialize( *resolved, bytes ) ) {
            return Fail( QStringLiteral( "Portable export could not resolve all bindings; no file was changed." ) );
        }
        if ( !WriteExport( path, bytes ) ) { return false; }
        m_pStatus->setText( QStringLiteral( "Exported portable profile %1. Common, macOS, Windows and Linux bindings include staged edits and require no base profiles. "
                                           "This profile's metadata and unknown platforms were retained; unknown platform inheritance is not resolved." )
                           .arg( QDir::toNativeSeparators( path ) ) );
        return true;
    }

    bool Apply( const QString &id, const QString &name )
    {
        if ( !id.isEmpty() ) { m_pId->setText( id ); }
        if ( !name.isEmpty() ) { m_pName->setText( name ); }
        if ( !m_draft ) { return false; }
        bool complete = false; ( void )Chain( nullptr, &complete );
        if ( !complete ) { return Fail( QStringLiteral( "Cannot Apply: the keymap base is missing, cyclic or exceeds eight levels." ) ); }
        const QStringList conflicts = Conflicts();
        if ( !conflicts.isEmpty() ) { return Fail( QStringLiteral( "Cannot Apply until conflicting bindings are changed or unbound: %1" ).arg( conflicts.first() ) ); }
        QString effectiveId = m_sourceId;
        QByteArray current; if ( !Serialize( *m_draft, current ) ) { return Fail( QStringLiteral( "The working copy could not be serialized." ) ); }
        const bool modified = current != m_snapshot || m_imported || m_createdProfile || m_pId->text() != m_defaultId || m_pName->text() != m_defaultName;
        m_publishing = true;
        if ( modified ) {
            effectiveId = m_pId->text().trimmed();
            if ( m_createdProfile && ProfileExists( effectiveId ) ) {
                m_publishing = false; return Fail( QStringLiteral( "That profile ID already exists. Choose a distinct ID; the existing profile was preserved." ) );
            }
            const QString contents = DraftText();
            if ( contents.isEmpty() || effectiveId == Text( EditorKeymap_Header( SettingsDocument_Root( &m_pGui->builtinKeymap ) ).id ) ) {
                m_publishing = false; return Fail( QStringLiteral( "Choose a user keymap ID (lower-case letters, digits and underscores) and a nonempty name." ) );
            }
            if ( m_folder.isEmpty() || !QDir().mkpath( m_folder ) ) { m_publishing = false; return Fail( QStringLiteral( "The user keymap folder could not be created." ) ); }
            QString error;
            if ( EditorGui_SaveKeymap( m_pGui, contents, QDir( m_folder ).filePath( effectiveId + QStringLiteral( ".cykeymap" ) ), &error ) != editor_gui_status_t::OK ) {
                m_publishing = false; return Fail( error.isEmpty() ? QStringLiteral( "The keymap could not be saved and published." ) : error );
            }
        }
        const QByteArray effectiveBytes = effectiveId.toUtf8();
        if ( !modified && EditorGui_SelectKeymap( m_pGui, View( effectiveBytes ) ) != editor_gui_status_t::OK ) {
            m_publishing = false; return Fail( QStringLiteral( "The selected keymap could not be activated." ) );
        }
        bool preferenceSaved = true;
        QString controllingId, controllingScope;
        if ( const auto *pSetting = EditorSettings_Find( &m_pGui->settings, StringView_FromCString( "editor.ui.keymap" ) ) ) {
            setting_value_t value{}; value.type = setting_type_t::STRING; value.text = View( effectiveBytes );
            const auto written = EditorSettings_Write( &m_pGui->settings, settings_scope_t::USER, *pSetting, value );
            preferenceSaved = written == settings_registry_status_t::OK;
            const auto resolved = EditorSettings_Resolve( &m_pGui->settings, *pSetting );
            if ( resolved.value.type == setting_type_t::STRING && Text( resolved.value.text ) != effectiveId ) {
                controllingId = Text( resolved.value.text );
                controllingScope = resolved.source == settings_scope_t::WORKSPACE ? QStringLiteral( "Workspace" ) :
                                   resolved.source == settings_scope_t::PROJECT ? QStringLiteral( "Project" ) : QStringLiteral( "Effective settings" );
            }
        }
        const QString activeId = Text( EditorGui_ActiveKeymapId( m_pGui ) );
        m_publishing = false; ( void )Select( effectiveId );
        // A higher-scope setting may reselect another keymap synchronously.
        // Saving still succeeded; do not treat that acknowledged policy as an
        // unapplied edit or prompt again when Settings closes.
        m_pendingSelection = false; UpdateState();
        if ( !controllingId.isEmpty() ) {
            m_pStatus->setText( QStringLiteral( "Keymap %1 saved. %2 scope selects %3 and controls the input preference; current input uses %4. "
                                               "Change or reset that scope's editor.ui.keymap to use the saved keymap." )
                               .arg( effectiveId, controllingScope, controllingId, activeId ) );
        } else if ( activeId != effectiveId ) {
            m_pStatus->setText( QStringLiteral( "Keymap %1 saved, but current input still uses %2. The active keymap changed during publication." ).arg( effectiveId, activeId ) );
        } else {
            m_pStatus->setText( preferenceSaved ? QStringLiteral( "Keymap saved and activated. Registered keyboard actions and supported 3D camera bindings follow it; unrouted declarations are retained." ) :
                                                QStringLiteral( "Keymap applied, but saving its startup preference failed. Select it again next session." ) );
        }
        return true;
    }

    bool CanClose()
    {
        if ( !HasChanges() ) { return true; }
        const auto answer = QMessageBox::question( this, QStringLiteral( "Unapplied keybindings" ),
                                                   QStringLiteral( "Apply the staged keymap before closing Settings?" ),
                                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
        if ( answer == QMessageBox::Save ) { return Apply( {}, {} ); }
        if ( answer == QMessageBox::Discard ) { Revert(); return true; }
        return false;
    }

private:
    bool ProfileExists( const QString &id ) const
    {
        return FindRoot( id ) != nullptr || QFile::exists( QDir( m_folder ).filePath( id + QStringLiteral( ".cykeymap" ) ) );
    }

    QString UnusedProfileId( QString stem ) const
    {
        stem = stem.toLower(); stem.replace( QRegularExpression( QStringLiteral( "[^a-z0-9_]+" ) ), QStringLiteral( "_" ) );
        if ( stem.isEmpty() || stem.front().isDigit() ) { stem.prepend( QStringLiteral( "profile_" ) ); }
        stem = stem.left( 52 );
        QString id = stem;
        for ( int suffix = 2; ProfileExists( id ); ++suffix ) { id = stem + QStringLiteral( "_%1" ).arg( suffix ); }
        return id;
    }

    bool AdoptProfile( std::unique_ptr<settings_document_t> candidate, const QString &id, const QString &name,
                       const QString &base, bool duplicate )
    {
        const QString profileId = id.trimmed(), profileName = name.trimmed();
        if ( ProfileExists( profileId ) ) { return Fail( QStringLiteral( "That profile ID already exists. Choose a distinct ID; the existing profile was preserved." ) ); }
        const QByteArray idBytes = profileId.toUtf8(), nameBytes = profileName.toUtf8(), baseBytes = base.toUtf8();
        if ( EditorKeymap_SetHeader( candidate.get(), View( idBytes ), View( nameBytes ), View( baseBytes ) ) != keymap_status_t::OK ) {
            return Fail( QStringLiteral( "Use a profile ID with lower-case letters, digits and underscores, and a nonempty name of at most 128 bytes. Your working copy is unchanged." ) );
        }
        bool complete = false; ( void )Chain( SettingsDocument_Root( candidate.get() ), &complete );
        if ( !complete ) { return Fail( QStringLiteral( "The starting profile's base chain is missing, cyclic or too deep; your working copy is unchanged." ) ); }
        QByteArray snapshot;
        if ( !Serialize( *candidate, snapshot ) ) { return Fail( QStringLiteral( "The profile could not be copied; your working copy is unchanged." ) ); }
        m_draft = std::move( candidate ); m_snapshot = snapshot; m_sourceId = profileId;
        m_imported = false; m_createdProfile = true; m_pendingSelection = true;
        LoadIdentity(); FillPresets(); Refresh();
        m_pStatus->setText( duplicate ? QStringLiteral( "Duplicate profile %1 staged. Its current edits, base, platform metadata and unknown records were copied. Save and activate profile to create it." ).arg( profileName ) :
                           QStringLiteral( "New profile %1 staged. It inherits %2; customize its bindings, then Save and activate profile to create it." ).arg( profileName, base ) );
        return true;
    }

    void ProfileDialog( bool duplicate )
    {
        QDialog dialog( this ); dialog.setObjectName( QStringLiteral( "KeymapProfileDialog" ) );
        dialog.setWindowTitle( duplicate ? QStringLiteral( "Duplicate keybinding profile" ) : QStringLiteral( "New keybinding profile" ) );
        auto *pLayout = new QVBoxLayout( &dialog );
        auto *pInfo = new QLabel( duplicate ? QStringLiteral( "Copy the current working profile and its staged changes. The original profile remains available." ) :
                                 QStringLiteral( "Choose a starting profile. The new profile inherits its bindings and stores your overrides." ), &dialog );
        pInfo->setWordWrap( true ); pLayout->addWidget( pInfo );
        auto *pForm = new QFormLayout();
        auto *pName = new QLineEdit( duplicate ? m_pName->text().trimmed() + QStringLiteral( " copy" ) : QStringLiteral( "My profile" ), &dialog );
        auto *pId = new QLineEdit( UnusedProfileId( duplicate ? m_pId->text().trimmed() + QStringLiteral( "_copy" ) : QStringLiteral( "my_profile" ) ), &dialog );
        pName->setObjectName( QStringLiteral( "KeymapProfileDialogName" ) ); pId->setObjectName( QStringLiteral( "KeymapProfileDialogId" ) );
        pForm->addRow( QStringLiteral( "Profile name" ), pName ); pForm->addRow( QStringLiteral( "Profile ID" ), pId );
        QComboBox *pBase = nullptr;
        if ( !duplicate ) {
            pBase = new QComboBox( &dialog ); pBase->setObjectName( QStringLiteral( "KeymapProfileDialogBase" ) );
            for ( const QString &id : EditorGui_KeymapIds( m_pGui ) ) { pBase->addItem( Text( EditorKeymap_Header( FindRoot( id ) ).name ), id ); }
            const int current = pBase->findData( m_sourceId ); if ( current >= 0 ) { pBase->setCurrentIndex( current ); }
            pForm->addRow( QStringLiteral( "Starting profile" ), pBase );
        }
        pLayout->addLayout( pForm );
        auto *pError = new QLabel( &dialog ); pError->setWordWrap( true ); pLayout->addWidget( pError );
        auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        pButtons->button( QDialogButtonBox::Ok )->setText( QStringLiteral( "Create draft" ) ); pLayout->addWidget( pButtons );
        QObject::connect( pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        QObject::connect( pButtons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, duplicate, pName, pId, pBase, pError]() {
            // Duplicate carries the current edits forward. New begins from an
            // installed source and must acknowledge replacing a dirty draft.
            if ( !duplicate && HasChanges() && !ConfirmDiscard() ) { return; }
            const bool created = duplicate ? DuplicateProfile( pId->text(), pName->text() ) : NewProfile( pId->text(), pName->text(), pBase->currentData().toString() );
            if ( created ) { dialog.accept(); } else { pError->setText( Status() ); }
        } );
        dialog.resize( 480, dialog.sizeHint().height() ); ( void )dialog.exec();
    }

    void ExportDialog( bool portable )
    {
        QString path = QFileDialog::getSaveFileName( this, portable ? QStringLiteral( "Export portable keybinding profile" ) : QStringLiteral( "Export source profile with base dependencies" ),
                                                   m_pId->text() + QStringLiteral( ".cykeymap" ), QStringLiteral( "Cypher keymaps (*.cykeymap)" ) );
        if ( path.isEmpty() ) { return; }
        if ( !path.endsWith( QStringLiteral( ".cykeymap" ), Qt::CaseInsensitive ) ) { path += QStringLiteral( ".cykeymap" ); }
        if ( portable ) { ( void )ExportPortable( path ); } else { ( void )Export( path ); }
    }

    bool WriteExport( const QString &path, const QByteArray &bytes )
    {
        QSaveFile file( path );
        if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() ) {
            return Fail( QStringLiteral( "Export failed; the previous file was preserved." ) );
        }
        return true;
    }

    const key_value_t *FindRoot( const QString &id ) const
    {
        const QByteArray bytes = id.toUtf8();
        for ( usize i = 0u; i < m_pGui->keymapLibrary.nCount; ++i ) {
            if ( StringView_Equals( EditorKeymap_Header( m_pGui->keymapLibrary.pData[i] ).id, View( bytes ) ) ) { return m_pGui->keymapLibrary.pData[i]; }
        }
        return nullptr;
    }

    std::vector<const key_value_t *> Chain( const key_value_t *pRoot = nullptr, bool *pComplete = nullptr ) const
    {
        const key_value_t *chain[EDITOR_KEYMAP_MAX_DEPTH]{}; bool_t complete = CY_FALSE;
        if ( pRoot == nullptr && m_draft ) { pRoot = SettingsDocument_Root( m_draft.get() ); }
        const usize count = EditorKeymap_BuildChain( m_pGui->keymapLibrary.pData, m_pGui->keymapLibrary.nCount, pRoot, chain, EDITOR_KEYMAP_MAX_DEPTH, &complete );
        if ( pComplete != nullptr ) { *pComplete = complete != CY_FALSE; }
        return { chain, chain + count };
    }

    std::vector<binding_row_t> Collect( keymap_platform_t platform ) const
    {
        std::vector<binding_row_t> result;
        if ( !m_draft ) { return result; }
        const auto chain = Chain(); QSet<QString> seen;
        const auto add = [this, platform, &chain, &seen, &result]( keymap_section_t section, const QString &context, const QString &id ) {
            binding_row_t row{}; row.section = section; row.context = context; row.id = id; row.category = Category( id, context );
            if ( seen.contains( row.Key() ) ) { return; } seen.insert( row.Key() );
            const QByteArray contextBytes = context.toUtf8(), idBytes = id.toUtf8();
            keymap_triggers_t triggers{};
            const auto status = EditorKeymap_FindTriggers( chain.data(), chain.size(), section, platform, View( contextBytes ), View( idBytes ), &triggers );
            for ( usize t = 0u; t < triggers.nTexts; ++t ) {
                const QString canonical = Canonical( section, Text( triggers.texts[t] ) ); if ( !canonical.isEmpty() ) { row.triggers.append( canonical ); }
            }
            if ( status == keymap_lookup_t::NOT_DEFINED ) { row.origin = QStringLiteral( "Not defined" ); }
            else if ( triggers.iSource < chain.size() ) {
                const key_value_t *pDeciding = chain[triggers.iSource];
                const bool overlay = platform != keymap_platform_t::NONE && Declaration( pDeciding, section, platform, context, id ) != nullptr;
                row.origin = triggers.iSource == 0u ? QStringLiteral( "This keymap" ) : QStringLiteral( "Inherited: %1" ).arg( Text( EditorKeymap_Header( pDeciding ).name ) );
                if ( platform != keymap_platform_t::NONE ) { row.origin += overlay ? QStringLiteral( " · platform" ) : QStringLiteral( " · common" ); }
            }
            const command_desc_t *pCommand = EditorCommands_Find( &m_pGui->commands, View( idBytes ) );
            row.label = pCommand != nullptr && pCommand->pLabel != nullptr ? QString::fromUtf8( pCommand->pLabel ) : Humanize( id );
            row.description = pCommand != nullptr && pCommand->pDescription != nullptr ? QString::fromUtf8( pCommand->pDescription ) : id;
            if ( section != keymap_section_t::BINDINGS || id == QStringLiteral( "map.tool.confirm" ) || id == QStringLiteral( "map.tool.cancel" ) ) {
                row.availability = QStringLiteral( "Input declaration" );
            } else if ( pCommand == nullptr ) { row.availability = QStringLiteral( "Not registered" ); }
            else { row.availability = ( EditorCommands_State( &m_pGui->commands, View( idBytes ) ) & COMMAND_STATE_ENABLED ) != 0u ?
                                       QStringLiteral( "Registered" ) : QStringLiteral( "Disabled now" ); }
            if ( section != keymap_section_t::BINDINGS ) {
                const bool cameraContext = context == QStringLiteral( "global" ) || context == QStringLiteral( "map" ) ||
                                           context == QStringLiteral( "map.viewport" ) || context == QStringLiteral( "map.viewport.3d" ) ||
                                           context.startsWith( QStringLiteral( "map.tool." ) ) || context.startsWith( QStringLiteral( "map.selection." ) );
                const bool navigation = id == QStringLiteral( "map.camera.forward" ) || id == QStringLiteral( "map.camera.back" ) ||
                                        id == QStringLiteral( "map.camera.left" ) || id == QStringLiteral( "map.camera.right" ) ||
                                        id == QStringLiteral( "map.camera.up" ) || id == QStringLiteral( "map.camera.down" ) ||
                                        id == QStringLiteral( "map.camera.fast" ) || id == QStringLiteral( "map.camera.slow" );
                const bool cameraMouse = id == QStringLiteral( "map.camera.look" ) || id == QStringLiteral( "map.camera.orbit" ) ||
                                         id == QStringLiteral( "map.camera.pan" ) || id == QStringLiteral( "map.camera.dolly" );
                row.availability = QStringLiteral( "Declaration · no known route" );
                if ( section == keymap_section_t::HELD && navigation && cameraContext ) {
                    row.availability = QStringLiteral( "3D camera held declaration" );
                } else if ( section == keymap_section_t::MOUSE && cameraMouse && cameraContext ) {
                    row.availability = QStringLiteral( "3D camera gesture declaration" );
                    for ( const QString &text : row.triggers ) {
                        mouse_gesture_t gesture{}; const QByteArray bytes = text.toUtf8();
                        const bool parsed = EditorMouseGesture_Parse( View( bytes ), &gesture ) != CY_FALSE;
                        const bool drag = gesture.action == MOUSE_ACTION_DRAG && gesture.button != MOUSE_BUTTON_NONE;
                        const bool wheel = id == QStringLiteral( "map.camera.dolly" ) && gesture.heldKey == KEY_NONE && gesture.button == MOUSE_BUTTON_NONE &&
                                           ( gesture.action == MOUSE_ACTION_WHEEL || gesture.action == MOUSE_ACTION_WHEEL_UP || gesture.action == MOUSE_ACTION_WHEEL_DOWN );
                        if ( !parsed || ( gesture.heldKey != KEY_NONE && gesture.heldKey != KEY_SPACE ) || ( !drag && !wheel ) ) {
                            row.availability = QStringLiteral( "Contains unrouted camera trigger" );
                            row.description += QStringLiteral( "\nCamera input supports button drags with modifiers/Space and vertical dolly wheel gestures; other declarations are retained but not dispatched." );
                            break;
                        }
                    }
                } else if ( section == keymap_section_t::MOUSE && ( id.startsWith( QStringLiteral( "map.select." ) ) ||
                            id.startsWith( QStringLiteral( "map.transform." ) ) || id == QStringLiteral( "map.view.pan" ) ||
                            id == QStringLiteral( "map.view.zoom" ) || id == QStringLiteral( "map.context_menu" ) ) ) {
                    row.availability = QStringLiteral( "Not remappable yet" );
                    row.description += QStringLiteral( "\nCurrent geometry/2D mouse handlers use fixed input. Editing this declaration is saved for future routing and does not change their behavior yet." );
                }
            }
            // Qt window actions accept sequences, but viewport-local input
            // currently dispatches individual strokes. Preserve valid format
            // declarations while making this routing limit explicit.
            if ( section == keymap_section_t::BINDINGS && context.startsWith( QStringLiteral( "map." ) ) ) {
                for ( const QString &trigger : row.triggers ) {
                    key_chord_t chord{}; const QByteArray bytes = trigger.toUtf8();
                    if ( EditorKeyChord_Parse( View( bytes ), &chord ) && chord.nStrokes > 1u ) {
                        row.availability = QStringLiteral( "Sequence not routed by view" );
                        row.description += QStringLiteral( "\nMultiple-stroke sequences are retained in the document, but this view/tool context currently routes one stroke at a time." );
                        break;
                    }
                }
            }
            result.push_back( std::move( row ) );
        };
        for ( const auto section : { keymap_section_t::BINDINGS, keymap_section_t::HELD, keymap_section_t::MOUSE } ) {
            for ( const key_value_t *pRoot : chain ) {
                for ( const auto scope : { keymap_platform_t::NONE, platform } ) {
                    const key_value_t *pContexts = Section( pRoot, section, scope );
                    for ( usize c = 0u; pContexts != nullptr && c < KeyValue_ChildCount( pContexts ); ++c ) {
                        const auto *pContext = KeyValue_ChildAt( pContexts, c );
                        if ( KeyValue_Type( pContext ) != key_value_type_t::OBJECT ) { continue; }
                        for ( usize a = 0u; a < KeyValue_ChildCount( pContext ); ++a ) {
                            add( section, Text( KeyValue_Name( pContext ) ), Text( KeyValue_Name( KeyValue_ChildAt( pContext, a ) ) ) );
                        }
                    }
                }
            }
        }
        // Registered commands without a declaration remain discoverable and
        // can be bound. Their initial workspace context follows their module.
        QSet<QString> declaredCommands;
        for ( const auto &row : result ) { if ( row.section == keymap_section_t::BINDINGS ) { declaredCommands.insert( row.id ); } }
        for ( usize i = 0u; i < EditorCommands_Count( &m_pGui->commands ); ++i ) {
            const QString id = QString::fromUtf8( EditorCommands_At( &m_pGui->commands, i )->pId );
            if ( !declaredCommands.contains( id ) ) { add( keymap_section_t::BINDINGS, id.startsWith( QStringLiteral( "map." ) ) ? QStringLiteral( "map" ) :
                                                                                      QStringLiteral( "global" ), id ); }
        }
        std::stable_sort( result.begin(), result.end(), []( const binding_row_t &a, const binding_row_t &b ) {
            if ( a.category != b.category ) { return a.category < b.category; }
            if ( a.label != b.label ) { return a.label.localeAwareCompare( b.label ) < 0; }
            if ( a.context != b.context ) { return a.context < b.context; }
            return a.section < b.section;
        } );
        return result;
    }

    void LoadIdentity()
    {
        const auto header = EditorKeymap_Header( SettingsDocument_Root( m_draft.get() ) );
        m_defaultId = Text( header.id ); m_defaultName = Text( header.name );
        const QSignalBlocker idBlocker( m_pId ), nameBlocker( m_pName );
        m_pId->setText( m_defaultId ); m_pName->setText( m_defaultName );
        m_pBase->setText( header.base.cchLength == 0u ? QStringLiteral( "Root keymap · user files are saved in %1" ).arg( QDir::toNativeSeparators( m_folder ) ) :
                         QStringLiteral( "Inherits from %1 · user files are saved in %2" ).arg( Text( header.base ), QDir::toNativeSeparators( m_folder ) ) );
    }

    void FillPresets()
    {
        const QSignalBlocker blocker( m_pPresets ); m_pPresets->clear();
        for ( const QString &id : EditorGui_KeymapIds( m_pGui ) ) {
            const auto header = EditorKeymap_Header( FindRoot( id ) ); m_pPresets->addItem( Text( header.name ), id );
        }
        if ( m_imported || m_createdProfile ) {
            m_pPresets->addItem( ( m_imported ? QStringLiteral( "Imported draft: %1" ) : QStringLiteral( "New draft: %1" ) ).arg( m_defaultName ), m_sourceId );
            m_pPresets->setCurrentIndex( m_pPresets->count() - 1 );
        }
        else { m_pPresets->setCurrentIndex( m_pPresets->findData( m_sourceId ) ); }
    }

    void Refresh()
    {
        m_rows = Collect( Platform() );
        const QString context = m_pContext->currentText(), id = m_pAction->currentText();
        const QSignalBlocker contextBlocker( m_pContext ), actionBlocker( m_pAction );
        m_pContext->clear(); m_pAction->clear(); QSet<QString> contexts, actions;
        for ( const auto &row : m_rows ) { contexts.insert( row.context ); actions.insert( row.id ); }
        QStringList sortedContexts = contexts.values(), sortedActions = actions.values(); sortedContexts.sort(); sortedActions.sort();
        m_pContext->addItems( sortedContexts ); m_pAction->addItems( sortedActions );
        m_pContext->setCurrentText( context ); m_pAction->setCurrentText( id );
        FillRows(); UpdateState();
    }

    void FillRows()
    {
        const QSignalBlocker blocker( m_pRows ); m_pRows->clear();
        const QString query = m_pSearch->text().trimmed();
        const QString category = m_pCategories->currentItem() != nullptr ? m_pCategories->currentItem()->text( 0 ) : QStringLiteral( "All actions" );
        QTreeWidgetItem *pSelected = nullptr;
        for ( usize i = 0u; i < m_rows.size(); ++i ) {
            const auto &row = m_rows[i];
            if ( category != QStringLiteral( "All actions" ) && row.category != category ) { continue; }
            const QString searchable = QStringList{ row.id, row.context, row.category, row.label, row.description, row.triggers.join( QLatin1Char( ' ' ) ),
                                                     row.origin, row.availability, SectionLabel( row.section ) }.join( QLatin1Char( ' ' ) );
            bool matches = true;
            for ( const QString &word : query.split( QLatin1Char( ' ' ), Qt::SkipEmptyParts ) ) { if ( !searchable.contains( word, Qt::CaseInsensitive ) ) { matches = false; break; } }
            if ( !matches ) { continue; }
            QStringList display;
            for ( const QString &trigger : row.triggers ) {
                if ( row.section == keymap_section_t::BINDINGS ) {
                    const QByteArray bytes = trigger.toUtf8(); key_chord_t chord{};
                    display.append( EditorKeyChord_Parse( View( bytes ), &chord ) ? EditorKeyChord_ToKeySequence( chord ).toString( QKeySequence::NativeText ) : trigger );
                } else { display.append( trigger ); }
            }
            auto *pItem = new QTreeWidgetItem( m_pRows );
            pItem->setText( 0, row.label ); pItem->setText( 1, SectionLabel( row.section ) ); pItem->setText( 2, row.context );
            pItem->setText( 3, display.isEmpty() ? QStringLiteral( "Unbound" ) : display.join( QStringLiteral( " / " ) ) );
            pItem->setText( 4, row.origin ); pItem->setText( 5, row.availability ); pItem->setData( 0, Qt::UserRole, static_cast<int>( i ) );
            for ( int column = 0; column < 6; ++column ) { pItem->setToolTip( column, row.id + QLatin1Char( '\n' ) + row.description + QLatin1Char( '\n' ) +
                                                                       row.triggers.join( QStringLiteral( " / " ) ) ); }
            if ( row.Key() == m_selectedKey ) { pSelected = pItem; }
        }
        if ( pSelected == nullptr && m_pRows->topLevelItemCount() != 0 ) { pSelected = m_pRows->topLevelItem( 0 ); }
        m_pRows->setCurrentItem( pSelected ); SelectRow( pSelected );
    }

    void SelectRow( QTreeWidgetItem *pItem )
    {
        if ( pItem == nullptr ) { m_pDetails->setText( QStringLiteral( "No matching actions. Change the category or search; enter a context and action ID to add a declaration." ) ); return; }
        const auto &row = m_rows[static_cast<usize>( pItem->data( 0, Qt::UserRole ).toInt() )];
        m_selectedKey = row.Key(); m_pContext->setCurrentText( row.context ); m_pAction->setCurrentText( row.id );
        m_pKind->setCurrentIndex( m_pKind->findData( static_cast<int>( row.section ) ) );
        for ( int i = 0; i < 4; ++i ) { m_pTriggers[i]->setText( i < row.triggers.size() ? row.triggers[i] : QString() ); }
        m_pDetails->setText( row.id + QStringLiteral( " · " ) + row.availability + QLatin1Char( '\n' ) + row.description ); RefreshRecordButtons();
    }

    void RefreshRecordButtons()
    {
        for ( auto *pButton : m_pRecord ) { pButton->setVisible( Kind() != keymap_section_t::MOUSE ); }
    }

    void Record( int slot )
    {
        QDialog dialog( this ); dialog.setObjectName( QStringLiteral( "KeymapRecordDialog" ) );
        dialog.setWindowTitle( QStringLiteral( "Record %1" ).arg( SectionLabel( Kind() ) ) );
        auto *pLayout = new QVBoxLayout( &dialog );
        pLayout->addWidget( new QLabel( QStringLiteral( "Record one alternative, then accept it. The binding remains staged until Set binding and Apply." ), &dialog ) );
        QKeySequenceEdit *pKeyboard = nullptr; held_capture_t *pHeld = nullptr;
        if ( Kind() == keymap_section_t::HELD ) { pHeld = new held_capture_t( &dialog ); pLayout->addWidget( pHeld ); pHeld->setFocus(); }
        else { pKeyboard = new QKeySequenceEdit( &dialog ); pKeyboard->setMaximumSequenceLength( EDITOR_KEY_CHORD_MAX_STROKES );
            pKeyboard->setFinishingKeyCombinations( {} ); // Tab/Shift+Tab are valid viewport bindings.
            pLayout->addWidget( pKeyboard ); pKeyboard->setFocus(); }
        auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog ); pLayout->addWidget( pButtons );
        QObject::connect( pButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        QObject::connect( pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        if ( dialog.exec() != QDialog::Accepted ) { return; }
        QString text;
        if ( pHeld != nullptr ) { text = pHeld->text(); }
        else {
            key_chord_t chord{}; char buffer[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
            if ( EditorKeyChord_FromKeySequence( pKeyboard->keySequence(), &chord ) ) {
                text = QString::fromUtf8( buffer, static_cast<qsizetype>( EditorKeyChord_Format( chord, buffer ) ) );
            }
        }
        if ( !text.isEmpty() ) { m_pTriggers[slot]->setText( text ); }
    }

    void UpdateState()
    {
        const bool changed = HasChanges(); m_pApply->setEnabled( changed ); m_pRevert->setEnabled( changed );
    }

    bool ConfirmDiscard()
    {
        return !isVisible() || QMessageBox::question( this, QStringLiteral( "Discard staged keybindings?" ),
                                                      QStringLiteral( "Starting from another profile discards this profile's unapplied working copy." ),
                                                      QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel ) == QMessageBox::Discard;
    }

    bool Fail( const QString &message ) { m_pStatus->setText( message ); UpdateState(); return false; }

    static void OnKeymap( void *pContext ) noexcept
    {
        auto &self = *static_cast<keymap_settings_t *>( pContext );
        if ( self.m_publishing ) { return; }
        QByteArray current;
        const bool userChanged = self.m_imported || self.m_createdProfile || self.m_pendingSelection ||
                                 self.m_pId->text() != self.m_defaultId || self.m_pName->text() != self.m_defaultName ||
                                 !self.m_draft || !Serialize( *self.m_draft, current ) || current != self.m_snapshot;
        if ( !userChanged ) { self.Revert(); }
        else { self.FillPresets(); self.UpdateState(); self.m_pStatus->setText( QStringLiteral( "The active keymap changed elsewhere; your staged working copy was retained." ) ); }
    }

    editor_gui_t *m_pGui{ nullptr };
    QString m_folder, m_sourceId, m_defaultId, m_defaultName, m_selectedKey;
    std::unique_ptr<settings_document_t> m_draft;
    QByteArray m_snapshot;
    bool m_imported{ false }, m_createdProfile{ false }, m_pendingSelection{ false }, m_subscribed{ false }, m_publishing{ false };
    void ( *m_pReferenceCallback )( void * ){ nullptr };
    void *m_pReferenceContext{ nullptr };
    std::vector<binding_row_t> m_rows;
    QComboBox *m_pPresets{ nullptr }, *m_pPlatform{ nullptr }, *m_pContext{ nullptr }, *m_pAction{ nullptr }, *m_pKind{ nullptr };
    QLineEdit *m_pId{ nullptr }, *m_pName{ nullptr }, *m_pSearch{ nullptr }, *m_pTriggers[4]{};
    QTreeWidget *m_pCategories{ nullptr }, *m_pRows{ nullptr };
    QLabel *m_pBase{ nullptr }, *m_pDetails{ nullptr }, *m_pStatus{ nullptr };
    QPushButton *m_pRecord[4]{}, *m_pApply{ nullptr }, *m_pRevert{ nullptr }, *m_pReference{ nullptr };
};

keymap_settings_t *Page( QWidget *pPage ) { return dynamic_cast<keymap_settings_t *>( pPage ); }

} // namespace

QWidget *EditorKeymapSettings_Create( QWidget *pParent, editor_gui_t *pGui, const QString &folder )
{
    return pGui != nullptr && pGui->bInitialized ? new keymap_settings_t( pParent, pGui, folder ) : nullptr;
}
QStringList EditorKeymapSettings_Keywords()
{
    return { QStringLiteral( "keybindings" ), QStringLiteral( "keyboard" ), QStringLiteral( "shortcuts" ), QStringLiteral( "camera" ),
             QStringLiteral( "mouse" ), QStringLiteral( "held" ), QStringLiteral( "contexts" ), QStringLiteral( "presets" ), QStringLiteral( "profiles" ),
             QStringLiteral( "Hammer" ), QStringLiteral( "TrenchBroom" ), QStringLiteral( "cykeymap" ), QStringLiteral( "remap" ) };
}
bool EditorKeymapSettings_CanClose( QWidget *pPage ) { auto *p = Page( pPage ); return p != nullptr && p->CanClose(); }
bool EditorKeymapSettings_SelectKeymap( QWidget *pPage, const QString &id ) { auto *p = Page( pPage ); return p != nullptr && p->Select( id ); }
QString EditorKeymapSettings_CurrentKeymap( QWidget *pPage ) { auto *p = Page( pPage ); return p != nullptr ? p->Current() : QString(); }
bool EditorKeymapSettings_NewProfile( QWidget *pPage, const QString &id, const QString &name, const QString &baseId )
{
    auto *p = Page( pPage ); return p != nullptr && p->NewProfile( id, name, baseId );
}
bool EditorKeymapSettings_DuplicateProfile( QWidget *pPage, const QString &id, const QString &name )
{
    auto *p = Page( pPage ); return p != nullptr && p->DuplicateProfile( id, name );
}
void EditorKeymapSettings_SetReferenceCallback( QWidget *pPage, void ( *pCallback )( void * ), void *pContext )
{
    if ( auto *p = Page( pPage ) ) { p->SetReferenceCallback( pCallback, pContext ); }
}
void EditorKeymapSettings_SetFilter( QWidget *pPage, const QString &text ) { if ( auto *p = Page( pPage ) ) { p->SetFilter( text ); } }
void EditorKeymapSettings_SetPlatform( QWidget *pPage, keymap_platform_t platform ) { if ( auto *p = Page( pPage ) ) { p->SetPlatform( platform ); } }
QStringList EditorKeymapSettings_Rows( QWidget *pPage ) { auto *p = Page( pPage ); return p != nullptr ? p->Rows() : QStringList(); }
bool EditorKeymapSettings_SetTriggers( QWidget *pPage, keymap_section_t section, keymap_platform_t platform, const QString &context,
                                      const QString &id, const QStringList &triggers )
{
    auto *p = Page( pPage ); return p != nullptr && p->SetTriggers( section, platform, context, id, triggers );
}
bool EditorKeymapSettings_Reset( QWidget *pPage, keymap_section_t section, keymap_platform_t platform, const QString &context, const QString &id )
{
    auto *p = Page( pPage ); return p != nullptr && p->Reset( section, platform, context, id );
}
QStringList EditorKeymapSettings_Conflicts( QWidget *pPage ) { auto *p = Page( pPage ); return p != nullptr ? p->Conflicts() : QStringList(); }
bool EditorKeymapSettings_HasChanges( QWidget *pPage ) { auto *p = Page( pPage ); return p != nullptr && p->HasChanges(); }
void EditorKeymapSettings_Revert( QWidget *pPage ) { if ( auto *p = Page( pPage ) ) { p->Revert(); } }
QString EditorKeymapSettings_DraftText( QWidget *pPage ) { auto *p = Page( pPage ); return p != nullptr ? p->DraftText() : QString(); }
bool EditorKeymapSettings_Import( QWidget *pPage, const QString &path ) { auto *p = Page( pPage ); return p != nullptr && p->Import( path ); }
bool EditorKeymapSettings_Export( QWidget *pPage, const QString &path ) { auto *p = Page( pPage ); return p != nullptr && p->Export( path ); }
bool EditorKeymapSettings_ExportPortable( QWidget *pPage, const QString &path ) { auto *p = Page( pPage ); return p != nullptr && p->ExportPortable( path ); }
bool EditorKeymapSettings_Apply( QWidget *pPage, const QString &id, const QString &name ) { auto *p = Page( pPage ); return p != nullptr && p->Apply( id, name ); }
QString EditorKeymapSettings_Status( QWidget *pPage ) { auto *p = Page( pPage ); return p != nullptr ? p->Status() : QString(); }

} // namespace cypher::editor::gui
