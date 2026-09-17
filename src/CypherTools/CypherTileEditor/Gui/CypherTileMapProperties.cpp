//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Edits map bounds and world metrics without reopening its source file.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileMapProperties.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace cypher::tools::tile_editor
{

CypherTileMapProperties::CypherTileMapProperties( QWidget *pParent ) : QWidget( pParent )
{
    setObjectName( QStringLiteral( "TileMapProperties" ) );
    auto *pRoot = new QVBoxLayout( this );
    pRoot->setContentsMargins( 6, 6, 6, 6 );
    pRoot->setSpacing( 8 );

    auto *pForm = new QFormLayout();
    pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
    pForm->setRowWrapPolicy( QFormLayout::WrapLongRows );
    m_pWidth = new QSpinBox( this );
    m_pHeight = new QSpinBox( this );
    m_pCellSize = new QDoubleSpinBox( this );
    m_pLevelHeight = new QDoubleSpinBox( this );
    m_pWidth->setObjectName( QStringLiteral( "TileMapWidth" ) );
    m_pHeight->setObjectName( QStringLiteral( "TileMapHeight" ) );
    m_pCellSize->setObjectName( QStringLiteral( "TileMapCellSize" ) );
    m_pLevelHeight->setObjectName( QStringLiteral( "TileMapLevelHeight" ) );
    m_pWidth->setRange( 1, static_cast<int>( TILE_MAP_MAX_WIDTH ) );
    m_pHeight->setRange( 1, static_cast<int>( TILE_MAP_MAX_HEIGHT ) );
    for ( auto *pMetric : { m_pCellSize, m_pLevelHeight } )
    {
        pMetric->setDecimals( 6 );
        pMetric->setSingleStep( 0.25 );
        pMetric->setKeyboardTracking( false );
    }
    m_pCellSize->setRange( TILE_MAP_MIN_CELL_SIZE, 1000.0 );
    m_pLevelHeight->setRange( TILE_MAP_MIN_LEVEL_HEIGHT, 1000.0 );
    m_pCellSize->setToolTip(
        tr( "World units per grid cell. Changing this resizes the authored map horizontally." ) );
    m_pLevelHeight->setToolTip(
        tr( "World units per floor level. Changing this rescales floor, wall, and stair heights." ) );
    m_pWidth->setToolTip(
        tr( "Map bounds in grid cells. Existing coordinates stay anchored at the map origin." ) );
    m_pHeight->setToolTip( m_pWidth->toolTip() );
    pForm->addRow( tr( "Width (cells)" ), m_pWidth );
    pForm->addRow( tr( "Height (cells)" ), m_pHeight );
    pForm->addRow( tr( "Cell size (units)" ), m_pCellSize );
    pForm->addRow( tr( "Level height (units)" ), m_pLevelHeight );
    pRoot->addLayout( pForm );

    m_pExtents = new QLabel( this );
    m_pExtents->setObjectName( QStringLiteral( "TileMapWorldExtents" ) );
    m_pExtents->setWordWrap( true );
    m_pExtents->setTextInteractionFlags( Qt::TextSelectableByMouse );
    pRoot->addWidget( m_pExtents );

    auto *pHelp = new QLabel( tr( "Apply updates the open map and its views as one undo step. "
                                  "Shrinking is rejected if it would remove cells or markers. "
                                  "Save the map to write these changes to .cymap." ),
                              this );
    pHelp->setWordWrap( true );
    pHelp->setProperty( "muted", true );
    pRoot->addWidget( pHelp );

    auto *pButtons = new QHBoxLayout();
    m_pApply = new QPushButton( tr( "Apply" ), this );
    m_pRevert = new QPushButton( tr( "Revert" ), this );
    m_pApply->setObjectName( QStringLiteral( "TileMapApply" ) );
    m_pRevert->setObjectName( QStringLiteral( "TileMapRevert" ) );
    m_pApply->setToolTip( tr( "Apply staged map properties without reopening the map." ) );
    m_pRevert->setToolTip( tr( "Discard staged values and display the current document properties." ) );
    pButtons->addWidget( m_pApply );
    pButtons->addWidget( m_pRevert );
    pRoot->addLayout( pButtons );
    m_pMessage = new QLabel( this );
    m_pMessage->setObjectName( QStringLiteral( "TileMapPropertiesMessage" ) );
    m_pMessage->setWordWrap( true );
    m_pMessage->setTextFormat( Qt::PlainText );
    m_pMessage->setTextInteractionFlags( Qt::TextSelectableByMouse );
    pRoot->addWidget( m_pMessage );

    auto *pIdentityLabel = new QLabel( tr( "Map identity" ), this );
    pIdentityLabel->setProperty( "muted", true );
    m_pMapId = new QLineEdit( this );
    m_pMapId->setObjectName( QStringLiteral( "TileMapIdentity" ) );
    m_pMapId->setReadOnly( true );
    m_pMapId->setToolTip( tr( "Stable map identifier. Resizing and metric edits preserve this identity." ) );
    pRoot->addWidget( pIdentityLabel );
    pRoot->addWidget( m_pMapId );
    pRoot->addStretch();

    connect( m_pWidth, &QSpinBox::valueChanged, this, [this] { fieldsChanged(); } );
    connect( m_pHeight, &QSpinBox::valueChanged, this, [this] { fieldsChanged(); } );
    connect( m_pCellSize, &QDoubleSpinBox::valueChanged, this, [this] { fieldsChanged(); } );
    connect( m_pLevelHeight, &QDoubleSpinBox::valueChanged, this, [this] { fieldsChanged(); } );
    connect( m_pApply, &QPushButton::clicked, this, [this] { applyChanges(); } );
    connect( m_pRevert, &QPushButton::clicked, this, [this] { resetChanges(); } );
    setDocument( nullptr );
}

void CypherTileMapProperties::setDocument( const tile_map_document_t *pDocument )
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) )
        pDocument = nullptr;
    if ( pDocument && pDocument == m_pDocument && UniqueId_Equals( pDocument->mapId, m_loadedMapId ) &&
         pDocument->nWidth == m_loadedDescription.nWidth &&
         pDocument->nHeight == m_loadedDescription.nHeight &&
         pDocument->nCellSize == m_loadedDescription.nCellSize &&
         pDocument->nLevelHeight == m_loadedDescription.nLevelHeight )
        return;
    m_pDocument = pDocument;
    m_loadedMapId = m_pDocument ? m_pDocument->mapId : unique_id_t{};
    m_syncing = true;
    m_loadedDescription = m_pDocument
                              ? tile_map_document_desc_t{ m_pDocument->nWidth, m_pDocument->nHeight,
                                                          m_pDocument->nCellSize, m_pDocument->nLevelHeight }
                              : tile_map_document_desc_t{};
    m_pWidth->setValue( static_cast<int>( m_loadedDescription.nWidth ) );
    m_pHeight->setValue( static_cast<int>( m_loadedDescription.nHeight ) );
    // Existing valid files may contain larger metrics than the normal editing
    // range. Display them faithfully rather than silently clamping on resize.
    m_pCellSize->setMaximum( std::max( 1000.0, static_cast<double>( m_loadedDescription.nCellSize ) ) );
    m_pLevelHeight->setMaximum( std::max( 1000.0, static_cast<double>( m_loadedDescription.nLevelHeight ) ) );
    m_pCellSize->setValue( m_loadedDescription.nCellSize );
    m_pLevelHeight->setValue( m_loadedDescription.nLevelHeight );
    m_loadedCellSizeValue = m_pCellSize->value();
    m_loadedLevelHeightValue = m_pLevelHeight->value();
    char identity[CY_UNIQUE_ID_STRING_CAPACITY]{};
    if ( m_pDocument && UniqueId_ToString( m_pDocument->mapId, identity, sizeof( identity ) ) )
        m_pMapId->setText( QString::fromLatin1( identity ) );
    else
        m_pMapId->clear();
    m_pMapId->setCursorPosition( 0 );
    m_syncing = false;
    m_pWidth->setEnabled( m_pDocument != nullptr );
    m_pHeight->setEnabled( m_pDocument != nullptr );
    m_pCellSize->setEnabled( m_pDocument != nullptr );
    m_pLevelHeight->setEnabled( m_pDocument != nullptr );
    m_pMessage->setText( m_pDocument ? tr( "Map properties are current." ) : tr( "No map is open." ) );
    refreshSummary();
    refreshButtons();
}

void CypherTileMapProperties::setApplyCallback(
    std::function<bool( const tile_map_document_desc_t &, QString & )> callback )
{
    m_apply = std::move( callback );
    refreshButtons();
}

tile_map_document_desc_t CypherTileMapProperties::description() const
{
    // A rounded display value must not change the original float when only
    // dimensions were edited. Metrics are converted only after an actual edit.
    return { static_cast<u32>( m_pWidth->value() ), static_cast<u32>( m_pHeight->value() ),
             m_pCellSize->value() == m_loadedCellSizeValue ? m_loadedDescription.nCellSize
                                                           : static_cast<f32>( m_pCellSize->value() ),
             m_pLevelHeight->value() == m_loadedLevelHeightValue
                 ? m_loadedDescription.nLevelHeight
                 : static_cast<f32>( m_pLevelHeight->value() ) };
}

bool CypherTileMapProperties::hasChanges() const
{
    const auto desired = description();
    return m_pDocument &&
           ( desired.nWidth != m_loadedDescription.nWidth || desired.nHeight != m_loadedDescription.nHeight ||
             desired.nCellSize != m_loadedDescription.nCellSize ||
             desired.nLevelHeight != m_loadedDescription.nLevelHeight );
}

void CypherTileMapProperties::refreshSummary()
{
    if ( !m_pDocument )
    {
        m_pExtents->clear();
        return;
    }
    const auto desired = description();
    m_pExtents->setText( tr( "World bounds: %1 × %2 units\nGrid capacity: %3 cells" )
                             .arg( static_cast<double>( desired.nWidth ) * desired.nCellSize, 0, 'g', 9 )
                             .arg( static_cast<double>( desired.nHeight ) * desired.nCellSize, 0, 'g', 9 )
                             .arg( static_cast<qulonglong>( desired.nWidth ) * desired.nHeight ) );
}

void CypherTileMapProperties::refreshButtons()
{
    const bool changed = hasChanges();
    m_pApply->setEnabled( changed && static_cast<bool>( m_apply ) && !m_applying );
    m_pRevert->setEnabled( changed && !m_applying );
}

void CypherTileMapProperties::fieldsChanged()
{
    if ( m_syncing )
        return;
    m_pMessage->setText( hasChanges() ? tr( "Unapplied map properties." )
                                      : tr( "Map properties are current." ) );
    refreshSummary();
    refreshButtons();
}

bool CypherTileMapProperties::applyChanges()
{
    if ( m_applying )
        return false;
    if ( !m_pDocument || !m_apply )
    {
        m_pMessage->setText( tr( "Map editing is unavailable." ) );
        return false;
    }
    // Commit any typed text before reading fields, including calls from menus.
    m_pWidth->interpretText();
    m_pHeight->interpretText();
    m_pCellSize->interpretText();
    m_pLevelHeight->interpretText();
    if ( !hasChanges() )
    {
        m_pMessage->setText( tr( "Map properties are current." ) );
        return true;
    }
    const auto desired = description();
    m_applying = true;
    refreshButtons();
    QString error;
    const bool applied = m_apply( desired, error );
    m_applying = false;
    if ( applied )
    {
        // The callback may synchronously publish a replacement document through
        // setDocument. Read that current pointer, never a cached pre-call one.
        setDocument( m_pDocument );
        m_pMessage->setText( tr( "Applied to the open map. Undo restores the previous properties." ) );
    }
    else
    {
        m_pMessage->setText( error.isEmpty() ? tr( "Map properties could not be applied." ) : error );
        refreshButtons();
    }
    return applied;
}

void CypherTileMapProperties::resetChanges()
{
    if ( m_applying )
        return;
    const auto *pCurrentDocument = m_pDocument;
    m_pDocument = nullptr;
    setDocument( pCurrentDocument );
}

} // namespace cypher::tools::tile_editor
