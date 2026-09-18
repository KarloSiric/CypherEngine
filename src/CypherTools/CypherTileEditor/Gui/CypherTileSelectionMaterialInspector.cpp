//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Selection-driven material information for the tile editor.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileSelectionMaterialInspector.h"

#include "Core/CypherTileMapMaterials.h"

#include <QApplication>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

namespace cypher::tools::tile_editor
{
namespace
{

QString MaterialBindingPath( const tile_map_material_binding_t &binding )
{
    const auto *end = static_cast<const char *>(
        std::memchr( binding.path, '\0', sizeof( binding.path ) ) );
    return end != nullptr
        ? QString::fromUtf8( binding.path, static_cast<qsizetype>( end - binding.path ) )
        : QString{};
}

QColor MaterialColor( u16 slot )
{
    const auto material = CypherTileMapMaterial_Resolve( slot );
    return QColor::fromRgbF( material.colorR, material.colorG, material.colorB );
}

QString CompactNumber( f32 value )
{
    QString result = QString::number( value, 'f', 3 );
    while ( result.contains( QLatin1Char( '.' ) ) && result.endsWith( QLatin1Char( '0' ) ) )
        result.chop( 1 );
    if ( result.endsWith( QLatin1Char( '.' ) ) ) result.chop( 1 );
    return result;
}

QPixmap PreviewPixmap( const tile_selection_material_inspection_t &inspection,
    const QSize &size )
{
    const qreal ratio = qMax( 1.0, qApp != nullptr ? qApp->devicePixelRatio() : 1.0 );
    QPixmap result( size * ratio );
    result.setDevicePixelRatio( ratio );
    result.fill( QColor( 24, 27, 31 ) );
    QPainter painter( &result );
    painter.setRenderHint( QPainter::Antialiasing );
    const QRectF canvas( 1.5, 1.5, size.width() - 3.0, size.height() - 3.0 );
    painter.setPen( QPen( QColor( 16, 18, 21 ), 1.0 ) );

    if ( !inspection.preview.isNull() ) {
        constexpr int checkerSize = 8;
        for ( int y = 0; y < size.height(); y += checkerSize ) {
            for ( int x = 0; x < size.width(); x += checkerSize ) {
                painter.fillRect( QRect( x, y, checkerSize, checkerSize ),
                    ( ( x / checkerSize + y / checkerSize ) & 1 ) != 0
                        ? QColor( 62, 65, 68 ) : QColor( 43, 46, 50 ) );
            }
        }
        const QImage scaled = inspection.preview.scaled( size * ratio,
            Qt::KeepAspectRatio, Qt::SmoothTransformation );
        const QSizeF logical( scaled.width() / ratio, scaled.height() / ratio );
        painter.drawImage( QRectF( ( size.width() - logical.width() ) * 0.5,
            ( size.height() - logical.height() ) * 0.5,
            logical.width(), logical.height() ), scaled );
    } else if ( inspection.state == tile_selection_material_state_t::MIXED ) {
        const auto &swatches = inspection.swatches;
        const int count = std::max( 1, static_cast<int>( swatches.size() ) );
        for ( int i = 0; i < count; ++i ) {
            const qreal left = canvas.left() + canvas.width() * i / count;
            const qreal right = canvas.left() + canvas.width() * ( i + 1 ) / count;
            painter.fillRect( QRectF( left, canvas.top(), right - left,
                canvas.height() ), swatches.empty() ? QColor( 80, 83, 88 ) : swatches[i] );
        }
        painter.setPen( QPen( QColor( 238, 166, 67 ), 2.0 ) );
        painter.drawLine( canvas.topLeft(), canvas.bottomRight() );
    } else if ( inspection.state == tile_selection_material_state_t::NO_SELECTION ||
                inspection.state == tile_selection_material_state_t::NO_SURFACES ) {
        painter.setPen( QColor( 105, 110, 116 ) );
        painter.drawText( canvas, Qt::AlignCenter, QStringLiteral( "—" ) );
    } else {
        const QColor base = inspection.color.isValid()
            ? inspection.color : QColor( 218, 35, 183 );
        QLinearGradient gradient( canvas.topLeft(), canvas.bottomRight() );
        gradient.setColorAt( 0.0, base.lighter( 132 ) );
        gradient.setColorAt( 0.55, base );
        gradient.setColorAt( 1.0, base.darker( 150 ) );
        painter.setBrush( gradient );
        painter.drawRect( canvas );
        painter.setPen( QColor( 245, 245, 245, 205 ) );
        painter.drawText( canvas.adjusted( 6, 5, -6, -5 ),
            Qt::AlignLeft | Qt::AlignBottom,
            QStringLiteral( "SLOT %1" ).arg( inspection.nSlot ) );
    }
    painter.setPen( QColor( 83, 87, 92 ) );
    painter.setBrush( Qt::NoBrush );
    painter.drawRect( canvas );
    return result;
}

} // namespace

tile_selection_material_inspection_t TileSelectionMaterial_Inspect(
    const tile_map_document_t *pDocument,
    std::span<const tile_map_grid_coord_t> selection,
    const tile_ortho_material_cache_t *pCache )
{
    tile_selection_material_inspection_t result{};
    result.nSelectedCells = selection.size();
    if ( pDocument == nullptr || selection.empty() ||
         !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        result.status = QObject::tr( "Select map geometry to inspect its material." );
        return result;
    }

    std::vector<u16> materialSlots;
    materialSlots.reserve( selection.size() );
    for ( const auto coordinate : selection ) {
        const tile_map_cell_t *cell = CypherTileMapDocument_CellAt( pDocument, coordinate );
        if ( cell == nullptr || ( cell->flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) continue;
        ++result.nFloorCells;
        materialSlots.push_back( cell->nMaterialSlot );
    }
    if ( materialSlots.empty() ) {
        result.state = tile_selection_material_state_t::NO_SURFACES;
        result.status = QObject::tr( "%1 selected cells contain no floor or stair surface." )
            .arg( selection.size() );
        return result;
    }

    std::sort( materialSlots.begin(), materialSlots.end() );
    materialSlots.erase( std::unique( materialSlots.begin(), materialSlots.end() ), materialSlots.end() );
    result.nDistinctSlots = materialSlots.size();
    QStringList slotLabels;
    const usize displayedSlots = std::min<usize>( materialSlots.size(), 8u );
    result.swatches.reserve( std::min<usize>( materialSlots.size(), 4u ) );
    for ( usize i = 0u; i < displayedSlots; ++i )
        slotLabels.push_back( QString::number( materialSlots[i] ) );
    for ( usize i = 0u; i < std::min<usize>( materialSlots.size(), 4u ); ++i ) {
        const tile_ortho_material_t *cached = pCache != nullptr
            ? TileOrthoMaterials_Find( *pCache, materialSlots[i] ) : nullptr;
        result.swatches.push_back( cached != nullptr && cached->color.isValid()
            ? cached->color : MaterialColor( materialSlots[i] ) );
    }
    if ( displayedSlots < materialSlots.size() ) slotLabels.push_back( QStringLiteral( "…" ) );
    result.slotsText = slotLabels.join( QStringLiteral( ", " ) );
    if ( materialSlots.size() != 1u ) {
        result.state = tile_selection_material_state_t::MIXED;
        result.name = QObject::tr( "Mixed materials" );
        result.status = QObject::tr( "%1 surfaces use %2 material slots." )
            .arg( result.nFloorCells ).arg( materialSlots.size() );
        return result;
    }

    result.nSlot = materialSlots.front();
    const auto fallback = CypherTileMapMaterial_Resolve( result.nSlot );
    result.builtInName = QString::fromUtf8( fallback.pDisplayName );
    result.stableId = QString::fromUtf8( fallback.pStableId );
    result.color = MaterialColor( result.nSlot );

    const tile_map_material_binding_t *binding =
        CypherTileMapDocument_FindMaterialBinding( pDocument, result.nSlot );
    if ( binding == nullptr ) {
        result.state = tile_selection_material_state_t::BUILTIN;
        result.name = result.builtInName;
        result.status = QObject::tr( "Built-in blockout material · %1 surfaces" )
            .arg( result.nFloorCells );
        return result;
    }

    result.path = MaterialBindingPath( *binding );
    const tile_ortho_material_t *cached = pCache != nullptr
        ? TileOrthoMaterials_Find( *pCache, result.nSlot ) : nullptr;
    if ( cached != nullptr ) {
        if ( !cached->path.isEmpty() ) result.path = cached->path;
        result.name = !cached->label.isEmpty()
            ? cached->label : QFileInfo( result.path ).completeBaseName();
        result.preview = cached->image;
        result.color = cached->color;
        result.textureWidth = cached->textureWidth;
        result.textureHeight = cached->textureHeight;
        result.sRGB = cached->sRGB;
        result.generateMips = cached->generateMips;
        std::copy_n( cached->tint, 4, result.tint );
        std::copy_n( cached->uvScale, 2, result.uvScale );
        result.diagnostic = cached->error;
    } else {
        result.name = QFileInfo( result.path ).completeBaseName();
        result.diagnostic = QObject::tr(
            "The project material is bound, but its preview has not been loaded." );
    }
    if ( result.name.isEmpty() ) result.name = QObject::tr( "Project material" );
    if ( cached != nullptr && !cached->image.isNull() && cached->error.isEmpty() ) {
        result.state = tile_selection_material_state_t::PROJECT_READY;
        result.status = QObject::tr( "Project material ready · %1 surfaces" )
            .arg( result.nFloorCells );
    } else {
        result.state = tile_selection_material_state_t::PROJECT_UNAVAILABLE;
        result.status = QObject::tr( "Project material preview unavailable" );
    }
    return result;
}

CypherTileSelectionMaterialInspector::CypherTileSelectionMaterialInspector(
    QWidget *pParent ) : QWidget( pParent )
{
    setObjectName( QStringLiteral( "TileSelectionMaterialInspector" ) );
    auto *root = new QVBoxLayout( this );
    root->setContentsMargins( 6, 6, 6, 6 );
    root->setSpacing( 6 );

    auto *hero = new QHBoxLayout;
    hero->setSpacing( 8 );
    m_pPreview = new QLabel( this );
    m_pPreview->setObjectName( QStringLiteral( "TileSelectionMaterialPreview" ) );
    m_pPreview->setFixedSize( 112, 78 );
    m_pPreview->setAlignment( Qt::AlignCenter );
    hero->addWidget( m_pPreview, 0, Qt::AlignTop );
    auto *identity = new QVBoxLayout;
    identity->setContentsMargins( 0, 2, 0, 0 );
    identity->setSpacing( 2 );
    m_pName = new QLabel( this );
    m_pName->setObjectName( QStringLiteral( "TileSelectionMaterialName" ) );
    m_pName->setWordWrap( true );
    m_pStatus = new QLabel( this );
    m_pStatus->setObjectName( QStringLiteral( "TileSelectionMaterialStatus" ) );
    m_pStatus->setWordWrap( true );
    m_pStatus->setProperty( "muted", true );
    identity->addWidget( m_pName );
    identity->addWidget( m_pStatus );
    identity->addStretch( 1 );
    hero->addLayout( identity, 1 );
    root->addLayout( hero );

    auto *details = new QFormLayout;
    details->setContentsMargins( 0, 0, 0, 0 );
    details->setHorizontalSpacing( 7 );
    details->setVerticalSpacing( 2 );
    const auto makeValue = [this]( const char *name ) {
        auto *label = new QLabel( this );
        label->setObjectName( QString::fromLatin1( name ) );
        label->setWordWrap( true );
        label->setTextInteractionFlags( Qt::TextSelectableByMouse );
        return label;
    };
    m_pSlot = makeValue( "TileSelectionMaterialSlotSummary" );
    m_pBinding = makeValue( "TileSelectionMaterialBinding" );
    m_pFallback = makeValue( "TileSelectionMaterialFallback" );
    m_pTexture = makeValue( "TileSelectionMaterialTexture" );
    m_pTint = makeValue( "TileSelectionMaterialTint" );
    m_pUvScale = makeValue( "TileSelectionMaterialUvScale" );
    details->addRow( tr( "Slot" ), m_pSlot );
    details->addRow( tr( "Binding" ), m_pBinding );
    details->addRow( tr( "Fallback" ), m_pFallback );
    details->addRow( tr( "Texture" ), m_pTexture );
    details->addRow( tr( "Tint" ), m_pTint );
    details->addRow( tr( "UV scale" ), m_pUvScale );
    root->addLayout( details );

    m_pDiagnostic = new QLabel( this );
    m_pDiagnostic->setObjectName( QStringLiteral( "TileSelectionMaterialDiagnostic" ) );
    m_pDiagnostic->setWordWrap( true );
    m_pDiagnostic->setTextInteractionFlags( Qt::TextSelectableByMouse );
    root->addWidget( m_pDiagnostic );

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins( 0, 0, 0, 0 );
    m_pUseForPaint = new QPushButton( tr( "Use as Current" ), this );
    m_pUseForPaint->setObjectName( QStringLiteral( "TileSelectionMaterialUse" ) );
    m_pBrowse = new QPushButton( tr( "Locate Material" ), this );
    m_pBrowse->setObjectName( QStringLiteral( "TileSelectionMaterialBrowse" ) );
    actions->addWidget( m_pUseForPaint );
    actions->addWidget( m_pBrowse );
    root->addLayout( actions );

    connect( m_pUseForPaint, &QPushButton::clicked, this, [this] {
        if ( m_useForPaintCallback ) m_useForPaintCallback( m_inspection.nSlot );
    } );
    connect( m_pBrowse, &QPushButton::clicked, this, [this] {
        if ( m_browseCallback )
            m_browseCallback( m_inspection.nSlot, m_inspection.path );
    } );
    publish( {} );
}

void CypherTileSelectionMaterialInspector::setSelection(
    const tile_map_document_t *pDocument,
    std::span<const tile_map_grid_coord_t> selection,
    const tile_ortho_material_cache_t *pCache )
{
    publish( TileSelectionMaterial_Inspect( pDocument, selection, pCache ) );
}

void CypherTileSelectionMaterialInspector::setUseForPaintCallback(
    std::function<void( u16 )> callback )
{
    m_useForPaintCallback = std::move( callback );
}

void CypherTileSelectionMaterialInspector::setBrowseCallback(
    std::function<void( u16, const QString & )> callback )
{
    m_browseCallback = std::move( callback );
}

const tile_selection_material_inspection_t &
CypherTileSelectionMaterialInspector::inspection() const noexcept
{
    return m_inspection;
}

void CypherTileSelectionMaterialInspector::publish(
    tile_selection_material_inspection_t inspection )
{
    m_inspection = std::move( inspection );
    const auto state = m_inspection.state;
    const char *stateName = "none";
    switch ( state ) {
        case tile_selection_material_state_t::NO_SELECTION:
        case tile_selection_material_state_t::NO_SURFACES:
            stateName = "none";
            break;
        case tile_selection_material_state_t::MIXED:
            stateName = "mixed";
            break;
        case tile_selection_material_state_t::BUILTIN:
            stateName = "builtin";
            break;
        case tile_selection_material_state_t::PROJECT_READY:
            stateName = "project";
            break;
        case tile_selection_material_state_t::PROJECT_UNAVAILABLE:
            stateName = "unavailable";
            break;
    }
    setProperty( "materialState", QString::fromLatin1( stateName ) );
    style()->unpolish( this );
    style()->polish( this );
    const bool single = state == tile_selection_material_state_t::BUILTIN ||
        state == tile_selection_material_state_t::PROJECT_READY ||
        state == tile_selection_material_state_t::PROJECT_UNAVAILABLE;
    const bool project = state == tile_selection_material_state_t::PROJECT_READY ||
        state == tile_selection_material_state_t::PROJECT_UNAVAILABLE;
    const bool hasTextureMetadata =
        state == tile_selection_material_state_t::PROJECT_READY;
    m_pPreview->setPixmap( PreviewPixmap( m_inspection, m_pPreview->size() ) );
    m_pName->setText( !m_inspection.name.isEmpty() ? m_inspection.name
        : state == tile_selection_material_state_t::NO_SURFACES
            ? tr( "No authored surface" ) : tr( "Material" ) );
    m_pStatus->setText( m_inspection.status );
    m_pSlot->setText( state == tile_selection_material_state_t::MIXED
        ? tr( "%1 slots · %2" ).arg( m_inspection.nDistinctSlots )
            .arg( m_inspection.slotsText )
        : single ? QString::number( m_inspection.nSlot ) : QStringLiteral( "—" ) );
    m_pBinding->setText( project ? m_inspection.path
        : state == tile_selection_material_state_t::BUILTIN
            ? tr( "Built-in blockout palette" ) : QStringLiteral( "—" ) );
    m_pBinding->setToolTip( project ? m_inspection.path : QString() );
    m_pFallback->setText( single
        ? tr( "%1 · %2" ).arg( m_inspection.builtInName, m_inspection.stableId )
        : QStringLiteral( "—" ) );
    m_pTexture->setText( state == tile_selection_material_state_t::PROJECT_READY
        ? tr( "%1 × %2 · %3 · %4" )
            .arg( m_inspection.textureWidth ).arg( m_inspection.textureHeight )
            .arg( m_inspection.sRGB ? tr( "sRGB" ) : tr( "Linear" ) )
            .arg( m_inspection.generateMips ? tr( "mip chain" ) : tr( "base level" ) )
        : state == tile_selection_material_state_t::BUILTIN
            ? tr( "Procedural color swatch" ) : QStringLiteral( "—" ) );
    m_pTint->setText( hasTextureMetadata
        ? tr( "%1, %2, %3, %4" ).arg( CompactNumber( m_inspection.tint[0] ),
            CompactNumber( m_inspection.tint[1] ), CompactNumber( m_inspection.tint[2] ),
            CompactNumber( m_inspection.tint[3] ) )
        : QStringLiteral( "—" ) );
    m_pUvScale->setText( hasTextureMetadata
        ? tr( "%1 × %2" ).arg( CompactNumber( m_inspection.uvScale[0] ),
            CompactNumber( m_inspection.uvScale[1] ) )
        : QStringLiteral( "—" ) );
    m_pDiagnostic->setVisible( !m_inspection.diagnostic.isEmpty() );
    m_pDiagnostic->setText( m_inspection.diagnostic );
    m_pDiagnostic->setProperty( "error", !m_inspection.diagnostic.isEmpty() );
    m_pDiagnostic->style()->unpolish( m_pDiagnostic );
    m_pDiagnostic->style()->polish( m_pDiagnostic );
    m_pUseForPaint->setEnabled( single );
    m_pBrowse->setEnabled( single );
}

} // namespace cypher::tools::tile_editor
