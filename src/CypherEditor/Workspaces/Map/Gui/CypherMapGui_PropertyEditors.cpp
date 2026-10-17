//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_PropertyEditors.cpp
//  Purpose: Implements the typed entity property editors of Object
//           Properties.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_PropertyEditors.h"

#include "CypherMapGui_Game.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_ObjectTools.h"
#include "CypherEditorGui_Style.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_Wireframe.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cypher::editor::map
{
using namespace common;

namespace
{
const QString kMixed = QStringLiteral( "Multiple values" );

struct document_delete_t {
    void operator()( key_value_document_t *pDocument ) const noexcept { KeyValue_DestroyDocument( pDocument ); }
};

map_property_value_t NewValue()
{
    key_value_document_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    key_value_document_t *pDocument = KeyValue_CreateDocument( desc );
    return pDocument != nullptr ? map_property_value_t( pDocument, document_delete_t{} ) : nullptr;
}

const key_value_t *Root( const map_property_value_t &value ) { return value != nullptr ? KeyValue_Root( value.get() ) : nullptr; }

QString Text( const key_value_t *pValue )
{
    string_view_t text{};
    return KeyValue_GetString( pValue, &text ) ? QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) : QString();
}

QString NumberText( f64 value, bool integer )
{
    return integer ? QString::number( static_cast<qlonglong>( std::llround( value ) ) ) : QString::number( value, 'g', 15 );
}

map_property_value_t Strings( const QStringList &items )
{
    map_property_value_t value = NewValue();
    if ( value == nullptr || !KeyValue_SetRootType( value.get(), key_value_type_t::ARRAY ) ) { return nullptr; }
    for ( const QString &item : items ) {
        const QByteArray utf8 = item.toUtf8();
        key_value_t *pItem = KeyValue_ArrayAppend( value.get(), KeyValue_Root( value.get() ), key_value_type_t::STRING );
        if ( pItem == nullptr || !KeyValue_SetString( value.get(), pItem, { utf8.constData(), static_cast<usize>( utf8.size() ) } ) ) { return nullptr; }
    }
    return value;
}

map_property_value_t Reals( const f64 *pValues, usize nValues )
{
    map_property_value_t value = NewValue();
    if ( value == nullptr || !KeyValue_SetRootType( value.get(), key_value_type_t::ARRAY ) ) { return nullptr; }
    for ( usize i = 0; i < nValues; ++i ) {
        key_value_t *pItem = KeyValue_ArrayAppend( value.get(), KeyValue_Root( value.get() ), key_value_type_t::F64 );
        if ( pItem == nullptr || !KeyValue_SetF64( value.get(), pItem, pValues[i] ) ) { return nullptr; }
    }
    return value;
}

map_property_value_t Id( u64 id )
{
    map_property_value_t value = NewValue();
    return value != nullptr && KeyValue_SetU64( value.get(), KeyValue_Root( value.get() ), id ) ? value : nullptr;
}

// What an editor needs once built. Nothing here points into the live map:
// a commit replaces the document, and a dying editor may still finish
// editing while the panel rebuilds.
struct editor_state_t {
    map_workspace_t *pWorkspace{ nullptr };
    map_game_property_t property{};
    map_property_value_t start{}; // An array holding what the editor started from (none when empty).
    bool bMixed{ false };
    bool bTouched{ false };       // The user changed something (what decides for mixed values).
    map_property_commit_fn commit{};
    map_property_value_t offered{};

    const key_value_t *Start() const { return KeyValue_ChildCount( Root( start ) ) != 0u ? KeyValue_ChildAt( Root( start ), 0u ) : nullptr; }

    // Commits the value unless it is what is already there or was just
    // offered (a combo box reports Enter both as a pick and as editing
    // finished).
    void Offer( const map_property_value_t &value )
    {
        const key_value_t *pValue = Root( value );
        if ( pValue == nullptr || !commit ) { return; }
        if ( bMixed ? !bTouched : ( Start() != nullptr && MapEntityEdit_ValuesEqual( Start(), pValue ) ) ) { return; }
        if ( offered != nullptr && MapEntityEdit_ValuesEqual( Root( offered ), pValue ) ) { return; }
        offered = value;
        commit( value );
    }
};
using state_t = std::shared_ptr<editor_state_t>;

state_t MakeState( const map_property_editor_args_t &args )
{
    auto state = std::make_shared<editor_state_t>();
    state->pWorkspace = args.pWorkspace;
    state->property = *args.pProperty;
    state->property.pDefault = nullptr; // Points into the profile, which a reload replaces.
    state->bMixed = args.bMixed;
    state->commit = args.commit;
    state->start = NewValue();
    const key_value_t *pShown = args.pValue != nullptr ? args.pValue : args.pProperty->pDefault;
    if ( state->start != nullptr && KeyValue_SetRootType( state->start.get(), key_value_type_t::ARRAY ) && pShown != nullptr ) {
        ( void )KeyValue_CloneInto( state->start.get(), KeyValue_Root( state->start.get() ), pShown );
    }
    return state;
}

// A dropdown inside a scrolling grid: the wheel scrolls the grid unless the
// dropdown has focus, and its items can be filled when it is first opened
// or focused (listing content folders on every rebuild would be wasted).
class map_combo_t final : public QComboBox {
public:
    explicit map_combo_t( QWidget *pParent ) : QComboBox( pParent )
    {
        setFocusPolicy( Qt::StrongFocus );
        setFrame( false );
        setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
        setMinimumContentsLength( 4 );
    }

    std::function<QStringList()> fill{};

    void showPopup() override { Fill(); QComboBox::showPopup(); }

    // Adds the items without changing what the field shows.
    void Fill()
    {
        if ( !fill ) { return; }
        const QStringList items = fill();
        fill = {};
        const QString text = currentText();
        const QSignalBlocker blocker( this );
        for ( const QString &item : items ) { if ( findText( item ) < 0 ) { addItem( item ); } }
        if ( isEditable() ) { setCurrentIndex( findText( text ) ); setEditText( text ); }
    }

protected:
    void wheelEvent( QWheelEvent *pEvent ) override
    {
        if ( !hasFocus() ) { pEvent->ignore(); return; }
        QComboBox::wheelEvent( pEvent );
    }
    void focusInEvent( QFocusEvent *pEvent ) override { Fill(); QComboBox::focusInEvent( pEvent ); }
};

// A number field: digits as typed, Up/Down step by one (Shift ten, Ctrl a
// tenth for reals), the wheel steps while it has focus; the limits apply to
// the value read back.
class map_number_edit_t final : public QLineEdit {
public:
    map_number_edit_t( QWidget *pParent, bool integer, f64 min, f64 max ) : QLineEdit( pParent ), m_bInteger( integer ), m_min( min ), m_max( max )
    {
        setFrame( false );
        setValidator( new QRegularExpressionValidator(
            QRegularExpression( integer ? QStringLiteral( "^[-+]?\\d*$" ) : QStringLiteral( "^[-+]?\\d*\\.?\\d*([eE][-+]?\\d*)?$" ) ), this ) );
    }

    bool Value( f64 *pOut ) const
    {
        bool ok = false;
        const f64 value = text().toDouble( &ok );
        if ( !ok || !std::isfinite( value ) ) { return false; }
        *pOut = std::clamp( m_bInteger ? std::round( value ) : value, m_min, m_max );
        return true;
    }
    void SetValue( f64 value ) { setText( NumberText( value, m_bInteger ) ); }

    std::function<void()> stepped{}; // Up/Down and the wheel changed the value.

protected:
    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->key() == Qt::Key_Up || pEvent->key() == Qt::Key_Down ) {
            const f64 sign = pEvent->key() == Qt::Key_Up ? 1.0 : -1.0;
            const f64 step = ( pEvent->modifiers() & Qt::ShiftModifier ) ? 10.0 : ( !m_bInteger && ( pEvent->modifiers() & Qt::ControlModifier ) ) ? 0.1 : 1.0;
            Step( sign * step );
            return;
        }
        QLineEdit::keyPressEvent( pEvent );
    }
    void wheelEvent( QWheelEvent *pEvent ) override
    {
        if ( !hasFocus() || pEvent->angleDelta().y() == 0 ) { pEvent->ignore(); return; }
        Step( pEvent->angleDelta().y() > 0 ? 1.0 : -1.0 );
    }

private:
    void Step( f64 delta )
    {
        f64 value = 0.0;
        if ( !Value( &value ) ) { value = std::clamp( 0.0, m_min, m_max ); }
        SetValue( std::clamp( value + delta, m_min, m_max ) );
        if ( stepped ) { stepped(); }
    }

    bool m_bInteger{ false };
    f64 m_min{ 0.0 }, m_max{ 0.0 };
};

// A row of widgets that fills the cell: what a property editor sits in.
QWidget *Row( QWidget *pParent, QHBoxLayout **ppLayout )
{
    auto *pRow = new QWidget( pParent );
    auto *pLayout = new QHBoxLayout( pRow );
    pLayout->setContentsMargins( 0, 0, 2, 0 );
    pLayout->setSpacing( 4 );
    *ppLayout = pLayout;
    return pRow;
}

QLabel *Unit( QWidget *pParent, const std::string &unit )
{
    auto *pLabel = new QLabel( QString::fromStdString( unit ), pParent );
    pLabel->setProperty( "muted", true );
    return pLabel;
}

f64 Lower( const map_game_property_t &p, bool integer ) { return p.bMin ? p.min : integer ? static_cast<f64>( std::numeric_limits<i32>::min() ) : -1e12; }
f64 Upper( const map_game_property_t &p, bool integer ) { return p.bMax ? p.max : integer ? static_cast<f64>( std::numeric_limits<i32>::max() ) : 1e12; }

// Fusion's box frame nearly vanishes on the dark grid rows: outline it, so
// an unticked box reads as a box.
class map_check_t final : public QCheckBox {
public:
    explicit map_check_t( QWidget *pParent ) : QCheckBox( pParent ) {}

protected:
    void paintEvent( QPaintEvent *pEvent ) override
    {
        QCheckBox::paintEvent( pEvent );
        QStyleOptionButton option;
        initStyleOption( &option );
        const QRect box = style()->subElementRect( QStyle::SE_CheckBoxIndicator, &option, this ).adjusted( 0, 0, -1, -1 );
        QPainter painter( this );
        QColor frame = palette().color( isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Text );
        frame.setAlpha( underMouse() ? 200 : 120 );
        painter.setPen( frame );
        painter.setBrush( Qt::NoBrush );
        painter.drawRect( box );
    }
};

QWidget *BoolEditor( QWidget *pParent, const state_t &state )
{
    auto *pBox = new map_check_t( pParent );
    pBox->setObjectName( QStringLiteral( "MapPropertyBool" ) );
    bool_t value = CY_FALSE;
    const bool has = KeyValue_GetBool( state->Start(), &value );
    if ( state->bMixed ) {
        pBox->setTristate( true );
        pBox->setCheckState( Qt::PartiallyChecked );
        pBox->setText( kMixed );
    } else {
        pBox->setChecked( has && value );
    }
    QObject::connect( pBox, &QCheckBox::clicked, pBox, [state, pBox]( bool ) {
        state->bTouched = true;
        state->Offer( MapPropertyValue_Bool( pBox->checkState() == Qt::Checked ) );
    } );
    return pBox;
}

QWidget *NumberEditor( QWidget *pParent, const state_t &state, bool integer )
{
    QHBoxLayout *pLayout = nullptr;
    QWidget *pRow = Row( pParent, &pLayout );
    const map_game_property_t &p = state->property;
    auto *pEdit = new map_number_edit_t( pRow, integer, Lower( p, integer ), Upper( p, integer ) );
    pEdit->setObjectName( integer ? QStringLiteral( "MapPropertyInteger" ) : QStringLiteral( "MapPropertyReal" ) );
    f64 value = 0.0;
    if ( state->bMixed ) { pEdit->setPlaceholderText( kMixed ); }
    else if ( MapPropertyValue_Number( state->Start(), &value ) ) { pEdit->SetValue( value ); }
    pLayout->addWidget( pEdit, 1 );
    if ( !p.unit.empty() ) { pLayout->addWidget( Unit( pRow, p.unit ) ); }
    const auto finish = [state, pEdit, integer]() {
        f64 number = 0.0;
        if ( !pEdit->Value( &number ) ) { return; } // Empty or unreadable: nothing changes.
        pEdit->SetValue( number );                  // Shows what the limits made of it.
        state->Offer( integer ? MapPropertyValue_Integer( static_cast<i64>( number ) ) : MapPropertyValue_Real( number ) );
    };
    QObject::connect( pEdit, &QLineEdit::textEdited, pEdit, [state]( const QString & ) { state->bTouched = true; } );
    pEdit->stepped = [state]() { state->bTouched = true; };
    QObject::connect( pEdit, &QLineEdit::editingFinished, pEdit, finish );
    return pRow;
}

QWidget *StringEditor( QWidget *pParent, const state_t &state )
{
    auto *pEdit = new QLineEdit( pParent );
    pEdit->setObjectName( QStringLiteral( "MapPropertyString" ) );
    pEdit->setFrame( false );
    if ( state->bMixed ) { pEdit->setPlaceholderText( kMixed ); }
    else { pEdit->setText( Text( state->Start() ) ); }
    QObject::connect( pEdit, &QLineEdit::textEdited, pEdit, [state]( const QString & ) { state->bTouched = true; } );
    QObject::connect( pEdit, &QLineEdit::editingFinished, pEdit, [state, pEdit]() { state->Offer( MapPropertyValue_String( pEdit->text() ) ); } );
    return pEdit;
}

QWidget *ChoiceEditor( QWidget *pParent, const state_t &state )
{
    auto *pCombo = new map_combo_t( pParent );
    pCombo->setObjectName( QStringLiteral( "MapPropertyChoice" ) );
    for ( const std::string &choice : state->property.choices ) {
        const QString text = QString::fromStdString( choice );
        pCombo->addItem( text, text );
    }
    const QString current = Text( state->Start() );
    if ( state->bMixed ) {
        pCombo->insertItem( 0, kMixed, QVariant() );
        pCombo->setCurrentIndex( 0 );
    } else {
        int index = pCombo->findData( current );
        if ( index < 0 && !current.isEmpty() ) {
            // Kept as written, and shown for what it is.
            pCombo->addItem( QStringLiteral( "%1 (not a choice)" ).arg( current ), current );
            index = pCombo->count() - 1;
        }
        pCombo->setCurrentIndex( index );
    }
    QObject::connect( pCombo, &QComboBox::activated, pCombo, [state, pCombo]( int index ) {
        const QVariant value = pCombo->itemData( index );
        if ( !value.isValid() ) { return; } // The "Multiple values" line.
        state->bTouched = true;
        state->Offer( MapPropertyValue_String( value.toString() ) );
    } );
    return pCombo;
}

// Keeps the flags menu open while boxes are ticked: one commit when it closes.
class map_keep_open_t final : public QObject {
public:
    explicit map_keep_open_t( QMenu *pMenu ) : QObject( pMenu ), m_pMenu( pMenu ) {}

protected:
    bool eventFilter( QObject *pObject, QEvent *pEvent ) override
    {
        if ( pObject == m_pMenu && pEvent->type() == QEvent::MouseButtonRelease ) {
            QAction *pAction = m_pMenu->actionAt( static_cast<QMouseEvent *>( pEvent )->position().toPoint() );
            if ( pAction != nullptr && pAction->isCheckable() && pAction->isEnabled() ) { pAction->toggle(); return true; }
        }
        return QObject::eventFilter( pObject, pEvent );
    }

private:
    QMenu *m_pMenu;
};

QWidget *FlagsEditor( QWidget *pParent, const state_t &state )
{
    auto *pButton = new QPushButton( pParent );
    pButton->setObjectName( QStringLiteral( "MapPropertyFlags" ) );
    pButton->setFlat( true );
    pButton->setStyleSheet( QStringLiteral( "text-align: left; padding-left: 2px;" ) );
    QStringList flags;
    for ( const key_value_t *pFlag = KeyValue_FirstChild( state->Start() ); pFlag != nullptr; pFlag = KeyValue_NextSibling( pFlag ) ) { flags.append( Text( pFlag ) ); }
    pButton->setText( state->bMixed ? kMixed : flags.isEmpty() ? QStringLiteral( "None" ) : flags.join( QStringLiteral( ", " ) ) );
    pButton->setProperty( "flags", flags );
    pButton->setToolTip( QStringLiteral( "Click to tick flags; the change is written when the list closes." ) );
    // The menu runs inside the click: the panel cannot rebuild under it, and
    // the commit is queued until after it closes.
    QObject::connect( pButton, &QPushButton::clicked, pButton, [state, pButton]() {
        const QStringList before = pButton->property( "flags" ).toStringList();
        QMenu menu( pButton );
        menu.setObjectName( QStringLiteral( "MapPropertyFlagsMenu" ) );
        menu.installEventFilter( new map_keep_open_t( &menu ) );
        for ( const std::string &choice : state->property.choices ) {
            QAction *pAction = menu.addAction( QString::fromStdString( choice ) );
            pAction->setCheckable( true );
            pAction->setChecked( before.contains( pAction->text() ) );
        }
        menu.exec( pButton->mapToGlobal( QPoint( 0, pButton->height() ) ) );
        QStringList after;
        for ( QAction *pAction : menu.actions() ) { if ( pAction->isChecked() ) { after.append( pAction->text() ); } }
        if ( after == before && !state->bMixed ) { return; }
        state->bTouched = state->bTouched || after != before;
        state->Offer( Strings( after ) );
    } );
    return pButton;
}

QIcon Swatch( const QColor &colour )
{
    QPixmap pixmap( 14, 14 );
    pixmap.fill( Qt::transparent );
    QPainter painter( &pixmap );
    painter.setPen( QColor( 0, 0, 0, 160 ) );
    painter.setBrush( colour.isValid() ? colour : QColor( Qt::transparent ) );
    painter.drawRect( 0, 0, 13, 13 );
    if ( !colour.isValid() ) { painter.drawLine( 0, 13, 13, 0 ); }
    return QIcon( pixmap );
}

QWidget *ColorEditor( QWidget *pParent, const state_t &state )
{
    QHBoxLayout *pLayout = nullptr;
    QWidget *pRow = Row( pParent, &pLayout );
    auto *pSwatch = new QToolButton( pRow );
    pSwatch->setObjectName( QStringLiteral( "MapPropertyColorSwatch" ) );
    pSwatch->setAutoRaise( true );
    pSwatch->setToolTip( QStringLiteral( "Pick a colour" ) );
    auto *pHex = new QLineEdit( pRow );
    pHex->setObjectName( QStringLiteral( "MapPropertyColor" ) );
    pHex->setFrame( false );
    pHex->setValidator( new QRegularExpressionValidator( QRegularExpression( QStringLiteral( "^#?[0-9a-fA-F]{0,6}$" ) ), pHex ) );
    const QColor start( Text( state->Start() ) );
    if ( state->bMixed ) { pHex->setPlaceholderText( kMixed ); }
    else { pHex->setText( start.isValid() ? start.name() : Text( state->Start() ) ); }
    pSwatch->setIcon( Swatch( state->bMixed ? QColor() : start ) );
    pLayout->addWidget( pSwatch );
    pLayout->addWidget( pHex, 1 );
    QObject::connect( pHex, &QLineEdit::textEdited, pHex, [state]( const QString & ) { state->bTouched = true; } );
    QObject::connect( pHex, &QLineEdit::editingFinished, pHex, [state, pHex, pSwatch]() {
        QString text = pHex->text().trimmed();
        if ( !text.startsWith( QLatin1Char( '#' ) ) ) { text.prepend( QLatin1Char( '#' ) ); }
        const QColor colour( text );
        if ( text.size() != 7 || !colour.isValid() ) { return; }
        pHex->setText( colour.name() );
        pSwatch->setIcon( Swatch( colour ) );
        state->Offer( MapPropertyValue_String( colour.name() ) );
    } );
    QObject::connect( pSwatch, &QToolButton::clicked, pSwatch, [state, pHex, pSwatch]() {
        const QColor current( pHex->text() );
        const QColor colour = QColorDialog::getColor( current.isValid() ? current : QColor( Qt::white ), pSwatch, QStringLiteral( "Colour" ) );
        if ( !colour.isValid() ) { return; } // Cancelled.
        pHex->setText( colour.name() );
        pSwatch->setIcon( Swatch( colour ) );
        state->bTouched = true;
        state->Offer( MapPropertyValue_String( colour.name() ) );
    } );
    return pRow;
}

QWidget *VectorEditor( QWidget *pParent, const state_t &state, bool angles )
{
    QHBoxLayout *pLayout = nullptr;
    QWidget *pRow = Row( pParent, &pLayout );
    pRow->setObjectName( angles ? QStringLiteral( "MapPropertyAngles" ) : QStringLiteral( "MapPropertyVector" ) );
    const QString axes[3]{ angles ? QStringLiteral( "P" ) : QStringLiteral( "X" ), QStringLiteral( "Y" ), angles ? QStringLiteral( "R" ) : QStringLiteral( "Z" ) };
    const QString names[3]{ angles ? QStringLiteral( "Pitch" ) : QStringLiteral( "X" ), angles ? QStringLiteral( "Yaw" ) : QStringLiteral( "Y" ), angles ? QStringLiteral( "Roll" ) : QStringLiteral( "Z" ) };
    std::array<map_number_edit_t *, 3> edits{};
    f64 start[3]{};
    for ( usize i = 0; i < 3u; ++i ) {
        ( void )MapPropertyValue_Number( KeyValue_ChildAt( state->Start(), i ), &start[i] );
        auto *pAxis = new QLabel( axes[i], pRow );
        pAxis->setProperty( "muted", true );
        edits[i] = new map_number_edit_t( pRow, false, -1e12, 1e12 );
        edits[i]->setObjectName( QStringLiteral( "MapPropertyAxis%1" ).arg( i ) );
        edits[i]->setToolTip( names[i] );
        if ( state->bMixed ) { edits[i]->setPlaceholderText( QStringLiteral( "-" ) ); }
        else { edits[i]->SetValue( start[i] ); }
        pLayout->addWidget( pAxis );
        pLayout->addWidget( edits[i], 1 );
    }
    if ( !state->property.unit.empty() ) { pLayout->addWidget( Unit( pRow, state->property.unit ) ); }
    else if ( angles ) { pLayout->addWidget( Unit( pRow, "°" ) ); }
    for ( usize i = 0; i < 3u; ++i ) {
        QObject::connect( edits[i], &QLineEdit::textEdited, edits[i], [state]( const QString & ) { state->bTouched = true; } );
        edits[i]->stepped = [state]() { state->bTouched = true; };
        // A field left empty (a mixed value never typed in) keeps the first
        // entity's component.
        QObject::connect( edits[i], &QLineEdit::editingFinished, edits[i], [state, edits, start]() {
            f64 values[3]{};
            for ( usize k = 0; k < 3u; ++k ) { if ( !edits[k]->Value( &values[k] ) ) { values[k] = start[k]; } }
            state->Offer( Reals( values, 3u ) );
        } );
    }
    return pRow;
}

// Asset and target pickers: a field to type in, a list to pick from.
map_combo_t *PickerCombo( QWidget *pParent, const state_t &state, const QString &name )
{
    auto *pCombo = new map_combo_t( pParent );
    pCombo->setObjectName( name );
    pCombo->setEditable( true );
    pCombo->setInsertPolicy( QComboBox::NoInsert );
    pCombo->completer()->setFilterMode( Qt::MatchContains );
    pCombo->completer()->setCaseSensitivity( Qt::CaseInsensitive );
    pCombo->completer()->setCompletionMode( QCompleter::PopupCompletion );
    if ( state->bMixed ) { pCombo->lineEdit()->setPlaceholderText( kMixed ); }
    else { pCombo->setEditText( Text( state->Start() ) ); }
    QObject::connect( pCombo->lineEdit(), &QLineEdit::textEdited, pCombo, [state]( const QString & ) { state->bTouched = true; } );
    QObject::connect( pCombo, &QComboBox::activated, pCombo, [state, pCombo]( int index ) {
        state->bTouched = true;
        state->Offer( MapPropertyValue_String( pCombo->itemText( index ) ) );
    } );
    QObject::connect( pCombo->lineEdit(), &QLineEdit::editingFinished, pCombo, [state, pCombo]() {
        state->Offer( MapPropertyValue_String( pCombo->currentText().trimmed() ) );
    } );
    return pCombo;
}

QWidget *AssetEditor( QWidget *pParent, const state_t &state )
{
    map_combo_t *pCombo = PickerCombo( pParent, state, QStringLiteral( "MapPropertyAsset" ) );
    QStringList extensions;
    for ( const std::string &extension : state->property.extensions ) { extensions.append( QString::fromStdString( extension ) ); }
    map_workspace_t *ws = state->pWorkspace;
    pCombo->fill = [ws, extensions]() { return MapAssets_List( ws, extensions ); };
    if ( !extensions.isEmpty() ) { pCombo->lineEdit()->setToolTip( QStringLiteral( "Content files: %1" ).arg( extensions.join( QStringLiteral( ", " ) ) ) ); }
    return pCombo;
}

QWidget *TargetEditor( QWidget *pParent, const state_t &state )
{
    QHBoxLayout *pLayout = nullptr;
    QWidget *pRow = Row( pParent, &pLayout );
    map_combo_t *pCombo = PickerCombo( pRow, state, QStringLiteral( "MapPropertyTarget" ) );
    // Hammer's Pick Entity: the eyedropper takes the next entity clicked in a view.
    auto *pPick = new QToolButton( pRow );
    pPick->setObjectName( QStringLiteral( "MapPropertyPickEntity" ) );
    pPick->setAutoRaise( true );
    pPick->setIcon( gui::EditorStyle_Icon( state->pWorkspace->pGui->style, "tool-eyedropper" ) );
    pPick->setToolTip( QStringLiteral( "Pick the target: click the entity in a view" ) );
    pLayout->addWidget( pCombo, 1 );
    pLayout->addWidget( pPick );
    QPointer<map_combo_t> combo( pCombo );
    QObject::connect( pPick, &QToolButton::clicked, pPick, [state, pPick, combo]() {
        MapObjectTools_BeginEntityPick( state->pWorkspace, pPick, [state, combo]( u64, const QString &name ) {
            if ( !combo.isNull() ) { combo->setEditText( name ); }
            state->bTouched = true;
            state->Offer( MapPropertyValue_String( name ) );
        } );
    } );
    std::vector<std::string> classes = state->property.targetClasses;
    map_workspace_t *ws = state->pWorkspace;
    pCombo->fill = [ws, classes]() {
        QStringList names;
        for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
            const map_wire_entity_t &entity = ws->wire.entities.pData[i];
            if ( entity.name[0] == '\0' ) { continue; }
            if ( !classes.empty() && std::find( classes.begin(), classes.end(), std::string( entity.className ) ) == classes.end() ) { continue; }
            const QString name = QString::fromUtf8( entity.name );
            if ( !names.contains( name ) ) { names.append( name ); }
        }
        names.sort( Qt::CaseInsensitive );
        return names;
    };
    return pRow;
}

QWidget *ObjectEditor( QWidget *pParent, const state_t &state )
{
    auto *pEdit = new QLineEdit( pParent );
    pEdit->setObjectName( QStringLiteral( "MapPropertyObject" ) );
    pEdit->setFrame( false );
    pEdit->setValidator( new QRegularExpressionValidator( QRegularExpression( QStringLiteral( "^#?\\d*$" ) ), pEdit ) );
    pEdit->setPlaceholderText( state->bMixed ? kMixed : QStringLiteral( "object ID" ) );
    u64 id = 0u;
    if ( !state->bMixed && KeyValue_GetU64( state->Start(), &id ) && id != 0u ) { pEdit->setText( QString::number( id ) ); }
    QObject::connect( pEdit, &QLineEdit::textEdited, pEdit, [state]( const QString & ) { state->bTouched = true; } );
    QObject::connect( pEdit, &QLineEdit::editingFinished, pEdit, [state, pEdit]() {
        QString text = pEdit->text().trimmed();
        if ( text.startsWith( QLatin1Char( '#' ) ) ) { text.remove( 0, 1 ); }
        bool ok = false;
        const u64 value = text.toULongLong( &ok );
        if ( ok ) { state->Offer( Id( value ) ); }
    } );
    return pEdit;
}
} // namespace

map_property_value_t MapPropertyValue_Bool( bool value )
{
    map_property_value_t out = NewValue();
    return out != nullptr && KeyValue_SetBool( out.get(), KeyValue_Root( out.get() ), value ? CY_TRUE : CY_FALSE ) ? out : nullptr;
}

map_property_value_t MapPropertyValue_Integer( i64 value )
{
    map_property_value_t out = NewValue();
    return out != nullptr && KeyValue_SetI64( out.get(), KeyValue_Root( out.get() ), value ) ? out : nullptr;
}

map_property_value_t MapPropertyValue_Real( f64 value )
{
    map_property_value_t out = NewValue();
    return out != nullptr && KeyValue_SetF64( out.get(), KeyValue_Root( out.get() ), value ) ? out : nullptr;
}

map_property_value_t MapPropertyValue_String( const QString &value )
{
    map_property_value_t out = NewValue();
    const QByteArray utf8 = value.toUtf8();
    return out != nullptr && KeyValue_SetString( out.get(), KeyValue_Root( out.get() ), { utf8.constData(), static_cast<usize>( utf8.size() ) } ) ? out : nullptr;
}

bool MapPropertyValue_Number( const key_value_t *pValue, f64 *pOut ) noexcept
{
    i64 nSigned = 0;
    u64 nUnsigned = 0u;
    if ( KeyValue_GetF64( pValue, pOut ) ) { return true; }
    if ( KeyValue_GetI64( pValue, &nSigned ) ) { *pOut = static_cast<f64>( nSigned ); return true; }
    if ( KeyValue_GetU64( pValue, &nUnsigned ) ) { *pOut = static_cast<f64>( nUnsigned ); return true; }
    return false;
}

QWidget *MapPropertyEditor_Create( QWidget *pParent, const map_property_editor_args_t &args )
{
    CY_ASSERT( args.pWorkspace != nullptr && args.pProperty != nullptr );
    const state_t state = MakeState( args );
    QWidget *pEditor = nullptr;
    switch ( args.pProperty->type ) {
        case map_game_type_t::BOOL: pEditor = BoolEditor( pParent, state ); break;
        case map_game_type_t::INTEGER: pEditor = NumberEditor( pParent, state, true ); break;
        case map_game_type_t::REAL: pEditor = NumberEditor( pParent, state, false ); break;
        case map_game_type_t::STRING: pEditor = StringEditor( pParent, state ); break;
        case map_game_type_t::CHOICE: pEditor = ChoiceEditor( pParent, state ); break;
        case map_game_type_t::FLAGS: pEditor = FlagsEditor( pParent, state ); break;
        case map_game_type_t::COLOR: pEditor = ColorEditor( pParent, state ); break;
        case map_game_type_t::VECTOR: pEditor = VectorEditor( pParent, state, false ); break;
        case map_game_type_t::ANGLES: pEditor = VectorEditor( pParent, state, true ); break;
        case map_game_type_t::ASSET: pEditor = AssetEditor( pParent, state ); break;
        case map_game_type_t::TARGET: pEditor = TargetEditor( pParent, state ); break;
        case map_game_type_t::OBJECT:
        case map_game_type_t::SHAPE: pEditor = ObjectEditor( pParent, state ); break;
        case map_game_type_t::OBJECTS:
        case map_game_type_t::FACES: return nullptr; // Lists of IDs: the value dialog.
    }
    if ( pEditor != nullptr && args.pProperty->bReadOnly ) { pEditor->setEnabled( false ); }
    return pEditor;
}

QWidget *MapClassEditor_Create( QWidget *pParent, map_workspace_t *pWorkspace, const QString &className, bool bMixed,
    std::function<void( const QString &className )> commit )
{
    CY_ASSERT( pWorkspace != nullptr );
    auto *pCombo = new map_combo_t( pParent );
    pCombo->setObjectName( QStringLiteral( "MapPropertyClass" ) );
    pCombo->setEditable( true );
    pCombo->setInsertPolicy( QComboBox::NoInsert );
    pCombo->completer()->setFilterMode( Qt::MatchContains );
    pCombo->completer()->setCaseSensitivity( Qt::CaseInsensitive );
    pCombo->completer()->setCompletionMode( QCompleter::PopupCompletion );
    if ( bMixed ) { pCombo->lineEdit()->setPlaceholderText( kMixed ); }
    else { pCombo->setEditText( className ); }
    // Point entities change to point classes, brush entities to solid ones.
    const QByteArray utf8 = className.toUtf8();
    const map_game_class_t *pCurrent = MapGame_Class( pWorkspace, { utf8.constData(), static_cast<usize>( utf8.size() ) } );
    const map_game_kind_t kind = pCurrent != nullptr ? pCurrent->kind : map_game_kind_t::POINT;
    pCombo->fill = [pWorkspace, kind]() {
        QStringList names;
        for ( const map_game_class_t *pClass : MapGame_Placeable( pWorkspace, kind ) ) { names.append( QString::fromStdString( pClass->name ) ); }
        return names;
    };
    auto offered = std::make_shared<QString>( bMixed ? QString() : className );
    const auto offer = [offered, commit]( const QString &text ) {
        const QString name = text.trimmed();
        if ( name.isEmpty() || name == *offered || !commit ) { return; }
        *offered = name;
        commit( name );
    };
    QObject::connect( pCombo, &QComboBox::activated, pCombo, [pCombo, offer]( int index ) { offer( pCombo->itemText( index ) ); } );
    QObject::connect( pCombo->lineEdit(), &QLineEdit::editingFinished, pCombo, [pCombo, offer]() { offer( pCombo->currentText() ); } );
    return pCombo;
}

} // namespace cypher::editor::map
