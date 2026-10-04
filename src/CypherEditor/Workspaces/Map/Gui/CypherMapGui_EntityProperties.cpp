//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Edits authored entity data through the map's prepared undo transactions.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_EntityProperties.h"
#include "CypherMap_EntityEdit.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cypher::editor::map
{
using namespace common;
namespace
{
constexpr usize kValueTextMax = 64u * CY_KIB;
string_view_t View( const QByteArray &text ) { return { text.constData(), static_cast<usize>( text.size() ) }; }
QString Text( string_view_t text ) { return QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ); }
struct owners_t {
    vector_t<u64> ids{};
    explicit owners_t( const allocator_t *allocator ) { (void)Vector_Init( &ids, allocator ); }
    ~owners_t() { Vector_Shutdown( &ids ); }
};
using value_document_ptr_t = std::unique_ptr<key_value_document_t, decltype( &KeyValue_DestroyDocument )>;
struct dialog_watch_t {
    map_workspace_t *workspace{};
    bool changed{}, registered{};
    static void OnChanged( void *context, u32 changes ) noexcept {
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION ) ) != 0u ) { static_cast<dialog_watch_t *>( context )->changed = true; }
    }
    explicit dialog_watch_t( map_workspace_t *ws ) : workspace( ws ) {
        registered = MapWorkspace_AddListener( workspace, &dialog_watch_t::OnChanged, this );
    }
    ~dialog_watch_t() { if ( registered ) { MapWorkspace_RemoveListener( workspace, &dialog_watch_t::OnChanged, this ); } }
};

const key_value_t *PropertyValue( map_document_t *map, u64 id, string_view_t key, bool identity )
{
    const auto *record = MapEntityEdit_FindEntity( map, id );
    return KeyValue_Find( identity ? record : KeyValue_Find( record, StringView_FromCString( "properties" ) ), key );
}
}

const char *MapEntityProperty_TypeName( key_value_type_t type ) noexcept
{
    switch ( type ) {
        case key_value_type_t::NULL_VALUE: return "Null";
        case key_value_type_t::BOOL: return "Boolean";
        case key_value_type_t::I64: return "Integer";
        case key_value_type_t::U64: return "Unsigned integer";
        case key_value_type_t::F64: return "Real number";
        case key_value_type_t::STRING: return "String";
        case key_value_type_t::BINARY: return "Binary (read-only)";
        case key_value_type_t::OBJECT: return "Object (CYKV)";
        case key_value_type_t::ARRAY: return "Array (CYKV)";
    }
    return "Unknown";
}

bool MapEntityProperty_CanEditValue( const key_value_t *value )
{
    if ( value == nullptr || KeyValue_Type( value ) == key_value_type_t::BINARY ) { return false; }
    if ( KeyValue_Type( value ) == key_value_type_t::STRING ) {
        string_view_t text{};
        return KeyValue_GetString( value, &text ) && text.cchLength <= kValueTextMax;
    }
    if ( KeyValue_Type( value ) == key_value_type_t::ARRAY || KeyValue_Type( value ) == key_value_type_t::OBJECT ) {
        key_value_write_options_t options{};
        options.flags = KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS;
        const auto size = KeyValue_WriteValueText( value, options, nullptr, 0u );
        return ( size.status == key_value_write_status_t::OK || size.status == key_value_write_status_t::OUTPUT_TRUNCATED ) && size.cchRequired <= kValueTextMax;
    }
    return true;
}

QString MapEntityProperty_ValueText( const key_value_t *value, bool *editable )
{
    if ( editable != nullptr ) { *editable = false; }
    if ( value == nullptr ) { return QString(); }
    QString text;
    switch ( KeyValue_Type( value ) ) {
        case key_value_type_t::STRING: {
            string_view_t v{}; (void)KeyValue_GetString( value, &v );
            if ( v.cchLength > kValueTextMax ) { return QStringLiteral( "String (%1 bytes; read-only)" ).arg( v.cchLength ); }
            text = Text( v ); break;
        }
        case key_value_type_t::BOOL: { bool_t v{}; (void)KeyValue_GetBool( value, &v ); text = v ? QStringLiteral( "true" ) : QStringLiteral( "false" ); break; }
        case key_value_type_t::I64: { i64 v{}; (void)KeyValue_GetI64( value, &v ); text = QString::number( v ); break; }
        case key_value_type_t::U64: { u64 v{}; (void)KeyValue_GetU64( value, &v ); text = QString::number( v ); break; }
        case key_value_type_t::F64: { f64 v{}; (void)KeyValue_GetF64( value, &v ); text = QString::number( v, 'g', 17 ); break; }
        case key_value_type_t::NULL_VALUE: text = QStringLiteral( "null" ); break;
        case key_value_type_t::BINARY: return QStringLiteral( "Binary data (read-only)" );
        case key_value_type_t::ARRAY:
        case key_value_type_t::OBJECT: {
            key_value_write_options_t options{};
            options.flags = KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS;
            const auto measure = KeyValue_WriteValueText( value, options, nullptr, 0u );
            if ( measure.status != key_value_write_status_t::OUTPUT_TRUNCATED && measure.status != key_value_write_status_t::OK ) { return QStringLiteral( "Value unavailable (read-only)" ); }
            if ( measure.cchRequired > kValueTextMax ) { return QStringLiteral( "%1 (%2 bytes; read-only)" ).arg( QString::fromLatin1( MapEntityProperty_TypeName( KeyValue_Type( value ) ) ) ).arg( measure.cchRequired ); }
            QByteArray buffer( static_cast<qsizetype>( measure.cchRequired + 1u ), '\0' );
            if ( KeyValue_WriteValueText( value, options, buffer.data(), static_cast<usize>( buffer.size() ) ).status != key_value_write_status_t::OK ) { return QStringLiteral( "Value unavailable (read-only)" ); }
            text = QString::fromUtf8( buffer.constData(), static_cast<qsizetype>( measure.cchRequired ) );
            break;
        }
    }
    if ( editable != nullptr ) { *editable = true; }
    return text;
}

bool MapEntityPropertyDialog_Show( QWidget *parent, map_workspace_t *ws, map_entity_property_action_t action, string_view_t key, QString *committedKey )
{
    if ( committedKey != nullptr ) { committedKey->clear(); }
    if ( !MapWorkspace_CanEditEntityProperties( ws ) ) { return false; }
    owners_t owners( ws->pDocument->pAllocator );
    if ( MapEntityEdit_ResolveOwners( ws->pDocument, { ws->selection.ids.pData, ws->selection.ids.nCount }, &owners.ids ) != map_status_t::OK ) { return false; }
    const bool identity = action == map_entity_property_action_t::IDENTITY;
    const bool add = action == map_entity_property_action_t::ADD;
    const bool rename = action == map_entity_property_action_t::RENAME;
    const QByteArray originalKey = Text( key ).toUtf8();
    key = View( originalKey ); // Own the key independently of the inspector rows.
    const key_value_t *exemplar = nullptr;
    usize present = 0u;
    bool mixed = false;
    if ( !add ) {
        for ( usize i = 0; i < owners.ids.nCount; ++i ) {
            const auto *value = PropertyValue( ws->pDocument, owners.ids.pData[i], key, identity );
            if ( value != nullptr ) {
                ++present;
                if ( exemplar == nullptr ) { exemplar = value; }
                else { mixed |= !MapEntityEdit_ValuesEqual( exemplar, value ); }
            }
        }
        if ( exemplar == nullptr && !( identity && StringView_Equals( key, StringView_FromCString( "name" ) ) ) ) { return false; }
    }
    bool editable = true;
    const QString initialValue = MapEntityProperty_ValueText( exemplar, &editable );
    if ( !add && !rename && exemplar != nullptr && !editable ) { return false; }
    const auto initialType = identity || exemplar == nullptr ? key_value_type_t::STRING : KeyValue_Type( exemplar );
    const auto initialSelection = std::vector<u64>( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
    const auto document = ws->pDocument;
    const auto token = UndoRedo_StateToken( ws->history.pUndo );
    dialog_watch_t watch( ws );
    if ( !watch.registered ) { return false; }

    QDialog dialog( parent );
    dialog.setObjectName( QStringLiteral( "MapEntityPropertyDialog" ) );
    dialog.setWindowTitle( add ? QStringLiteral( "Add entity key" ) : rename ? QStringLiteral( "Rename entity key" ) : QStringLiteral( "Edit entity value" ) );
    dialog.resize( 460, rename ? 180 : 380 );
    auto *layout = new QVBoxLayout( &dialog );
    auto *scope = new QLabel( QStringLiteral( "Applies to %1 owning %2." ).arg( owners.ids.nCount ).arg( owners.ids.nCount == 1 ? "entity" : "entities" ), &dialog );
    scope->setWordWrap( true );
    if ( mixed || ( !add && present < owners.ids.nCount ) ) {
        scope->setText( scope->text() + QStringLiteral( "\n%1Present on %2 of %3 entities. Applying a value updates every target." )
            .arg( mixed ? QStringLiteral( "Multiple values. " ) : QString() ).arg( present ).arg( owners.ids.nCount ) );
    }
    layout->addWidget( scope );
    auto *form = new QFormLayout();
    auto *keyEdit = new QLineEdit( Text( key ), &dialog );
    keyEdit->setObjectName( QStringLiteral( "MapEntityPropertyKey" ) );
    keyEdit->setReadOnly( !add && !rename );
    keyEdit->setPlaceholderText( QStringLiteral( "Property key" ) );
    form->addRow( QStringLiteral( "Key" ), keyEdit );
    auto *type = new QComboBox( &dialog );
    type->setObjectName( QStringLiteral( "MapEntityPropertyType" ) );
    constexpr key_value_type_t types[]{ key_value_type_t::STRING, key_value_type_t::I64, key_value_type_t::U64, key_value_type_t::F64,
        key_value_type_t::BOOL, key_value_type_t::NULL_VALUE, key_value_type_t::ARRAY, key_value_type_t::OBJECT };
    for ( auto t : types ) { type->addItem( QString::fromLatin1( MapEntityProperty_TypeName( t ) ), static_cast<int>( t ) ); }
    type->setCurrentIndex( type->findData( static_cast<int>( initialType ) ) );
    type->setEnabled( !identity );
    auto *typeLabel = new QLabel( QStringLiteral( "Type" ), &dialog );
    form->addRow( typeLabel, type );
    layout->addLayout( form );
    auto *valueEdit = new QPlainTextEdit( &dialog );
    valueEdit->setObjectName( QStringLiteral( "MapEntityPropertyValue" ) );
    valueEdit->setPlainText( mixed ? QString() : initialValue );
    auto *boolEdit = new QCheckBox( QStringLiteral( "Enabled (true)" ), &dialog );
    boolEdit->setObjectName( QStringLiteral( "MapEntityPropertyBool" ) );
    boolEdit->setTristate( mixed && initialType == key_value_type_t::BOOL );
    boolEdit->setCheckState( mixed && initialType == key_value_type_t::BOOL ? Qt::PartiallyChecked : initialValue == QStringLiteral( "true" ) ? Qt::Checked : Qt::Unchecked );
    layout->addWidget( boolEdit );
    layout->addWidget( valueEdit, 1 );
    auto *hint = new QLabel( &dialog );
    hint->setWordWrap( true );
    hint->setProperty( "muted", true );
    layout->addWidget( hint );
    const auto refreshType = [&]() {
        const auto t = static_cast<key_value_type_t>( type->currentData().toInt() );
        boolEdit->setVisible( !rename && t == key_value_type_t::BOOL );
        valueEdit->setVisible( !rename && t != key_value_type_t::BOOL && t != key_value_type_t::NULL_VALUE );
        type->setVisible( !rename ); typeLabel->setVisible( !rename );
        const bool structured = t == key_value_type_t::ARRAY || t == key_value_type_t::OBJECT;
        valueEdit->setFont( structured ? QFontDatabase::systemFont( QFontDatabase::FixedFont ) : keyEdit->font() );
        hint->setText( rename ? QStringLiteral( "The existing value and its type are preserved. Destination keys must be absent on every target." ) :
            structured ? QStringLiteral( "Enter one CYKV value. Arrays use [ ... ]; objects use { key = value }. Duplicate keys are rejected." ) :
            t == key_value_type_t::STRING ? QStringLiteral( "Plain text, including empty strings and line breaks. Quotes are not required." ) :
            t == key_value_type_t::NULL_VALUE ? QStringLiteral( "Stores an explicit null value." ) :
            t == key_value_type_t::BOOL ? QStringLiteral( "Choose true or false. Mixed values require an explicit choice." ) :
            QStringLiteral( "Enter a finite number within the selected type's range." ) );
    };
    QObject::connect( type, &QComboBox::currentIndexChanged, &dialog, refreshType );
    refreshType();
    auto *error = new QLabel( &dialog );
    error->setObjectName( QStringLiteral( "MapEntityPropertyError" ) );
    error->setWordWrap( true );
    error->setTextFormat( Qt::PlainText );
    layout->addWidget( error );
    auto *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    buttons->setObjectName( QStringLiteral( "MapEntityPropertyButtons" ) );
    buttons->button( QDialogButtonBox::Ok )->setText( rename ? QStringLiteral( "Rename" ) : QStringLiteral( "Apply" ) );
    layout->addWidget( buttons );
    QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    bool committed = false;
    QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if ( watch.changed || ws->pDocument != document || !UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) ||
             ws->selection.ids.nCount != initialSelection.size() || !std::equal( initialSelection.begin(), initialSelection.end(), ws->selection.ids.pData ) ||
             !MapWorkspace_CanEditEntityProperties( ws ) ) {
            error->setText( QStringLiteral( "The document or selection changed. Close this dialog and reopen the intended entity key." ) ); return;
        }
        const QByteArray newKey = keyEdit->text().toUtf8();
        if ( newKey.isEmpty() || newKey.size() > static_cast<qsizetype>( MAP_NAME_MAX_LENGTH ) || newKey.contains( '\0' ) ) {
            error->setText( QStringLiteral( "Enter a nonempty key of at most %1 UTF-8 bytes." ).arg( MAP_NAME_MAX_LENGTH ) ); return;
        }
        if ( rename ) {
            if ( newKey == originalKey ) { dialog.accept(); return; }
            for ( usize i = 0; i < owners.ids.nCount; ++i ) {
                if ( PropertyValue( ws->pDocument, owners.ids.pData[i], View( newKey ), false ) != nullptr ) {
                    error->setText( QStringLiteral( "That key already exists on a target entity. Choose a different name." ) ); return;
                }
            }
            committed = MapWorkspace_RenameEntityProperty( ws, key, View( newKey ) );
        } else {
            const QByteArray bytes = valueEdit->toPlainText().toUtf8();
            if ( bytes.size() > static_cast<qsizetype>( kValueTextMax ) ) { error->setText( QStringLiteral( "The value exceeds this editor's 64 KiB text limit." ) ); return; }
            if ( identity ) {
                bool unchanged = true;
                for ( usize i = 0; i < owners.ids.nCount; ++i ) {
                    const auto *old = PropertyValue( ws->pDocument, owners.ids.pData[i], key, true );
                    string_view_t oldText{};
                    const bool stringValue = KeyValue_GetString( old, &oldText );
                    unchanged &= ( old == nullptr || stringValue ) && StringView_Equals( oldText, View( bytes ) );
                }
                if ( unchanged ) { dialog.accept(); return; }
                committed = MapWorkspace_SetEntityIdentityField( ws, key, View( bytes ) );
            } else {
                key_value_document_desc_t desc{}; desc.pAllocator = ws->pDocument->pAllocator;
                value_document_ptr_t value( KeyValue_CreateDocument( desc ), &KeyValue_DestroyDocument );
                if ( value == nullptr ) { error->setText( QStringLiteral( "Not enough memory to prepare this value." ) ); return; }
                const auto t = static_cast<key_value_type_t>( type->currentData().toInt() );
                bool valid = true;
                auto *root = KeyValue_Root( value.get() );
                const key_value_t *authoredValue = root;
                const QString number = valueEdit->toPlainText().trimmed();
                switch ( t ) {
                    case key_value_type_t::STRING: valid = KeyValue_SetString( value.get(), root, View( bytes ) ); break;
                    case key_value_type_t::NULL_VALUE: valid = KeyValue_SetNull( value.get(), root ); break;
                    case key_value_type_t::BOOL:
                        valid = boolEdit->checkState() != Qt::PartiallyChecked && KeyValue_SetBool( value.get(), root, boolEdit->isChecked() ? CY_TRUE : CY_FALSE ); break;
                    case key_value_type_t::I64: {
                        bool ok = false; const auto n = number.toLongLong( &ok, 10 ); valid = ok && KeyValue_SetI64( value.get(), root, n ); break;
                    }
                    case key_value_type_t::U64: {
                        bool ok = false; const auto n = number.toULongLong( &ok, 10 ); valid = ok && !number.startsWith( QLatin1Char( '-' ) ) && KeyValue_SetU64( value.get(), root, n ); break;
                    }
                    case key_value_type_t::F64: {
                        bool ok = false; const auto n = number.toDouble( &ok ); valid = ok && std::isfinite( n ) && KeyValue_SetF64( value.get(), root, n ); break;
                    }
                    case key_value_type_t::ARRAY:
                    case key_value_type_t::OBJECT: {
                        // The native parser reads complete CYKV documents with
                        // object roots. Wrap exactly one value; do not burden
                        // authoring with headers or relax duplicate validation.
                        const QByteArray prefix( "@cykv 1\n@schema \"cypher.editor.entity_value\" 1\n{\nvalue =\n" );
                        const QByteArray suffix( "\n}" );
                        key_value_parse_options_t options{};
                        options.cbMaxInput = kValueTextMax + static_cast<usize>( prefix.size() + suffix.size() );
                        options.cbMaxStringData = kValueTextMax + static_cast<usize>( prefix.size() );
                        options.nMaxDepth = MAP_ENTITY_PROPERTY_VALUE_DEPTH_MAX;
                        options.nMaxNodes = MAP_ENTITY_PROPERTY_VALUE_NODES_MAX;
                        options.nMaxContainerValues = MAP_ENTITY_PROPERTY_VALUE_NODES_MAX;
                        const QByteArray wrapped = prefix + bytes + suffix;
                        const auto result = KeyValue_ParseText( View( wrapped ), options, value.get() );
                        if ( result.status != key_value_parse_status_t::OK ) {
                            error->setText( QStringLiteral( "CYKV value rejected: %1 (line %2, column %3)." ).arg( QString::fromLatin1( KeyValue_ParseStatusName( result.status ) ) )
                                .arg( std::max( 1u, result.errorLocation.nLine > 4u ? result.errorLocation.nLine - 4u : 1u ) ).arg( result.errorLocation.nColumn ) ); return;
                        }
                        const auto *container = KeyValue_Root( value.get() );
                        authoredValue = KeyValue_Find( container, StringView_FromCString( "value" ) );
                        valid = KeyValue_ChildCount( container ) == 1u && authoredValue != nullptr && KeyValue_Type( authoredValue ) == t;
                        break;
                    }
                    case key_value_type_t::BINARY: valid = false; break;
                }
                if ( !valid ) { error->setText( QStringLiteral( "The value does not match the selected type or exceeds its range. Choose a boolean value or correct the text." ) ); return; }
                if ( add ) {
                    for ( usize i = 0; i < owners.ids.nCount; ++i ) {
                        if ( PropertyValue( ws->pDocument, owners.ids.pData[i], View( newKey ), false ) != nullptr ) {
                            error->setText( QStringLiteral( "That key already exists. Select its row and edit the value." ) ); return;
                        }
                    }
                }
                bool unchanged = !add;
                for ( usize i = 0; i < owners.ids.nCount && unchanged; ++i ) {
                    unchanged &= MapEntityEdit_ValuesEqual( PropertyValue( ws->pDocument, owners.ids.pData[i], View( newKey ), false ), authoredValue );
                }
                if ( unchanged ) { dialog.accept(); return; }
                committed = MapWorkspace_SetEntityProperty( ws, View( newKey ), authoredValue );
            }
        }
        if ( committed ) { if ( committedKey != nullptr ) { *committedKey = Text( View( newKey ) ); } dialog.accept(); }
        else { error->setText( QStringLiteral( "Could not apply the edit. Check the entity data, field limits, and available undo memory. The live map was preserved." ) ); }
    } );
    if ( add || rename ) { keyEdit->selectAll(); keyEdit->setFocus(); }
    else if ( initialType == key_value_type_t::BOOL ) { boolEdit->setFocus(); }
    else { valueEdit->setFocus(); }
    dialog.exec();
    return committed;
}
} // namespace cypher::editor::map
