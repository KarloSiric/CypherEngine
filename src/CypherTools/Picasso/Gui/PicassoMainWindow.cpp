//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoMainWindow.cpp
//  Purpose: Implements the standalone Picasso authoring workspace.
//  Details: All commands delegate pixel work to PicassoCore. Qt owns only native
//           dialogs, presentation copies, persistent dock layout, and user-facing
//           diagnostics, preserving a reusable backend for Mason and automation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoMainWindow.h"

#include "PicassoCanvas.h"
#include "PicassoConsole.h"
#include "PicassoIcons.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Blob.h"
#include "CypherCommon_ImageConvert.h"
#include "CypherCommon_ImageFormat.h"
#include "CypherCommon_StringParse.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QRadialGradient>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string_view>

namespace cypher::tools::picasso
{

namespace
{

class PicassoNewTextureDialog final : public QDialog
{
public:
    explicit PicassoNewTextureDialog( QWidget *pParent )
        : QDialog( pParent )
    {
        setWindowTitle( tr( "New Texture" ) );
        setModal( true );
        setMinimumWidth( 360 );

        auto *pLayout = new QVBoxLayout( this );
        auto *pForm = new QFormLayout();
        m_pPreset = new QComboBox( this );
        m_pPreset->addItem( tr( "256 x 256" ), 256 );
        m_pPreset->addItem( tr( "512 x 512" ), 512 );
        m_pPreset->addItem( tr( "1024 x 1024" ), 1024 );
        m_pPreset->addItem( tr( "2048 x 2048" ), 2048 );
        m_pPreset->addItem( tr( "4096 x 4096" ), 4096 );
        m_pPreset->setCurrentIndex( 2 );

        m_pWidth = new QSpinBox( this );
        m_pHeight = new QSpinBox( this );
        for ( QSpinBox *pDimension : { m_pWidth, m_pHeight } ) {
            pDimension->setRange( 1, PICASSO_TEXTURE_MAX_DIMENSION );
            pDimension->setValue( 1024 );
        }

        m_pFill = new QComboBox( this );
        m_pFill->addItem( tr( "Checkerboard" ) );
        m_pFill->addItem( tr( "Solid" ) );
        m_pCheckerSize = new QSpinBox( this );
        m_pCheckerSize->setRange( 1, 1024 );
        m_pCheckerSize->setValue( 32 );

        pForm->addRow( tr( "Preset" ), m_pPreset );
        pForm->addRow( tr( "Width" ), m_pWidth );
        pForm->addRow( tr( "Height" ), m_pHeight );
        pForm->addRow( tr( "Fill" ), m_pFill );
        pForm->addRow( tr( "Checker size" ), m_pCheckerSize );
        pLayout->addLayout( pForm );

        auto *pButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
            this );
        pLayout->addWidget( pButtons );

        connect( m_pPreset, &QComboBox::currentIndexChanged, this, [this]( int ) {
            const int dimension = m_pPreset->currentData().toInt();
            m_pWidth->setValue( dimension );
            m_pHeight->setValue( dimension );
        } );
        connect( m_pFill, &QComboBox::currentIndexChanged, this, [this]( int index ) {
            m_pCheckerSize->setEnabled( index == 0 );
        } );
        connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
        connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    }

    [[nodiscard]] picasso_canvas_desc_t canvasDesc() const noexcept
    {
        picasso_canvas_desc_t desc{};
        desc.nWidth = static_cast<u32>( m_pWidth->value() );
        desc.nHeight = static_cast<u32>( m_pHeight->value() );
        desc.fill = m_pFill->currentIndex() == 0
            ? picasso_canvas_fill_t::CHECKERBOARD
            : picasso_canvas_fill_t::SOLID;
        desc.nCheckerSize = static_cast<u32>( m_pCheckerSize->value() );
        return desc;
    }

private:
    QComboBox *m_pPreset{ nullptr };
    QSpinBox *m_pWidth{ nullptr };
    QSpinBox *m_pHeight{ nullptr };
    QComboBox *m_pFill{ nullptr };
    QSpinBox *m_pCheckerSize{ nullptr };
};

class PicassoColorSwatches final : public QWidget
{
public:
    explicit PicassoColorSwatches( QWidget *pParent = nullptr )
        : QWidget( pParent )
    {
        setFixedSize( 34, 42 );
        setToolTip( tr( "Click a swatch to choose a paint color" ) );
    }

    [[nodiscard]] QColor foreground() const noexcept
    {
        return m_foreground;
    }

    void setForeground( const QColor &color )
    {
        if ( !color.isValid() || color == m_foreground ) {
            return;
        }
        m_foreground = color;
        update();
        if ( m_colorChanged ) {
            m_colorChanged( m_foreground );
        }
    }

    void setColorChangedCallback(
        std::function<void( const QColor & )> callback )
    {
        m_colorChanged = std::move( callback );
    }

protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing, false );

        const QRect background = backgroundRect();
        painter.fillRect( background, m_background );
        painter.setPen( QColor( 7, 10, 12 ) );
        painter.drawRect( background.adjusted( 0, 0, -1, -1 ) );

        const QRect foreground = foregroundRect();
        painter.fillRect( foreground, m_foreground );
        painter.setPen( QColor( 113, 125, 134 ) );
        painter.drawRect( foreground.adjusted( 0, 0, -1, -1 ) );

        painter.setPen( QColor( 217, 133, 49 ) );
        painter.drawLine( 3, 28, 30, 1 );
    }

    void mousePressEvent( QMouseEvent *pEvent ) override
    {
        QColor *pSelected = nullptr;
        if ( foregroundRect().contains( pEvent->position().toPoint() ) ) {
            pSelected = &m_foreground;
        } else if ( backgroundRect().contains(
                        pEvent->position().toPoint() ) ) {
            pSelected = &m_background;
        }
        if ( pSelected == nullptr ) {
            QWidget::mousePressEvent( pEvent );
            return;
        }

        const QColor selected = QColorDialog::getColor(
            *pSelected,
            this,
            tr( "Choose Paint Color" ),
            QColorDialog::ShowAlphaChannel );
        if ( selected.isValid() ) {
            *pSelected = selected;
            update();
            if ( pSelected == &m_foreground && m_colorChanged ) {
                m_colorChanged( m_foreground );
            }
        }
        pEvent->accept();
    }

private:
    [[nodiscard]] static QRect foregroundRect() noexcept
    {
        return { 3, 5, 20, 20 };
    }

    [[nodiscard]] static QRect backgroundRect() noexcept
    {
        return { 11, 14, 20, 20 };
    }

    QColor m_foreground{ 24, 28, 31, 255 };
    QColor m_background{ 221, 224, 226, 255 };
    std::function<void( const QColor & )> m_colorChanged{};
};

QWidget *MakePropertyRow(
    const QString &name,
    QLabel **ppValueOut,
    QWidget *pParent )
{
    auto *pRow = new QWidget( pParent );
    pRow->setProperty( "propertyRow", true );
    auto *pLayout = new QHBoxLayout( pRow );
    pLayout->setContentsMargins( 0, 1, 0, 1 );
    auto *pName = new QLabel( name, pRow );
    pName->setObjectName( "SectionLabel" );
    auto *pValue = new QLabel( QStringLiteral( "-" ), pRow );
    pValue->setObjectName( "ValueLabel" );
    pValue->setTextInteractionFlags( Qt::TextSelectableByMouse );
    pLayout->addWidget( pName );
    pLayout->addStretch( 1 );
    pLayout->addWidget( pValue );
    *ppValueOut = pValue;
    return pRow;
}

QString ColorSpaceName( image_color_space_t colorSpace )
{
    switch ( colorSpace ) {
        case image_color_space_t::SRGB:   return QStringLiteral( "sRGB" );
        case image_color_space_t::LINEAR: return QStringLiteral( "Linear" );
        default:                          return QStringLiteral( "Unknown" );
    }
}

QString StringFromView( string_view_t view )
{
    return StringView_IsValid( view )
        ? QString::fromUtf8(
              view.pData,
              static_cast<qsizetype>( view.cchLength ) )
        : QString{};
}

bool_t CommandNameIs( string_view_t name, const char *pExpected ) noexcept
{
    return StringView_EqualsInsensitiveAscii(
        name,
        StringView_FromCString( pExpected ) );
}

error_code_t InvalidCommandArguments() noexcept
{
    return CommandSystem_MakeError(
        command_system_error_t::INVALID_ARGUMENT );
}

QString ErrorName( error_code_t error )
{
    const error_domain_t domain = Cy_ErrorDomain( error );
    const u16 localCode = Cy_ErrorLocalCode( error );
    switch ( domain ) {
        case error_domain_t::COMMON:
            return QString::fromLatin1( Cy_CommonErrorName(
                static_cast<common_error_t>( localCode ) ) );
        case error_domain_t::COMMAND:
            return QString::fromLatin1( CommandSystem_ErrorName(
                static_cast<command_system_error_t>( localCode ) ) );
        case error_domain_t::CVAR:
            return QString::fromLatin1( CommandSystem_ErrorName(
                static_cast<convar_system_error_t>( localCode ) ) );
        default:
            return QStringLiteral( "%1:%2" ).arg(
                QString::fromLatin1( Cy_ErrorDomainName( domain ) ) )
                .arg( localCode );
    }
}

template<usize N>
usize CompleteFromLiterals(
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity,
    const char *( &values )[N] ) noexcept
{
    usize nRequired = 0u;
    usize nWritten = 0u;
    for ( const char *pValue : values ) {
        const string_view_t candidate = StringView_FromCString( pValue );
        if ( partial.cchLength > candidate.cchLength ||
             !StringView_EqualsInsensitiveAscii(
                 partial,
                 StringView_Prefix( candidate, partial.cchLength ) ) ) {
            continue;
        }
        ++nRequired;
        if ( nWritten < nSuggestionCapacity ) {
            pSuggestions[nWritten++] = candidate;
        }
    }
    return nRequired;
}

} // namespace

class PicassoSwatchPreview final : public QWidget
{
public:
    explicit PicassoSwatchPreview( QWidget *pParent = nullptr )
        : QWidget( pParent )
    {
        setMinimumHeight( 220 );
    }

    void setImage( const QImage &image )
    {
        m_image = image;
        update();
    }

protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter painter( this );
        painter.fillRect( rect(), QColor( 17, 21, 25 ) );
        const int diameter = std::min( width(), height() ) - 52;
        if ( diameter <= 0 ) {
            return;
        }
        const QRect circle(
            ( width() - diameter ) / 2,
            ( height() - diameter ) / 2,
            diameter,
            diameter );
        painter.setRenderHint( QPainter::Antialiasing );
        QPainterPath path;
        path.addEllipse( circle );
        painter.save();
        painter.setClipPath( path );
        if ( !m_image.isNull() ) {
            painter.drawImage( circle, m_image );
        } else {
            painter.fillRect( circle, QColor( 67, 74, 78 ) );
        }
        QRadialGradient shade(
            circle.center() - QPoint( diameter / 5, diameter / 5 ),
            diameter * 0.68 );
        shade.setColorAt( 0.0, QColor( 255, 255, 255, 25 ) );
        shade.setColorAt( 0.55, QColor( 0, 0, 0, 10 ) );
        shade.setColorAt( 1.0, QColor( 0, 0, 0, 185 ) );
        painter.fillRect( circle, shade );
        painter.restore();
        painter.setPen( QPen( QColor( 75, 82, 84 ), 1 ) );
        painter.drawEllipse( circle );
    }

private:
    QImage m_image{};
};

PicassoMainWindow::PicassoMainWindow( QWidget *pParent )
    : QMainWindow( pParent )
{
    setObjectName( QStringLiteral( "PicassoMainWindow" ) );
    setMinimumSize( 1280, 760 );
    resize( 1600, 960 );
    setDockNestingEnabled( true );
    setDocumentMode( true );

    const picasso_document_status_t initStatus =
        PicassoTextureDocument_Init( &m_document, Allocator_GetSystem() );
    if ( initStatus != picasso_document_status_t::OK ) {
        QMessageBox::critical(
            this,
            tr( "Picasso" ),
            tr( "The texture document backend could not be initialized." ) );
    }

    const picasso_paint_material_status_t materialStatus =
        PicassoPaintMaterial_Init(
            &m_material,
            StringView_FromCString( "Untitled Material" ) );
    if ( materialStatus == picasso_paint_material_status_t::OK ) {
        (void)PicassoPaintMaterial_SetConstant(
            &m_material,
            picasso_channel_semantic_t::BASE_COLOR,
            { 0.5f, 0.5f, 0.5f, 1.0f } );
        (void)PicassoPaintMaterial_SetConstant(
            &m_material,
            picasso_channel_semantic_t::ROUGHNESS,
            { 0.6f, 0.6f, 0.6f, 1.0f } );
    }

    buildActions();
    buildToolbar();
    buildWorkspace();
    buildMenus();
    restoreWorkspace();
    initializeCommandSystem();
    registerCommands();

    picasso_canvas_desc_t initial{};
    initial.nWidth = 1024u;
    initial.nHeight = 1024u;
    if ( PicassoTextureDocument_Create( &m_document, initial ) ==
         picasso_document_status_t::OK ) {
        refreshDocument();
        appendInfo( tr( "Created 1024 x 1024 checkerboard texture." ) );
        appendInfo( tr( "Type 'help' for the available Picasso commands." ) );

        // Dock restoration and maximization settle after construction. Fit on
        // the next event-loop turn so the initial document uses the real canvas
        // viewport instead of its temporary pre-show dimensions.
        QTimer::singleShot( 0, this, [this] {
            if ( m_pCanvas != nullptr ) {
                m_pCanvas->fitToView();
            }
        } );
    }
}

PicassoMainWindow::~PicassoMainWindow()
{
    CommandSystem_Destroy( m_pCommandSystem );
    m_pCommandSystem = nullptr;
    PicassoTextureDocument_Shutdown( &m_document );
}

void PicassoMainWindow::closeEvent( QCloseEvent *pEvent )
{
    if ( !maybeSave() ) {
        pEvent->ignore();
        return;
    }
    QSettings settings;
    settings.setValue( QStringLiteral( "Picasso/geometryV4" ), saveGeometry() );
    settings.setValue( QStringLiteral( "Picasso/windowStateV4" ), saveState( 4 ) );
    pEvent->accept();
}

void PicassoMainWindow::buildActions()
{
    m_pNewAction = new QAction(
        PicassoIcon_Create( u"file-plus", picasso_icon_tone_t::BLUE ),
        tr( "New Texture" ),
        this );
    m_pNewAction->setShortcut( QKeySequence::New );
    connect( m_pNewAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "file.new" ) );
    } );

    m_pOpenAction = new QAction(
        PicassoIcon_Create( u"folder-open", picasso_icon_tone_t::GOLD ),
        tr( "Open" ),
        this );
    m_pOpenAction->setShortcut( QKeySequence::Open );
    connect( m_pOpenAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "file.open" ) );
    } );

    m_pNewMaterialAction = new QAction(
        PicassoIcon_Create( u"layers-3", picasso_icon_tone_t::TEAL ),
        tr( "New Material" ),
        this );
    connect( m_pNewMaterialAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "material.new" ) );
    } );

    m_pOpenMaterialAction = new QAction(
        PicassoIcon_Create( u"folder-open", picasso_icon_tone_t::ORANGE ),
        tr( "Open Material" ),
        this );
    connect( m_pOpenMaterialAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "material.open" ) );
    } );

    m_pSaveAction = new QAction(
        PicassoIcon_Create( u"save", picasso_icon_tone_t::BLUE ),
        tr( "Save" ),
        this );
    m_pSaveAction->setShortcut( QKeySequence::Save );
    connect( m_pSaveAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "file.save" ) );
    } );

    m_pSaveAsAction = new QAction( tr( "Save As" ), this );
    m_pSaveAsAction->setShortcut( QKeySequence::SaveAs );
    connect( m_pSaveAsAction, &QAction::triggered, this, [this] { saveTextureAs(); } );

    m_pCompileAction = new QAction(
        PicassoIcon_Create( u"package-check", picasso_icon_tone_t::GREEN ),
        tr( "Compile" ),
        this );
    m_pCompileAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+B" ) ) );
    m_pCompileAction->setToolTip(
        tr( "Validate and compile the active Cypher texture" ) );
    connect( m_pCompileAction, &QAction::triggered,
             this, [this] { compileTexture(); } );

    m_pUndoAction = new QAction(
        PicassoIcon_Create( u"undo-2", picasso_icon_tone_t::GOLD ),
        tr( "Undo" ),
        this );
    m_pUndoAction->setShortcut( QKeySequence::Undo );
    connect( m_pUndoAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "edit.undo" ) );
    } );

    m_pRedoAction = new QAction(
        PicassoIcon_Create( u"redo-2", picasso_icon_tone_t::GOLD ),
        tr( "Redo" ),
        this );
    m_pRedoAction->setShortcut( QKeySequence::Redo );
    connect( m_pRedoAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "edit.redo" ) );
    } );

    auto makeOperation = [this](
        const QString &name,
        QStringView iconName,
        const char *pCommand ) {
        auto *pAction = new QAction(
            PicassoIcon_Create( iconName, picasso_icon_tone_t::GOLD ),
            name,
            this );
        connect( pAction, &QAction::triggered, this, [this, pCommand] {
            executeCommandLine( QString::fromLatin1( pCommand ) );
        } );
        return pAction;
    };
    m_pFlipHorizontalAction = makeOperation(
        tr( "Flip Horizontal" ), u"flip-horizontal-2", "image.flip-h" );
    m_pFlipVerticalAction = makeOperation(
        tr( "Flip Vertical" ), u"flip-vertical-2", "image.flip-v" );
    m_pRotateClockwiseAction = makeOperation(
        tr( "Rotate 90 Clockwise" ), u"rotate-cw", "image.rotate-right" );
    m_pRotateCounterClockwiseAction = makeOperation(
        tr( "Rotate 90 Counter-clockwise" ), u"rotate-ccw", "image.rotate-left" );
    m_pRotate180Action = makeOperation(
        tr( "Rotate 180" ), u"repeat-2", "image.rotate-180" );

    m_pFitAction = new QAction(
        PicassoIcon_Create( u"maximize", picasso_icon_tone_t::BLUE ),
        tr( "Fit to View" ),
        this );
    m_pFitAction->setShortcut( QKeySequence( Qt::Key_F ) );
    connect( m_pFitAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "view.fit" ) );
    } );
    m_pActualSizeAction = new QAction(
        PicassoIcon_Create( u"scan", picasso_icon_tone_t::BLUE ),
        tr( "Actual Size" ),
        this );
    m_pActualSizeAction->setShortcut( QKeySequence( Qt::Key_1 ) );
    connect( m_pActualSizeAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "view.actual" ) );
    } );
    m_pZoomInAction = new QAction(
        PicassoIcon_Create( u"zoom-in", picasso_icon_tone_t::BLUE ),
        tr( "Zoom In" ),
        this );
    m_pZoomInAction->setShortcut( QKeySequence::ZoomIn );
    connect( m_pZoomInAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "view.zoom-in" ) );
    } );
    m_pZoomOutAction = new QAction(
        PicassoIcon_Create( u"zoom-out", picasso_icon_tone_t::BLUE ),
        tr( "Zoom Out" ),
        this );
    m_pZoomOutAction->setShortcut( QKeySequence::ZoomOut );
    connect( m_pZoomOutAction, &QAction::triggered, this, [this] {
        executeCommandLine( QStringLiteral( "view.zoom-out" ) );
    } );

    m_pToggleConsoleAction = new QAction( tr( "Console" ), this );
    m_pToggleConsoleAction->setCheckable( true );
    m_pToggleConsoleAction->setChecked( true );
    m_pToggleConsoleAction->setShortcut(
        QKeySequence( QStringLiteral( "Ctrl+`" ) ) );
    connect( m_pToggleConsoleAction, &QAction::triggered,
             this, [this]( bool bVisible ) {
        if ( m_pConsoleDock != nullptr ) {
            m_pConsoleDock->setVisible( bVisible );
            if ( bVisible ) {
                m_pConsole->focusCommandLine();
            }
        }
    } );

    m_pChannelActions = new QActionGroup( this );
    m_pChannelActions->setExclusive( true );
    auto makeChannelAction = [this](
        const QString &name,
        const char *pChannel ) {
        auto *pAction = new QAction( name, m_pChannelActions );
        pAction->setCheckable( true );
        m_pChannelActions->addAction( pAction );
        connect( pAction, &QAction::triggered, this, [this, pChannel] {
            executeCommandLine( QStringLiteral( "channel.set %1" )
                .arg( QString::fromLatin1( pChannel ) ) );
        } );
        return pAction;
    };
    m_pChannelRgbaAction = makeChannelAction( QStringLiteral( "RGBA" ), "rgba" );
    m_pChannelRedAction = makeChannelAction( QStringLiteral( "R" ), "r" );
    m_pChannelGreenAction = makeChannelAction( QStringLiteral( "G" ), "g" );
    m_pChannelBlueAction = makeChannelAction( QStringLiteral( "B" ), "b" );
    m_pChannelAlphaAction = makeChannelAction( QStringLiteral( "A" ), "a" );
    m_pChannelRgbaAction->setChecked( true );

    m_pToolActions = new QActionGroup( this );
    m_pToolActions->setExclusive( true );
    auto makeToolAction = [this](
        const QString &name,
        QStringView iconName,
        const char *pTool,
        picasso_canvas_tool_t tool,
        const QKeySequence &shortcut,
        int group ) {
        const picasso_icon_tone_t tone = group == 0
            ? picasso_icon_tone_t::BLUE
            : ( group == 1
                ? picasso_icon_tone_t::TEAL
                : ( group == 2
                    ? picasso_icon_tone_t::GOLD
                    : picasso_icon_tone_t::STEEL ) );
        auto *pAction = new QAction(
            PicassoIcon_Create( iconName, tone ), name, m_pToolActions );
        pAction->setCheckable( true );
        pAction->setShortcut( shortcut );
        pAction->setToolTip( shortcut.isEmpty()
            ? name
            : tr( "%1 (%2)" ).arg( name, shortcut.toString() ) );
        pAction->setData( static_cast<int>( tool ) );
        pAction->setProperty( "PicassoToolName", pTool );
        pAction->setProperty( "PicassoToolGroup", group );
        m_pToolActions->addAction( pAction );
        connect( pAction, &QAction::triggered, this, [this, pTool] {
            executeCommandLine( QStringLiteral( "tool.set %1" )
                .arg( QString::fromLatin1( pTool ) ) );
        } );
        return pAction;
    };
    m_pSelectToolAction = makeToolAction(
        tr( "Select" ), u"mouse-pointer-2", "select",
        picasso_canvas_tool_t::SELECT, QKeySequence( Qt::Key_V ), 0 );
    makeToolAction(
        tr( "Rectangular Selection" ), u"scan", "marquee",
        picasso_canvas_tool_t::MARQUEE, QKeySequence( Qt::Key_M ), 0 );
    makeToolAction(
        tr( "Lasso Selection" ), u"lasso-select", "lasso",
        picasso_canvas_tool_t::LASSO, QKeySequence( Qt::Key_L ), 0 );
    makeToolAction(
        tr( "Move" ), u"move", "move",
        picasso_canvas_tool_t::MOVE, QKeySequence( Qt::Key_T ), 0 );
    makeToolAction(
        tr( "Brush" ), u"brush", "brush",
        picasso_canvas_tool_t::BRUSH, QKeySequence( Qt::Key_B ), 1 );
    makeToolAction(
        tr( "Eraser" ), u"eraser", "eraser",
        picasso_canvas_tool_t::ERASER, QKeySequence( Qt::Key_E ), 1 );
    makeToolAction(
        tr( "Fill" ), u"paint-bucket", "fill",
        picasso_canvas_tool_t::FILL, QKeySequence( Qt::Key_G ), 1 );
    makeToolAction(
        tr( "Gradient" ), u"blend", "gradient",
        picasso_canvas_tool_t::GRADIENT, QKeySequence(), 1 );
    makeToolAction(
        tr( "Color Picker" ), u"pipette", "eyedropper",
        picasso_canvas_tool_t::EYEDROPPER, QKeySequence( Qt::Key_I ), 1 );
    makeToolAction(
        tr( "Clone" ), u"stamp", "clone",
        picasso_canvas_tool_t::CLONE, QKeySequence( Qt::Key_S ), 1 );
    makeToolAction(
        tr( "Crop" ), u"crop", "crop",
        picasso_canvas_tool_t::CROP, QKeySequence( Qt::Key_C ), 2 );
    makeToolAction(
        tr( "Seam and Offset" ), u"repeat-2", "seam",
        picasso_canvas_tool_t::SEAM, QKeySequence(), 2 );
    makeToolAction(
        tr( "Mask" ), u"square-dashed", "mask",
        picasso_canvas_tool_t::MASK, QKeySequence(), 2 );
    m_pPanToolAction = makeToolAction(
        tr( "Pan" ), u"hand", "pan",
        picasso_canvas_tool_t::PAN, QKeySequence( Qt::Key_H ), 3 );
    m_pZoomToolAction = makeToolAction(
        tr( "Zoom" ), u"zoom-in", "zoom",
        picasso_canvas_tool_t::ZOOM, QKeySequence( Qt::Key_Z ), 3 );
    m_pInspectToolAction = makeToolAction(
        tr( "Inspect Pixels" ), u"gauge", "inspect",
        picasso_canvas_tool_t::INSPECT, QKeySequence(), 3 );
    m_pSelectToolAction->setChecked( true );
}

void PicassoMainWindow::buildMenus()
{
    // Asset tools keep their menus inside the workspace on every platform.
    // This gives macOS the same dense menu/tool hierarchy as Windows and Linux.
    menuBar()->setNativeMenuBar( false );

    QMenu *pFile = menuBar()->addMenu( tr( "File" ) );
    pFile->addAction( m_pNewAction );
    pFile->addAction( m_pOpenAction );
    pFile->addSeparator();
    pFile->addAction( m_pSaveAction );
    pFile->addAction( m_pSaveAsAction );
    pFile->addSeparator();
    pFile->addAction( m_pCompileAction );
    pFile->addSeparator();
    QAction *pQuit = pFile->addAction( tr( "Quit" ) );
    pQuit->setShortcut( QKeySequence::Quit );
    connect( pQuit, &QAction::triggered, this, &QWidget::close );

    QMenu *pEdit = menuBar()->addMenu( tr( "Edit" ) );
    pEdit->addAction( m_pUndoAction );
    pEdit->addAction( m_pRedoAction );

    QMenu *pView = menuBar()->addMenu( tr( "View" ) );
    pView->addAction( m_pFitAction );
    pView->addAction( m_pActualSizeAction );
    pView->addAction( m_pZoomInAction );
    pView->addAction( m_pZoomOutAction );
    pView->addSeparator();
    pView->addAction( m_pToggleConsoleAction );

    QMenu *pTexture = menuBar()->addMenu( tr( "Texture" ) );
    pTexture->addAction( m_pFlipHorizontalAction );
    pTexture->addAction( m_pFlipVerticalAction );
    pTexture->addSeparator();
    pTexture->addAction( m_pRotateClockwiseAction );
    pTexture->addAction( m_pRotateCounterClockwiseAction );
    pTexture->addAction( m_pRotate180Action );

    QMenu *pMaterial = menuBar()->addMenu( tr( "Material" ) );
    pMaterial->addAction( m_pNewMaterialAction );
    pMaterial->addAction( m_pOpenMaterialAction );
    pMaterial->addSeparator();
    QAction *pAssignTexture = pMaterial->addAction(
        tr( "Assign Texture to Material Slot" ) );
    pAssignTexture->setEnabled( false );

    QMenu *pTools = menuBar()->addMenu( tr( "Tools" ) );
    pTools->addAction( m_pCompileAction );
    pTools->addAction( m_pToggleConsoleAction );

    QMenu *pWindow = menuBar()->addMenu( tr( "Window" ) );
    const QList<QDockWidget *> docks = findChildren<QDockWidget *>();
    for ( QDockWidget *pDock : docks ) {
        pWindow->addAction( pDock->toggleViewAction() );
    }

    QMenu *pHelp = menuBar()->addMenu( tr( "Help" ) );
    QAction *pAbout = pHelp->addAction( tr( "About Picasso" ) );
    connect( pAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            tr( "About Picasso" ),
            tr( "Picasso 1.0.0\nTexture and material authoring for CypherEngine." ) );
    } );
}

void PicassoMainWindow::buildToolbar()
{
    QToolBar *pToolbar = addToolBar( tr( "Main" ) );
    pToolbar->setObjectName( QStringLiteral( "PicassoMainToolbar" ) );
    pToolbar->setMovable( false );
    pToolbar->setFloatable( false );
    pToolbar->setIconSize( QSize( 26, 26 ) );
    pToolbar->addAction( m_pNewAction );

    auto addLabeledAction = [pToolbar]( QAction *pAction ) {
        auto *pButton = new QToolButton( pToolbar );
        pButton->setDefaultAction( pAction );
        pButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        pButton->setProperty( "commandButton", true );
        pToolbar->addWidget( pButton );
    };
    addLabeledAction( m_pOpenAction );
    addLabeledAction( m_pSaveAction );
    addLabeledAction( m_pCompileAction );
    pToolbar->addSeparator();
    pToolbar->addAction( m_pUndoAction );
    pToolbar->addAction( m_pRedoAction );
    pToolbar->addSeparator();

    auto *p2d = new QToolButton( pToolbar );
    auto *p3d = new QToolButton( pToolbar );
    for ( QToolButton *pButton : { p2d, p3d } ) {
        pButton->setCheckable( true );
        pButton->setProperty( "segment", true );
    }
    p2d->setText( QStringLiteral( "2D" ) );
    p3d->setText( QStringLiteral( "3D" ) );
    p2d->setChecked( true );
    pToolbar->addWidget( p2d );
    pToolbar->addWidget( p3d );
    connect( p2d, &QToolButton::clicked, this, [this, p2d, p3d] {
        p2d->setChecked( true );
        p3d->setChecked( false );
        setStatusMessage( tr( "2D texture view" ) );
    } );
    connect( p3d, &QToolButton::clicked, this, [this, p2d, p3d] {
        p2d->setChecked( false );
        p3d->setChecked( true );
        if ( QDockWidget *pPreviewDock = findChild<QDockWidget *>(
                 QStringLiteral( "PicassoPreviewDock" ) ) ) {
            pPreviewDock->show();
            pPreviewDock->raise();
        }
        setStatusMessage( tr( "3D material preview" ) );
    } );

    auto *pSpacer = new QWidget( pToolbar );
    pSpacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
    pToolbar->addWidget( pSpacer );
    pToolbar->addAction( m_pFitAction );
    pToolbar->addAction( m_pActualSizeAction );

    // The options strip belongs to the active tool. Persistent tool selection
    // remains exclusively in the left rail, matching image-authoring workflow.
    addToolBarBreak( Qt::TopToolBarArea );
    QToolBar *pOperations = addToolBar( tr( "Tool Options" ) );
    pOperations->setObjectName( QStringLiteral( "PicassoOperationToolbar" ) );
    pOperations->setMovable( false );
    pOperations->setFloatable( false );
    pOperations->setIconSize( QSize( 24, 24 ) );

    auto *pToolLabel = new QLabel( tr( "TOOL" ), pOperations );
    pToolLabel->setObjectName( QStringLiteral( "ToolbarGroupLabel" ) );
    pOperations->addWidget( pToolLabel );

    m_pContextToolButton = new QToolButton( pOperations );
    m_pContextToolButton->setObjectName(
        QStringLiteral( "PicassoContextTool" ) );
    m_pContextToolButton->setProperty( "contextTool", true );
    m_pContextToolButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    m_pContextToolButton->setFocusPolicy( Qt::NoFocus );
    m_pContextToolButton->setAttribute( Qt::WA_TransparentForMouseEvents );
    pOperations->addWidget( m_pContextToolButton );
    pOperations->addSeparator();

    m_pBrushContextPanel = new QWidget( pOperations );
    m_pBrushContextPanel->setObjectName(
        QStringLiteral( "PicassoBrushContext" ) );
    auto *pBrushLayout = new QHBoxLayout( m_pBrushContextPanel );
    pBrushLayout->setContentsMargins( 0, 0, 0, 0 );
    pBrushLayout->setSpacing( 5 );

    auto addContextLabel = [this, pBrushLayout]( const QString &text ) {
        auto *pLabel = new QLabel( text, m_pBrushContextPanel );
        pLabel->setObjectName( QStringLiteral( "ToolbarLabel" ) );
        pBrushLayout->addWidget( pLabel );
    };
    addContextLabel( tr( "Size" ) );
    m_pBrushSize = new QSpinBox( m_pBrushContextPanel );
    m_pBrushSize->setObjectName( QStringLiteral( "PicassoBrushSize" ) );
    m_pBrushSize->setRange( 1, 512 );
    m_pBrushSize->setValue( 24 );
    m_pBrushSize->setSuffix( tr( "px" ) );
    m_pBrushSize->setMaximumWidth( 76 );
    pBrushLayout->addWidget( m_pBrushSize );

    addContextLabel( tr( "Opacity" ) );
    m_pBrushOpacity = new QSpinBox( m_pBrushContextPanel );
    m_pBrushOpacity->setObjectName( QStringLiteral( "PicassoBrushOpacity" ) );
    m_pBrushOpacity->setRange( 0, 100 );
    m_pBrushOpacity->setValue( 80 );
    m_pBrushOpacity->setSuffix( QStringLiteral( "%" ) );
    m_pBrushOpacity->setMaximumWidth( 72 );
    pBrushLayout->addWidget( m_pBrushOpacity );

    addContextLabel( tr( "Hardness" ) );
    m_pBrushHardness = new QSpinBox( m_pBrushContextPanel );
    m_pBrushHardness->setObjectName( QStringLiteral( "PicassoBrushHardness" ) );
    m_pBrushHardness->setRange( 0, 100 );
    m_pBrushHardness->setValue( 90 );
    m_pBrushHardness->setSuffix( QStringLiteral( "%" ) );
    m_pBrushHardness->setMaximumWidth( 72 );
    pBrushLayout->addWidget( m_pBrushHardness );

    addContextLabel( tr( "Blend" ) );
    m_pBlendMode = new QComboBox( m_pBrushContextPanel );
    m_pBlendMode->setObjectName( QStringLiteral( "PicassoBlendMode" ) );
    // Additional blend modes return when their kernels and undo tests exist.
    // Showing inert choices here would make the toolbar lie about capability.
    m_pBlendMode->addItem( tr( "Normal" ) );
    m_pBlendMode->setMinimumWidth( 96 );
    pBrushLayout->addWidget( m_pBlendMode );
    m_pBrushContextAction = pOperations->addWidget( m_pBrushContextPanel );

    auto *pContextSpacer = new QWidget( pOperations );
    pContextSpacer->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Preferred );
    pOperations->addWidget( pContextSpacer );

    auto updateContext = [this] {
        if ( m_pCanvas != nullptr && m_pBrushSize != nullptr ) {
            m_pCanvas->setBrushDiameter( m_pBrushSize->value() );
        }
        refreshToolContext();
    };
    connect( m_pBrushSize, &QSpinBox::valueChanged, this, updateContext );
    connect( m_pBrushOpacity, &QSpinBox::valueChanged, this, updateContext );
    connect( m_pBrushHardness, &QSpinBox::valueChanged, this, updateContext );
    connect( m_pBlendMode, &QComboBox::currentIndexChanged, this, updateContext );
}

void PicassoMainWindow::buildWorkspace()
{
    setCorner( Qt::BottomLeftCorner, Qt::LeftDockWidgetArea );
    setCorner( Qt::BottomRightCorner, Qt::RightDockWidgetArea );

    auto *pDocumentHost = new QWidget( this );
    pDocumentHost->setObjectName( QStringLiteral( "PicassoDocumentHost" ) );
    auto *pDocumentLayout = new QVBoxLayout( pDocumentHost );
    pDocumentLayout->setContentsMargins( 0, 0, 0, 0 );
    pDocumentLayout->setSpacing( 0 );

    auto *pDocumentTabs = new QWidget( pDocumentHost );
    pDocumentTabs->setObjectName( QStringLiteral( "PicassoDocumentTabs" ) );
    auto *pTabLayout = new QHBoxLayout( pDocumentTabs );
    pTabLayout->setContentsMargins( 9, 0, 5, 0 );
    pTabLayout->setSpacing( 6 );
    auto *pDocumentIcon = new QLabel( pDocumentTabs );
    pDocumentIcon->setPixmap( PicassoIcon_Create(
        u"brush", picasso_icon_tone_t::TEAL ).pixmap( 18, 18 ) );
    m_pDocumentName = new QLabel( tr( "Untitled.cytex" ), pDocumentTabs );
    m_pDocumentName->setObjectName( QStringLiteral( "PicassoDocumentName" ) );
    auto *pCloseTab = new QToolButton( pDocumentTabs );
    pCloseTab->setText( QStringLiteral( "×" ) );
    pCloseTab->setToolTip( tr( "Close document" ) );
    connect( pCloseTab, &QToolButton::clicked, this, &QWidget::close );
    pTabLayout->addWidget( pDocumentIcon );
    pTabLayout->addWidget( m_pDocumentName );
    pTabLayout->addWidget( pCloseTab );
    pTabLayout->addStretch( 1 );
    auto *pNewTab = new QToolButton( pDocumentTabs );
    pNewTab->setIcon( PicassoIcon_Create(
        u"plus", picasso_icon_tone_t::BLUE ) );
    pNewTab->setToolTip( tr( "New texture document" ) );
    connect( pNewTab, &QToolButton::clicked, m_pNewAction, &QAction::trigger );
    pTabLayout->addWidget( pNewTab );
    pDocumentLayout->addWidget( pDocumentTabs );

    auto *pDocumentInfoBar = new QWidget( pDocumentHost );
    pDocumentInfoBar->setObjectName( QStringLiteral( "PicassoDocumentInfoBar" ) );
    auto *pInfoLayout = new QHBoxLayout( pDocumentInfoBar );
    pInfoLayout->setContentsMargins( 10, 3, 7, 3 );
    pInfoLayout->setSpacing( 5 );
    m_pDocumentInfo = new QLabel( tr( "1024 × 1024    RGBA8" ), pDocumentInfoBar );
    m_pDocumentInfo->setObjectName( QStringLiteral( "PicassoDocumentInfo" ) );
    pInfoLayout->addWidget( m_pDocumentInfo );
    pInfoLayout->addStretch( 1 );
    for ( QAction *pAction : {
              m_pChannelRgbaAction,
              m_pChannelRedAction,
              m_pChannelGreenAction,
              m_pChannelBlueAction,
              m_pChannelAlphaAction } ) {
        auto *pButton = new QToolButton( pDocumentInfoBar );
        pButton->setDefaultAction( pAction );
        pButton->setProperty( "channelButton", true );
        pInfoLayout->addWidget( pButton );
    }
    auto *pZoom = new QComboBox( pDocumentInfoBar );
    pZoom->setObjectName( QStringLiteral( "PicassoZoomPreset" ) );
    pZoom->addItems( {
        QStringLiteral( "Fit" ), QStringLiteral( "25%" ),
        QStringLiteral( "50%" ), QStringLiteral( "100%" ),
        QStringLiteral( "200%" ), QStringLiteral( "400%" )
    } );
    pZoom->setCurrentText( QStringLiteral( "Fit" ) );
    pZoom->setMaximumWidth( 78 );
    pInfoLayout->addWidget( pZoom );
    pDocumentLayout->addWidget( pDocumentInfoBar );

    m_pCanvas = new PicassoCanvas( pDocumentHost );
    m_pCanvas->setObjectName( QStringLiteral( "PicassoCanvas" ) );
    pDocumentLayout->addWidget( m_pCanvas, 1 );
    setCentralWidget( pDocumentHost );
    m_pCanvas->setZoomChangedCallback( [this]( qreal zoom ) {
        if ( m_pStatusZoom != nullptr ) {
            m_pStatusZoom->setText(
                QString::number( qRound( zoom * 100.0 ) ) + QLatin1Char( '%' ) );
        }
    } );
    m_pCanvas->setPixelHoveredCallback( [this]( int x, int y, QRgb pixel ) {
        if ( m_pStatusPixel == nullptr ) {
            return;
        }
        if ( x < 0 || y < 0 ) {
            m_pStatusPixel->clear();
            return;
        }
        const QColor color = QColor::fromRgba( pixel );
        m_pStatusPixel->setText(
            QStringLiteral( "x:%1 y:%2  #%3%4%5%6" )
                .arg( x )
                .arg( y )
                .arg( color.red(), 2, 16, QLatin1Char( '0' ) )
                .arg( color.green(), 2, 16, QLatin1Char( '0' ) )
                .arg( color.blue(), 2, 16, QLatin1Char( '0' ) )
                .arg( color.alpha(), 2, 16, QLatin1Char( '0' ) )
                .toUpper() );
    } );
    m_pCanvas->setBrushDiameter( m_pBrushSize->value() );
    m_pCanvas->setToolInteractionCallback(
        [this](
            picasso_canvas_tool_t tool,
            picasso_canvas_interaction_t interaction,
            qreal x,
            qreal y ) {
            handleCanvasInteraction( tool, interaction, x, y );
        } );
    connect( pZoom, &QComboBox::currentTextChanged, this,
             [this]( const QString &text ) {
        if ( text == QStringLiteral( "Fit" ) ) {
            m_pCanvas->fitToView();
            return;
        }
        QString numeric = text;
        numeric.remove( QLatin1Char( '%' ) );
        bool bOk = false;
        const qreal percentage = numeric.toDouble( &bOk );
        if ( bOk && percentage > 0.0 ) {
            m_pCanvas->setZoom( percentage / 100.0 );
        }
    } );

    buildToolRail();

    auto *pLayersDock = new QDockWidget( tr( "LAYERS" ), this );
    pLayersDock->setObjectName( QStringLiteral( "PicassoLayersDock" ) );
    pLayersDock->setMinimumWidth( 230 );
    auto *pLayersPanel = new QWidget( pLayersDock );
    pLayersPanel->setObjectName( QStringLiteral( "PicassoLayersPanel" ) );
    auto *pLayersLayout = new QVBoxLayout( pLayersPanel );
    pLayersLayout->setContentsMargins( 5, 5, 5, 5 );
    pLayersLayout->setSpacing( 4 );
    m_pLayerTree = new QTreeWidget( pLayersPanel );
    m_pLayerTree->setObjectName( QStringLiteral( "PicassoLayerTree" ) );
    m_pLayerTree->setIconSize( QSize( 19, 19 ) );
    m_pLayerTree->setHeaderHidden( true );
    m_pLayerTree->setRootIsDecorated( false );
    m_pLayerTree->setColumnCount( 2 );
    m_pLayerTree->setColumnWidth( 0, 164 );
    m_pLayerTree->setColumnWidth( 1, 22 );
    m_pLayerTree->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );

    auto addPreviewLayer = [this](
        const QString &name,
        const QString &blend,
        int opacity,
        bool bLocked ) {
        auto *pItem = new QTreeWidgetItem( m_pLayerTree );
        pItem->setText( 0, QStringLiteral( "%1\n%2 %3%" )
            .arg( name, blend )
            .arg( opacity ) );
        pItem->setIcon( 0, PicassoIcon_Create(
            u"eye", picasso_icon_tone_t::BLUE ) );
        pItem->setCheckState( 0, Qt::Checked );
        pItem->setData( 0, Qt::UserRole, false );
        pItem->setToolTip(
            0,
            tr( "Layer-stack presentation is ready; compositing is wired in the backend pass." ) );
        if ( bLocked ) {
            pItem->setIcon( 1, PicassoIcon_Create(
                u"lock", picasso_icon_tone_t::GOLD ) );
        }
        return pItem;
    };
    addPreviewLayer( tr( "Specular Detail" ), tr( "Overlay" ), 65, true );
    addPreviewLayer( tr( "Roughness Variation" ), tr( "Soft Light" ), 80, true );
    addPreviewLayer( tr( "Weathering" ), tr( "Multiply" ), 45, false );

    m_pBaseLayerItem = new QTreeWidgetItem( m_pLayerTree );
    m_pBaseLayerItem->setText( 0, tr( "Base Image\nNormal 100%" ) );
    m_pBaseLayerItem->setIcon( 0, PicassoIcon_Create(
        u"eye", picasso_icon_tone_t::BLUE ) );
    m_pBaseLayerItem->setCheckState( 0, Qt::Checked );
    m_pBaseLayerItem->setData( 0, Qt::UserRole, true );
    m_pBaseLayerItem->setSelected( true );
    pLayersLayout->addWidget( m_pLayerTree, 1 );

    auto *pOpacityRow = new QWidget( pLayersPanel );
    auto *pOpacityLayout = new QHBoxLayout( pOpacityRow );
    pOpacityLayout->setContentsMargins( 0, 0, 0, 0 );
    pOpacityLayout->addWidget( new QLabel( tr( "Opacity" ), pOpacityRow ) );
    m_pOpacitySlider = new QSlider( Qt::Horizontal, pOpacityRow );
    m_pOpacitySlider->setRange( 0, 100 );
    m_pOpacitySlider->setValue( 100 );
    pOpacityLayout->addWidget( m_pOpacitySlider, 1 );
    pLayersLayout->addWidget( pOpacityRow );

    auto *pLayerCommands = new QWidget( pLayersPanel );
    pLayerCommands->setObjectName( QStringLiteral( "PicassoLayerCommands" ) );
    auto *pLayerCommandLayout = new QHBoxLayout( pLayerCommands );
    pLayerCommandLayout->setContentsMargins( 0, 0, 0, 0 );
    pLayerCommandLayout->setSpacing( 2 );
    for ( const auto &[icon, tooltip] : std::array{
              std::pair{ u"plus", tr( "Add layer" ) },
              std::pair{ u"copy", tr( "Duplicate layer" ) },
              std::pair{ u"trash-2", tr( "Delete layer" ) } } ) {
        auto *pButton = new QToolButton( pLayerCommands );
        const picasso_icon_tone_t tone = QStringView( icon ) == u"trash-2"
            ? picasso_icon_tone_t::RED
            : ( QStringView( icon ) == u"copy"
                ? picasso_icon_tone_t::TEAL
                : picasso_icon_tone_t::BLUE );
        pButton->setIcon( PicassoIcon_Create( icon, tone ) );
        pButton->setToolTip( tooltip );
        pButton->setEnabled( false );
        pLayerCommandLayout->addWidget( pButton );
    }
    pLayerCommandLayout->addStretch( 1 );
    pLayersLayout->addWidget( pLayerCommands );
    pLayersDock->setWidget( pLayersPanel );
    addDockWidget( Qt::RightDockWidgetArea, pLayersDock );
    connect( m_pLayerTree, &QTreeWidget::itemChanged, this,
             [this]( QTreeWidgetItem *pItem, int ) {
        if ( pItem == m_pBaseLayerItem ) {
            m_pCanvas->setLayerVisible( pItem->checkState( 0 ) == Qt::Checked );
        } else {
            setStatusMessage( tr( "Layer compositing will be connected in the backend pass" ) );
        }
    } );
    connect( m_pLayerTree, &QTreeWidget::itemSelectionChanged, this, [this] {
        const QList<QTreeWidgetItem *> selected = m_pLayerTree->selectedItems();
        if ( selected.isEmpty() || selected.front() == m_pBaseLayerItem ) {
            m_pOpacitySlider->setEnabled( true );
            return;
        }
        m_pOpacitySlider->setEnabled( false );
        setStatusMessage( tr( "Preview layer selected; compositing backend is pending" ) );
    } );
    connect( m_pOpacitySlider, &QSlider::valueChanged, this, [this]( int value ) {
        m_pCanvas->setImageOpacity( value / 100.0 );
    } );

    auto *pAuthoringDock = new QDockWidget( tr( "ASSETS / OPERATIONS" ), this );
    pAuthoringDock->setObjectName( QStringLiteral( "PicassoAuthoringDock" ) );
    pAuthoringDock->setMinimumWidth( 330 );
    auto *pAuthoringScroll = new QScrollArea( pAuthoringDock );
    pAuthoringScroll->setWidgetResizable( true );
    pAuthoringScroll->setFrameShape( QFrame::NoFrame );
    auto *pAuthoringPanel = new QWidget( pAuthoringScroll );
    auto *pAuthoringLayout = new QVBoxLayout( pAuthoringPanel );
    pAuthoringLayout->setContentsMargins( 7, 6, 7, 7 );
    pAuthoringLayout->setSpacing( 6 );

    auto makeSectionTitle = []( const QString &text, QWidget *pParent ) {
        auto *pLabel = new QLabel( text, pParent );
        pLabel->setObjectName( QStringLiteral( "PicassoSectionTitle" ) );
        return pLabel;
    };

    // The material section is backed by PicassoPaintMaterial. It displays only
    // bindings that survived CYKV parsing and schema validation; no UI row is
    // synthesized from a filename guess.
    pAuthoringLayout->addWidget( makeSectionTitle(
        tr( "ACTIVE MATERIAL" ), pAuthoringPanel ) );
    auto *pMaterialCommands = new QWidget( pAuthoringPanel );
    auto *pMaterialCommandLayout = new QHBoxLayout( pMaterialCommands );
    pMaterialCommandLayout->setContentsMargins( 0, 0, 0, 0 );
    pMaterialCommandLayout->setSpacing( 3 );
    auto *pNewMaterial = new QToolButton( pMaterialCommands );
    pNewMaterial->setDefaultAction( m_pNewMaterialAction );
    pNewMaterial->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    auto *pOpenMaterial = new QToolButton( pMaterialCommands );
    pOpenMaterial->setDefaultAction( m_pOpenMaterialAction );
    pOpenMaterial->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    pMaterialCommandLayout->addWidget( pNewMaterial );
    pMaterialCommandLayout->addWidget( pOpenMaterial );
    pMaterialCommandLayout->addStretch( 1 );
    pAuthoringLayout->addWidget( pMaterialCommands );

    m_pMaterialName = new QLabel( pAuthoringPanel );
    m_pMaterialName->setObjectName( QStringLiteral( "PicassoMaterialName" ) );
    m_pMaterialShader = new QLabel( pAuthoringPanel );
    m_pMaterialShader->setObjectName( QStringLiteral( "PicassoMutedText" ) );
    m_pMaterialShader->setTextInteractionFlags( Qt::TextSelectableByMouse );
    pAuthoringLayout->addWidget( m_pMaterialName );
    pAuthoringLayout->addWidget( m_pMaterialShader );

    m_pMaterialChannels = new QTreeWidget( pAuthoringPanel );
    m_pMaterialChannels->setObjectName(
        QStringLiteral( "PicassoMaterialChannels" ) );
    m_pMaterialChannels->setColumnCount( 2 );
    m_pMaterialChannels->setHeaderLabels( {
        tr( "Channel" ), tr( "Source" )
    } );
    m_pMaterialChannels->setRootIsDecorated( false );
    m_pMaterialChannels->setAlternatingRowColors( true );
    m_pMaterialChannels->setSelectionMode(
        QAbstractItemView::SingleSelection );
    m_pMaterialChannels->setMinimumHeight( 132 );
    m_pMaterialChannels->setColumnWidth( 0, 112 );
    pAuthoringLayout->addWidget( m_pMaterialChannels );

    pAuthoringLayout->addWidget( makeSectionTitle( tr( "GENERATORS" ), pAuthoringPanel ) );
    auto *pGeneratorSearch = new QLineEdit( pAuthoringPanel );
    pGeneratorSearch->setObjectName( QStringLiteral( "PicassoGeneratorSearch" ) );
    pGeneratorSearch->setPlaceholderText( tr( "Search generators" ) );
    pGeneratorSearch->addAction(
        PicassoIcon_Create( u"search" ), QLineEdit::LeadingPosition );
    pAuthoringLayout->addWidget( pGeneratorSearch );
    auto *pGenerators = new QListWidget( pAuthoringPanel );
    pGenerators->setObjectName( QStringLiteral( "PicassoGeneratorList" ) );
    pGenerators->setIconSize( QSize( 21, 21 ) );
    pGenerators->setMinimumHeight( 184 );
    int iGenerator = 0;
    for ( const auto &[title, detail] : std::array{
              std::pair{ tr( "Perlin Noise" ), tr( "Classic smooth noise" ) },
              std::pair{ tr( "Simplex" ), tr( "Fast continuous noise" ) },
              std::pair{ tr( "Worley" ), tr( "Cellular surface patterns" ) },
              std::pair{ tr( "Brick Pattern" ), tr( "Parametric masonry" ) } } ) {
        auto *pItem = new QListWidgetItem(
            PicassoIcon_Create(
                u"zap",
                iGenerator % 2 == 0
                    ? picasso_icon_tone_t::TEAL
                    : picasso_icon_tone_t::GOLD ),
            QStringLiteral( "%1\n%2" ).arg( title, detail ),
            pGenerators );
        pItem->setSizeHint( QSize( 0, 43 ) );
        ++iGenerator;
    }
    pAuthoringLayout->addWidget( pGenerators );
    connect( pGeneratorSearch, &QLineEdit::textChanged,
             this, [pGenerators]( const QString &text ) {
        for ( int iItem = 0; iItem < pGenerators->count(); ++iItem ) {
            QListWidgetItem *pItem = pGenerators->item( iItem );
            pItem->setHidden( !pItem->text().contains(
                text, Qt::CaseInsensitive ) );
        }
    } );
    connect( pGenerators, &QListWidget::itemClicked, this,
             [this]( QListWidgetItem *pItem ) {
        setStatusMessage( tr( "Generator selected: %1" )
            .arg( pItem->text().section( QLatin1Char( '\n' ), 0, 0 ) ) );
    } );

    pAuthoringLayout->addWidget( makeSectionTitle( tr( "PRESETS" ), pAuthoringPanel ) );
    auto *pPresetCategories = new QWidget( pAuthoringPanel );
    auto *pCategoryLayout = new QHBoxLayout( pPresetCategories );
    pCategoryLayout->setContentsMargins( 0, 0, 0, 0 );
    pCategoryLayout->setSpacing( 1 );
    auto *pCategoryGroup = new QButtonGroup( pPresetCategories );
    pCategoryGroup->setExclusive( true );
    for ( const QString &category : {
              tr( "All" ), tr( "Metal" ), tr( "Wood" ),
              tr( "Stone" ), tr( "Fabric" ) } ) {
        auto *pButton = new QToolButton( pPresetCategories );
        pButton->setText( category );
        pButton->setCheckable( true );
        pButton->setProperty( "segment", true );
        pButton->setChecked( category == tr( "All" ) );
        pCategoryGroup->addButton( pButton );
        pCategoryLayout->addWidget( pButton );
    }
    pCategoryLayout->addStretch( 1 );
    pAuthoringLayout->addWidget( pPresetCategories );

    auto *pPresetGrid = new QWidget( pAuthoringPanel );
    auto *pPresetLayout = new QGridLayout( pPresetGrid );
    pPresetLayout->setContentsMargins( 0, 0, 0, 0 );
    pPresetLayout->setSpacing( 5 );
    struct preset_t { const char *pName; QColor color; };
    const preset_t presets[]{
        { "Brushed Steel", QColor( 121, 126, 130 ) },
        { "Rusted Iron", QColor( 134, 70, 43 ) },
        { "Weathered Wood", QColor( 101, 77, 51 ) },
        { "Concrete", QColor( 132, 132, 126 ) }
    };
    auto *pPresetGroup = new QButtonGroup( pPresetGrid );
    pPresetGroup->setExclusive( true );
    for ( int iPreset = 0; iPreset < 4; ++iPreset ) {
        QPixmap preview( 126, 43 );
        preview.fill( presets[iPreset].color );
        QPainter painter( &preview );
        painter.setPen( QColor( 255, 255, 255, 28 ) );
        for ( int y = 7; y < preview.height(); y += 8 ) {
            painter.drawLine( 0, y, preview.width(), y );
        }
        auto *pButton = new QToolButton( pPresetGrid );
        pButton->setObjectName( QStringLiteral( "PicassoPresetButton" ) );
        pButton->setIcon( QIcon( preview ) );
        pButton->setIconSize( preview.size() );
        pButton->setText( QString::fromLatin1( presets[iPreset].pName ) );
        pButton->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
        pButton->setCheckable( true );
        pButton->setChecked( iPreset == 0 );
        pPresetGroup->addButton( pButton );
        connect( pButton, &QToolButton::clicked, this,
                 [this, pButton, iPreset] {
            activateMaterialPreset( pButton->text(), iPreset );
        } );
        pPresetLayout->addWidget( pButton, iPreset / 2, iPreset % 2 );
    }
    pAuthoringLayout->addWidget( pPresetGrid );

    pAuthoringLayout->addWidget( makeSectionTitle( tr( "FILTER CHAIN" ), pAuthoringPanel ) );
    auto *pFilterHint = new QLabel(
        tr( "Applied in order (top to bottom)" ), pAuthoringPanel );
    pFilterHint->setObjectName( QStringLiteral( "PicassoMutedText" ) );
    pAuthoringLayout->addWidget( pFilterHint );
    auto *pFilters = new QListWidget( pAuthoringPanel );
    pFilters->setObjectName( QStringLiteral( "PicassoFilterChain" ) );
    pFilters->setIconSize( QSize( 20, 20 ) );
    pFilters->setMinimumHeight( 146 );
    pFilters->setDragDropMode( QAbstractItemView::InternalMove );
    pFilters->setDefaultDropAction( Qt::MoveAction );
    pFilters->setSelectionMode( QAbstractItemView::SingleSelection );
    auto addFilterItem = [pFilters](
        const QString &name,
        const QString &type ) {
        auto *pItem = new QListWidgetItem(
            PicassoIcon_Create(
                u"sliders-horizontal", picasso_icon_tone_t::TEAL ),
            QStringLiteral( "%1\n%2" ).arg( name, type ),
            pFilters );
        pItem->setFlags(
            pItem->flags() |
            Qt::ItemIsUserCheckable |
            Qt::ItemIsDragEnabled |
            Qt::ItemIsDropEnabled );
        pItem->setCheckState( Qt::Checked );
        pItem->setSizeHint( QSize( 0, 44 ) );
        pItem->setToolTip( QObject::tr(
            "Drag to reorder. Uncheck to bypass this operation." ) );
        return pItem;
    };
    addFilterItem( tr( "Perlin Noise" ), tr( "Generator" ) );
    addFilterItem( tr( "Blur" ), tr( "Filter" ) );
    addFilterItem( tr( "Color Balance" ), tr( "Filter" ) );
    pFilters->setCurrentRow( 0 );
    pAuthoringLayout->addWidget( pFilters );
    auto *pAddFilter = new QPushButton( tr( "Add Filter" ), pAuthoringPanel );
    pAddFilter->setIcon( PicassoIcon_Create(
        u"plus", picasso_icon_tone_t::GREEN ) );
    auto *pFilterMenu = new QMenu( pAddFilter );
    for ( const QString &filter : {
              tr( "Blur" ), tr( "Sharpen" ), tr( "Levels" ),
              tr( "Color Balance" ), tr( "Normal Map" ) } ) {
        QAction *pFilterAction = pFilterMenu->addAction(
            PicassoIcon_Create(
                u"sliders-horizontal", picasso_icon_tone_t::TEAL ),
            filter );
        connect( pFilterAction, &QAction::triggered, this,
                 [this, addFilterItem, filter] {
            addFilterItem( filter, tr( "Filter (preview)" ) );
            setStatusMessage( tr( "%1 added to the preview filter chain" )
                .arg( filter ) );
        } );
    }
    pAddFilter->setMenu( pFilterMenu );
    pAuthoringLayout->addWidget( pAddFilter );
    connect( pFilters, &QListWidget::itemChanged, this,
             [this]( QListWidgetItem *pItem ) {
        setStatusMessage( pItem->checkState() == Qt::Checked
            ? tr( "Filter enabled in the frontend chain" )
            : tr( "Filter bypassed in the frontend chain" ) );
    } );
    connect( pGenerators, &QListWidget::itemDoubleClicked, this,
             [this, addFilterItem]( QListWidgetItem *pItem ) {
        const QString generator = pItem->text().section(
            QLatin1Char( '\n' ), 0, 0 );
        addFilterItem( generator, tr( "Generator (preview)" ) );
        setStatusMessage( tr( "%1 added to the preview filter chain" )
            .arg( generator ) );
    } );

    auto *pProperties = new QGroupBox( tr( "Texture Properties" ), pAuthoringPanel );
    auto *pPropertiesLayout = new QVBoxLayout( pProperties );
    pPropertiesLayout->addWidget( MakePropertyRow(
        tr( "Dimensions" ), &m_pDimensionsValue, pProperties ) );
    pPropertiesLayout->addWidget( MakePropertyRow(
        tr( "Format" ), &m_pFormatValue, pProperties ) );
    pPropertiesLayout->addWidget( MakePropertyRow(
        tr( "Color space" ), &m_pColorSpaceValue, pProperties ) );
    pPropertiesLayout->addWidget( MakePropertyRow(
        tr( "Source" ), &m_pSourceValue, pProperties ) );
    pAuthoringLayout->addWidget( pProperties );
    pAuthoringLayout->addStretch( 1 );
    pAuthoringScroll->setWidget( pAuthoringPanel );
    pAuthoringDock->setWidget( pAuthoringScroll );
    addDockWidget( Qt::LeftDockWidgetArea, pAuthoringDock );

    auto *pPreviewDock = new QDockWidget( tr( "MATERIAL PREVIEW" ), this );
    pPreviewDock->setObjectName( QStringLiteral( "PicassoPreviewDock" ) );
    pPreviewDock->setMinimumWidth( 270 );
    auto *pPreviewPanel = new QWidget( pPreviewDock );
    auto *pPreviewLayout = new QVBoxLayout( pPreviewPanel );
    pPreviewLayout->setContentsMargins( 7, 7, 7, 7 );
    pPreviewLayout->setSpacing( 6 );
    m_pPreview = new PicassoSwatchPreview( pPreviewPanel );
    pPreviewLayout->addWidget( m_pPreview, 1 );
    auto *pShapeRow = new QWidget( pPreviewPanel );
    auto *pShapeLayout = new QHBoxLayout( pShapeRow );
    pShapeLayout->setContentsMargins( 0, 0, 0, 0 );
    pShapeLayout->setSpacing( 2 );
    auto *pShapeGroup = new QButtonGroup( pShapeRow );
    pShapeGroup->setExclusive( true );
    for ( const auto &[name, icon] : std::array{
              std::pair{ tr( "Sphere" ), u"circle" },
              std::pair{ tr( "Cube" ), u"box" },
              std::pair{ tr( "Plane" ), u"grid-3x3" } } ) {
        auto *pButton = new QToolButton( pShapeRow );
        pButton->setText( name );
        const picasso_icon_tone_t tone = name == tr( "Sphere" )
            ? picasso_icon_tone_t::GOLD
            : ( name == tr( "Cube" )
                ? picasso_icon_tone_t::BLUE
                : picasso_icon_tone_t::TEAL );
        pButton->setIcon( PicassoIcon_Create( icon, tone ) );
        pButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        pButton->setCheckable( true );
        pButton->setProperty( "segment", true );
        pButton->setChecked( name == tr( "Sphere" ) );
        pShapeGroup->addButton( pButton );
        pShapeLayout->addWidget( pButton, 1 );
    }
    pPreviewLayout->addWidget( pShapeRow );
    auto *pPreviewForm = new QFormLayout();
    pPreviewForm->setContentsMargins( 0, 4, 0, 0 );
    auto *pRotationX = new QDoubleSpinBox( pPreviewPanel );
    auto *pRotationY = new QDoubleSpinBox( pPreviewPanel );
    for ( QDoubleSpinBox *pRotation : { pRotationX, pRotationY } ) {
        pRotation->setRange( -180.0, 180.0 );
        pRotation->setSuffix( QStringLiteral( "°" ) );
    }
    pRotationX->setValue( 35.0 );
    pRotationY->setValue( 45.0 );
    auto *pRotationRow = new QWidget( pPreviewPanel );
    auto *pRotationLayout = new QHBoxLayout( pRotationRow );
    pRotationLayout->setContentsMargins( 0, 0, 0, 0 );
    pRotationLayout->addWidget( new QLabel( tr( "X" ), pRotationRow ) );
    pRotationLayout->addWidget( pRotationX );
    pRotationLayout->addWidget( new QLabel( tr( "Y" ), pRotationRow ) );
    pRotationLayout->addWidget( pRotationY );
    pPreviewForm->addRow( tr( "Light Rotation" ), pRotationRow );
    auto *pRoughness = new QSlider( Qt::Horizontal, pPreviewPanel );
    pRoughness->setRange( 0, 100 );
    pRoughness->setValue( 85 );
    auto *pMetalness = new QSlider( Qt::Horizontal, pPreviewPanel );
    pMetalness->setRange( 0, 100 );
    pMetalness->setValue( 0 );
    pPreviewForm->addRow( tr( "Roughness" ), pRoughness );
    pPreviewForm->addRow( tr( "Metalness" ), pMetalness );
    pPreviewLayout->addLayout( pPreviewForm );
    pPreviewDock->setWidget( pPreviewPanel );
    addDockWidget( Qt::RightDockWidgetArea, pPreviewDock );
    splitDockWidget( pLayersDock, pPreviewDock, Qt::Vertical );

    auto *pHistoryDock = new QDockWidget( tr( "UNDO HISTORY" ), this );
    pHistoryDock->setObjectName( QStringLiteral( "PicassoHistoryDock" ) );
    pHistoryDock->setMinimumWidth( 270 );
    m_pHistoryList = new QListWidget( pHistoryDock );
    m_pHistoryList->setObjectName( QStringLiteral( "PicassoHistoryList" ) );
    pHistoryDock->setWidget( m_pHistoryList );
    addDockWidget( Qt::RightDockWidgetArea, pHistoryDock );
    tabifyDockWidget( pLayersDock, pHistoryDock );
    pLayersDock->raise();
    connect( m_pHistoryList, &QListWidget::itemClicked,
             this, [this]( QListWidgetItem *pItem ) {
        jumpToHistory( static_cast<usize>(
            pItem->data( Qt::UserRole ).toULongLong() ) );
    } );

    m_pConsoleDock = new QDockWidget( tr( "CONSOLE" ), this );
    m_pConsoleDock->setObjectName( QStringLiteral( "PicassoConsoleDock" ) );
    m_pConsole = new PicassoConsole( m_pConsoleDock );
    m_pConsole->setMinimumHeight( 170 );
    m_pConsole->setExecuteCallback( [this]( const QString &line ) {
        executeCommandLine( line );
    } );
    m_pConsole->setCompleteCallback( [this]( const QString &partial ) {
        return completeCommandLine( partial );
    } );
    m_pConsoleDock->setWidget( m_pConsole );
    addDockWidget( Qt::BottomDockWidgetArea, m_pConsoleDock );
    connect( m_pConsoleDock, &QDockWidget::visibilityChanged,
             this, [this]( bool bVisible ) {
        m_pToggleConsoleAction->setChecked( bVisible );
    } );

    statusBar()->setObjectName( QStringLiteral( "PicassoStatusBar" ) );
    m_pStatusMessage = new QLabel( tr( "Ready" ), this );
    m_pStatusMessage->setObjectName( "AccentLabel" );
    m_pStatusDimensions = new QLabel( this );
    m_pStatusPixel = new QLabel( this );
    m_pStatusBrush = new QLabel( this );
    m_pStatusMemory = new QLabel( this );
    m_pStatusZoom = new QLabel( tr( "100%" ), this );
    statusBar()->addWidget( m_pStatusMessage, 1 );
    statusBar()->addPermanentWidget( m_pStatusPixel );
    statusBar()->addPermanentWidget( m_pStatusBrush );
    statusBar()->addPermanentWidget( m_pStatusDimensions );
    statusBar()->addPermanentWidget( m_pStatusMemory );
    statusBar()->addPermanentWidget( m_pStatusZoom );

    resizeDocks( { pAuthoringDock, pLayersDock }, { 318, 292 }, Qt::Horizontal );
    resizeDocks( { pLayersDock, pPreviewDock }, { 350, 360 }, Qt::Vertical );
    resizeDocks( { m_pConsoleDock }, { 190 }, Qt::Vertical );
    refreshToolContext();
}

void PicassoMainWindow::buildToolRail()
{
    m_pToolRail = new QToolBar( tr( "Texture Tools" ), this );
    m_pToolRail->setObjectName( QStringLiteral( "PicassoToolRail" ) );
    m_pToolRail->setMovable( false );
    m_pToolRail->setFloatable( false );
    m_pToolRail->setIconSize( QSize( 26, 26 ) );
    m_pToolRail->setToolButtonStyle( Qt::ToolButtonIconOnly );
    m_pToolRail->setOrientation( Qt::Vertical );

    int previousGroup = -1;
    for ( QAction *pAction : m_pToolActions->actions() ) {
        const int group = pAction->property( "PicassoToolGroup" ).toInt();
        if ( previousGroup >= 0 && group != previousGroup ) {
            m_pToolRail->addSeparator();
        }
        m_pToolRail->addAction( pAction );
        previousGroup = group;
    }
    m_pToolRail->addSeparator();
    m_pToolRail->addAction( m_pFitAction );

    auto *pRailSpacer = new QWidget( m_pToolRail );
    pRailSpacer->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Expanding );
    m_pToolRail->addWidget( pRailSpacer );
    auto *pColorSwatches = new PicassoColorSwatches( m_pToolRail );
    m_pColorSwatches = pColorSwatches;
    pColorSwatches->setObjectName(
        QStringLiteral( "PicassoColorSwatches" ) );
    pColorSwatches->setColorChangedCallback( [this]( const QColor &color ) {
        m_paintColor = color;
        setStatusMessage( tr( "Paint color: %1" ).arg( color.name(
            QColor::HexArgb ) ) );
    } );
    m_pToolRail->addWidget( pColorSwatches );
    addToolBar( Qt::LeftToolBarArea, m_pToolRail );
}

void PicassoMainWindow::restoreWorkspace()
{
    QSettings settings;
    const QByteArray state = settings.value(
        QStringLiteral( "Picasso/windowStateV4" ) ).toByteArray();
    if ( state.isEmpty() ) {
        return;
    }

    restoreGeometry( settings.value(
        QStringLiteral( "Picasso/geometryV4" ) ).toByteArray() );
    restoreState( state, 4 );
}

void PicassoMainWindow::initializeCommandSystem()
{
    command_system_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    desc.nInitialCommands = 32u;
    desc.nInitialConVars = 8u;
    desc.bCaseInsensitiveAscii = CY_TRUE;
    desc.pfnOutput = &PicassoMainWindow::CommandOutputCallback;
    desc.pOutputUserData = this;
    m_pCommandSystem = CommandSystem_Create( desc );
    if ( m_pCommandSystem == nullptr ) {
        appendError( tr( "The Picasso command system could not be initialized." ) );
    }
}

void PicassoMainWindow::registerCommands()
{
    if ( m_pCommandSystem == nullptr ) {
        return;
    }

    struct command_spec_t {
        const char *pName;
        const char *pHelp;
        const char *pUsage;
    };
    static constexpr command_spec_t commands[]{
        { "help", "Lists commands or describes one command.", "help [command]" },
        { "console.clear", "Clears console output.", "console.clear" },
        { "document.status", "Prints active texture document state.", "document.status" },
        { "file.new", "Creates a texture or opens the New Texture dialog.",
          "file.new [width height [checker|solid]]" },
        { "file.open", "Opens an image or displays the native file dialog.",
          "file.open [path]" },
        { "file.save", "Saves as PNG using the current or supplied path.",
          "file.save [path]" },
        { "material.new", "Creates a neutral Picasso paint material.",
          "material.new" },
        { "material.open", "Opens and validates a .cymat material.",
          "material.open [path]" },
        { "material.status", "Prints the active material and channel bindings.",
          "material.status" },
        { "texture.compile", "Validates and compiles the active texture document.",
          "texture.compile" },
        { "edit.undo", "Undoes the most recent document operation.", "edit.undo" },
        { "edit.redo", "Redoes the next document operation.", "edit.redo" },
        { "image.flip-h", "Flips the texture horizontally.", "image.flip-h" },
        { "image.flip-v", "Flips the texture vertically.", "image.flip-v" },
        { "image.rotate-left", "Rotates the texture 90 degrees left.",
          "image.rotate-left" },
        { "image.rotate-right", "Rotates the texture 90 degrees right.",
          "image.rotate-right" },
        { "image.rotate-180", "Rotates the texture 180 degrees.",
          "image.rotate-180" },
        { "view.fit", "Fits the texture inside the canvas.", "view.fit" },
        { "view.actual", "Displays one image pixel per screen pixel.",
          "view.actual" },
        { "view.zoom-in", "Increases canvas magnification.", "view.zoom-in" },
        { "view.zoom-out", "Decreases canvas magnification.", "view.zoom-out" },
        { "tool.set", "Selects the active canvas interaction tool.",
          "tool.set <select|marquee|lasso|move|brush|eraser|fill|gradient|eyedropper|clone|crop|seam|mask|pan|zoom|inspect>" },
        { "channel.set", "Selects the displayed texture channel.",
          "channel.set <rgba|r|g|b|a>" }
    };

    for ( const command_spec_t &spec : commands ) {
        concommand_desc_t desc{};
        desc.name = StringView_FromCString( spec.pName );
        desc.help = StringView_FromCString( spec.pHelp );
        desc.usage = StringView_FromCString( spec.pUsage );
        desc.pfnExecute = &PicassoMainWindow::ExecuteCommandCallback;
        if ( std::string_view( spec.pName ) == "channel.set" ) {
            desc.pfnComplete = &PicassoMainWindow::CompleteChannelCallback;
        } else if ( std::string_view( spec.pName ) == "tool.set" ) {
            desc.pfnComplete = &PicassoMainWindow::CompleteToolCallback;
        }
        desc.pUserData = this;
        const command_register_result_t result =
            CommandSystem_RegisterCommand( m_pCommandSystem, desc );
        if ( Cy_ErrorFailed( result.error ) ) {
            appendError( tr( "Could not register command '%1': %2" )
                .arg(
                    QString::fromLatin1( spec.pName ),
                    ErrorName( result.error ) ) );
        }
    }
}

void PicassoMainWindow::executeCommandLine( const QString &line )
{
    if ( m_pCommandSystem == nullptr ) {
        appendError( tr( "The command system is unavailable." ) );
        return;
    }

    const QByteArray utf8 = line.toUtf8();
    const string_view_t commandLine{
        utf8.constData(),
        static_cast<usize>( utf8.size() )
    };
    command_context_t context{};
    context.source = command_source_t::LOCAL_CONSOLE;
    context.bDevelopmentAllowed = CY_TRUE;
    context.pUserData = this;
    const error_code_t error = CommandSystem_ExecuteLine(
        m_pCommandSystem,
        commandLine,
        context );
    if ( Cy_ErrorFailed( error ) ) {
        appendError( tr( "Command failed: %1" )
            .arg( ErrorName( error ) ) );
    }
}

QStringList PicassoMainWindow::completeCommandLine( const QString &partial )
{
    if ( m_pCommandSystem == nullptr ) {
        return {};
    }

    const QByteArray utf8 = partial.toUtf8();
    const string_view_t partialView{
        utf8.constData(),
        static_cast<usize>( utf8.size() )
    };
    std::array<string_view_t, 64u> suggestions{};
    const usize nRequired = CommandSystem_Complete(
        m_pCommandSystem,
        partialView,
        suggestions.data(),
        suggestions.size() );
    const usize nWritten = std::min( nRequired, suggestions.size() );
    qsizetype iSeparator = -1;
    for ( qsizetype iCharacter = 0; iCharacter < partial.size(); ++iCharacter ) {
        if ( partial.at( iCharacter ).isSpace() ) {
            iSeparator = iCharacter;
            break;
        }
    }
    const QString argumentPrefix = iSeparator >= 0
        ? partial.left( iSeparator + 1 )
        : QString{};

    QStringList result;
    result.reserve( static_cast<qsizetype>( nWritten ) );
    for ( usize iSuggestion = 0u; iSuggestion < nWritten; ++iSuggestion ) {
        result.append( argumentPrefix + StringFromView( suggestions[iSuggestion] ) );
    }
    return result;
}

error_code_t PicassoMainWindow::dispatchCommand(
    const command_args_t &args ) noexcept
{
    if ( args.nArgumentCount == 0u ) {
        return InvalidCommandArguments();
    }
    const string_view_t name = args.arguments[0];

    if ( CommandNameIs( name, "help" ) ) {
        if ( args.nArgumentCount > 2u ) {
            return InvalidCommandArguments();
        }
        printCommandHelp(
            args.nArgumentCount == 2u ? args.arguments[1] : string_view_t{} );
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "console.clear" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        m_pConsole->clearRecords();
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "document.status" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        if ( !PicassoTextureDocument_IsOpen( &m_document ) ) {
            appendInfo( tr( "No texture document is open." ) );
            return CY_ERROR_OK;
        }
        const image_surface_t *pPrimary =
            PicassoTextureDocument_PrimarySurface( &m_document );
        const image_desc_t &desc = pPrimary->desc;
        appendInfo( tr( "%1 x %2, %3, %4, zoom %5%" )
            .arg( desc.extent.nWidth )
            .arg( desc.extent.nHeight )
            .arg( QString::fromLatin1( ImageFormat_Name( desc.pixelFormat ) ) )
            .arg( PicassoTextureDocument_IsDirty( &m_document )
                ? tr( "modified" ) : tr( "saved" ) )
            .arg( qRound( m_pCanvas->zoom() * 100.0 ) ) );
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "file.new" ) ) {
        if ( args.nArgumentCount == 1u ) {
            newTexture();
            return CY_ERROR_OK;
        }
        if ( args.nArgumentCount < 3u || args.nArgumentCount > 4u ) {
            return InvalidCommandArguments();
        }
        picasso_canvas_desc_t desc{};
        const string_parse_options_t options{};
        if ( !StringParse_Succeeded( StringParse_U32(
                 args.arguments[1], options, &desc.nWidth ) ) ||
             !StringParse_Succeeded( StringParse_U32(
                 args.arguments[2], options, &desc.nHeight ) ) ) {
            return InvalidCommandArguments();
        }
        if ( args.nArgumentCount == 4u ) {
            if ( StringView_EqualsInsensitiveAscii(
                     args.arguments[3], StringView_FromCString( "solid" ) ) ) {
                desc.fill = picasso_canvas_fill_t::SOLID;
            } else if ( !StringView_EqualsInsensitiveAscii(
                            args.arguments[3],
                            StringView_FromCString( "checker" ) ) ) {
                return InvalidCommandArguments();
            }
        }
        if ( !maybeSave() ) {
            return CY_ERROR_OK;
        }
        return createTexture( desc )
            ? CY_ERROR_OK
            : Cy_ErrorMake( common_error_t::ERR_FAILED );
    }
    if ( CommandNameIs( name, "file.open" ) ) {
        if ( args.nArgumentCount == 1u ) {
            openTexture();
            return CY_ERROR_OK;
        }
        if ( args.nArgumentCount != 2u ) {
            return InvalidCommandArguments();
        }
        if ( !maybeSave() ) {
            return CY_ERROR_OK;
        }
        return openTextureFromPath( StringFromView( args.arguments[1] ) )
            ? CY_ERROR_OK
            : Cy_ErrorMake( common_error_t::ERR_IO_ERROR );
    }
    if ( CommandNameIs( name, "file.save" ) ) {
        if ( args.nArgumentCount == 1u ) {
            return saveTexture()
                ? CY_ERROR_OK
                : Cy_ErrorMake( common_error_t::ERR_IO_ERROR );
        }
        if ( args.nArgumentCount == 2u ) {
            return saveTextureTo( StringFromView( args.arguments[1] ) )
                ? CY_ERROR_OK
                : Cy_ErrorMake( common_error_t::ERR_IO_ERROR );
        }
        return InvalidCommandArguments();
    }
    if ( CommandNameIs( name, "material.new" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        newMaterial();
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "material.open" ) ) {
        if ( args.nArgumentCount == 1u ) {
            openMaterial();
            return CY_ERROR_OK;
        }
        if ( args.nArgumentCount != 2u ) {
            return InvalidCommandArguments();
        }
        return openMaterialFromPath( StringFromView( args.arguments[1] ) )
            ? CY_ERROR_OK
            : Cy_ErrorMake( common_error_t::ERR_IO_ERROR );
    }
    if ( CommandNameIs( name, "material.status" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        appendOutput(
            tr( "%1 | shader: %2 | %3 channel(s)" )
                .arg(
                    StringFromView( FixedString_View( m_material.name ) ),
                    FixedString_IsEmpty( m_material.shader )
                        ? tr( "unassigned" )
                        : StringFromView( FixedString_View( m_material.shader ) ) )
                .arg( PicassoPaintMaterial_ChannelCount( &m_material ) ),
            picasso_console_record_t::INFO,
            picasso_console_channel_t::MATERIAL );
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "texture.compile" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        compileTexture();
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "edit.undo" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        undo();
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "edit.redo" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        redo();
        return CY_ERROR_OK;
    }

    picasso_texture_operation_t operation{};
    bool_t bImageOperation = CY_TRUE;
    if ( CommandNameIs( name, "image.flip-h" ) ) {
        operation = picasso_texture_operation_t::FLIP_HORIZONTAL;
    } else if ( CommandNameIs( name, "image.flip-v" ) ) {
        operation = picasso_texture_operation_t::FLIP_VERTICAL;
    } else if ( CommandNameIs( name, "image.rotate-left" ) ) {
        operation = picasso_texture_operation_t::ROTATE_90_COUNTER_CLOCKWISE;
    } else if ( CommandNameIs( name, "image.rotate-right" ) ) {
        operation = picasso_texture_operation_t::ROTATE_90_CLOCKWISE;
    } else if ( CommandNameIs( name, "image.rotate-180" ) ) {
        operation = picasso_texture_operation_t::ROTATE_180;
    } else {
        bImageOperation = CY_FALSE;
    }
    if ( bImageOperation ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        applyOperation( operation );
        return CY_ERROR_OK;
    }

    if ( CommandNameIs( name, "view.fit" ) ||
         CommandNameIs( name, "view.actual" ) ||
         CommandNameIs( name, "view.zoom-in" ) ||
         CommandNameIs( name, "view.zoom-out" ) ) {
        if ( args.nArgumentCount != 1u ) {
            return InvalidCommandArguments();
        }
        if ( CommandNameIs( name, "view.fit" ) ) {
            m_pCanvas->fitToView();
        } else if ( CommandNameIs( name, "view.actual" ) ) {
            m_pCanvas->actualSize();
        } else if ( CommandNameIs( name, "view.zoom-in" ) ) {
            m_pCanvas->zoomIn();
        } else {
            m_pCanvas->zoomOut();
        }
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "channel.set" ) ) {
        if ( args.nArgumentCount != 2u ) {
            return InvalidCommandArguments();
        }
        picasso_channel_mode_t mode = picasso_channel_mode_t::RGBA;
        QAction *pAction = m_pChannelRgbaAction;
        const string_view_t channel = args.arguments[1];
        if ( StringView_EqualsInsensitiveAscii(
                 channel, StringView_FromCString( "r" ) ) ) {
            mode = picasso_channel_mode_t::RED;
            pAction = m_pChannelRedAction;
        } else if ( StringView_EqualsInsensitiveAscii(
                        channel, StringView_FromCString( "g" ) ) ) {
            mode = picasso_channel_mode_t::GREEN;
            pAction = m_pChannelGreenAction;
        } else if ( StringView_EqualsInsensitiveAscii(
                        channel, StringView_FromCString( "b" ) ) ) {
            mode = picasso_channel_mode_t::BLUE;
            pAction = m_pChannelBlueAction;
        } else if ( StringView_EqualsInsensitiveAscii(
                        channel, StringView_FromCString( "a" ) ) ) {
            mode = picasso_channel_mode_t::ALPHA;
            pAction = m_pChannelAlphaAction;
        } else if ( !StringView_EqualsInsensitiveAscii(
                        channel, StringView_FromCString( "rgba" ) ) ) {
            return InvalidCommandArguments();
        }
        pAction->setChecked( true );
        m_pCanvas->setChannelMode( mode );
        appendInfo( tr( "Display channel: %1" ).arg( pAction->text() ) );
        return CY_ERROR_OK;
    }
    if ( CommandNameIs( name, "tool.set" ) ) {
        if ( args.nArgumentCount != 2u ) {
            return InvalidCommandArguments();
        }
        const QString requested = StringFromView( args.arguments[1] );
        QAction *pAction = nullptr;
        for ( QAction *pCandidate : m_pToolActions->actions() ) {
            if ( pCandidate->property( "PicassoToolName" ).toString().compare(
                     requested, Qt::CaseInsensitive ) == 0 ) {
                pAction = pCandidate;
                break;
            }
        }
        if ( pAction == nullptr ) {
            return InvalidCommandArguments();
        }
        const picasso_canvas_tool_t tool = static_cast<picasso_canvas_tool_t>(
            pAction->data().toInt() );
        pAction->setChecked( true );
        m_pCanvas->setTool( tool );
        refreshToolContext();
        setStatusMessage( tr( "%1 tool" ).arg( pAction->text() ) );
        return CY_ERROR_OK;
    }

    return CommandSystem_MakeError( command_system_error_t::NOT_FOUND );
}

void PicassoMainWindow::printCommandHelp( string_view_t commandName )
{
    if ( commandName.cchLength == 0u ) {
        appendInfo( tr( "Available commands:" ) );
        (void)CommandSystem_ForEachCommand(
            m_pCommandSystem,
            &PicassoMainWindow::CommandHelpVisitor,
            this );
        return;
    }

    const command_handle_t handle = CommandSystem_FindCommand(
        m_pCommandSystem,
        commandName );
    concommand_desc_t desc{};
    if ( !Cy_Handle32IsValid( handle ) ||
         !CommandSystem_GetCommandDesc( m_pCommandSystem, handle, &desc ) ) {
        appendError( tr( "Unknown command '%1'." )
            .arg( StringFromView( commandName ) ) );
        return;
    }
    appendOutput(
        QStringLiteral( "%1\n  %2\n  Usage: %3" )
            .arg(
                StringFromView( desc.name ),
                StringFromView( desc.help ),
                StringFromView( desc.usage ) ),
        picasso_console_record_t::INFO,
        picasso_console_channel_t::PICASSO );
}

error_code_t PicassoMainWindow::ExecuteCommandCallback(
    const command_context_t &,
    const command_args_t &args,
    void *pCommandUserData ) noexcept
{
    auto *pWindow = static_cast<PicassoMainWindow *>( pCommandUserData );
    return pWindow != nullptr
        ? pWindow->dispatchCommand( args )
        : InvalidCommandArguments();
}

void PicassoMainWindow::CommandOutputCallback(
    string_view_t text,
    void *pUserData ) noexcept
{
    auto *pWindow = static_cast<PicassoMainWindow *>( pUserData );
    if ( pWindow != nullptr ) {
        pWindow->appendOutput(
            StringFromView( text ),
            picasso_console_record_t::INFO,
            picasso_console_channel_t::PICASSO );
    }
}

bool_t PicassoMainWindow::CommandHelpVisitor(
    command_handle_t,
    const concommand_desc_t &desc,
    void *pUserData ) noexcept
{
    auto *pWindow = static_cast<PicassoMainWindow *>( pUserData );
    if ( pWindow == nullptr ||
         ( desc.flags & CONCOMMAND_FLAG_HIDDEN ) != 0u ) {
        return CY_TRUE;
    }
    pWindow->appendOutput(
        QStringLiteral( "  %1  %2" ).arg(
            StringFromView( desc.name ).leftJustified( 20 ),
            StringFromView( desc.help ) ),
        picasso_console_record_t::INFO,
        picasso_console_channel_t::PICASSO );
    return CY_TRUE;
}

usize PicassoMainWindow::CompleteChannelCallback(
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity,
    void * ) noexcept
{
    static const char *values[]{ "rgba", "r", "g", "b", "a" };
    return CompleteFromLiterals(
        partial,
        pSuggestions,
        nSuggestionCapacity,
        values );
}

usize PicassoMainWindow::CompleteToolCallback(
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity,
    void * ) noexcept
{
    static const char *values[]{
        "select", "marquee", "lasso", "move", "brush", "eraser",
        "fill", "gradient", "eyedropper", "clone", "crop", "seam",
        "mask", "pan", "zoom", "inspect"
    };
    return CompleteFromLiterals(
        partial,
        pSuggestions,
        nSuggestionCapacity,
        values );
}

void PicassoMainWindow::newTexture()
{
    if ( !maybeSave() ) {
        return;
    }
    PicassoNewTextureDialog dialog( this );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }
    createTexture( dialog.canvasDesc() );
}

bool PicassoMainWindow::createTexture( const picasso_canvas_desc_t &desc )
{
    const picasso_document_status_t status = PicassoTextureDocument_Create(
        &m_document,
        desc );
    if ( status != picasso_document_status_t::OK ) {
        QMessageBox::critical(
            this,
            tr( "New Texture" ),
            tr( "The canvas could not be created: %1" )
                .arg( QString::fromLatin1(
                    PicassoTextureDocument_StatusName( status ) ) ) );
        appendError( tr( "Texture creation failed: %1" )
            .arg( QString::fromLatin1(
                PicassoTextureDocument_StatusName( status ) ) ) );
        return false;
    }
    m_currentPath.clear();
    refreshDocument();
    appendInfo( tr( "Created %1 x %2 texture." )
        .arg( desc.nWidth ).arg( desc.nHeight ) );
    return true;
}

void PicassoMainWindow::openTexture()
{
    if ( !maybeSave() ) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr( "Open Texture" ),
        QStandardPaths::writableLocation( QStandardPaths::PicturesLocation ),
        tr( "Images (*.png *.jpg *.jpeg *.tga *.exr);;All Files (*)" ) );
    if ( path.isEmpty() ) {
        return;
    }

    openTextureFromPath( path );
}

bool PicassoMainWindow::openTextureFromPath( const QString &path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        QMessageBox::critical( this, tr( "Open Texture" ), file.errorString() );
        appendError( tr( "Failed to open %1: %2" )
            .arg( QDir::toNativeSeparators( path ), file.errorString() ) );
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QByteArray pathUtf8 = path.toUtf8();
    const binary_block_t encoded{
        reinterpret_cast<const byte *>( bytes.constData() ),
        static_cast<usize>( bytes.size() )
    };
    const string_view_t sourcePath{
        pathUtf8.constData(),
        static_cast<usize>( pathUtf8.size() )
    };
    const picasso_document_status_t status =
        PicassoTextureDocument_OpenEncoded(
            &m_document,
            encoded,
            sourcePath );
    if ( status != picasso_document_status_t::OK ) {
        QMessageBox::critical(
            this,
            tr( "Open Texture" ),
            tr( "The image could not be decoded: %1" )
                .arg( QString::fromLatin1(
                    PicassoTextureDocument_StatusName( status ) ) ) );
        appendError( tr( "Failed to decode %1." )
            .arg( QDir::toNativeSeparators( path ) ) );
        return false;
    }

    m_currentPath = path;
    refreshDocument();
    appendInfo( tr( "Opened %1." ).arg( QDir::toNativeSeparators( path ) ) );
    return true;
}

void PicassoMainWindow::newMaterial()
{
    picasso_paint_material_t pending{};
    if ( PicassoPaintMaterial_Init(
             &pending,
             StringView_FromCString( "Untitled Material" ) ) !=
         picasso_paint_material_status_t::OK ||
         PicassoPaintMaterial_SetConstant(
             &pending,
             picasso_channel_semantic_t::BASE_COLOR,
             { 0.5f, 0.5f, 0.5f, 1.0f } ) !=
         picasso_paint_material_status_t::OK ||
         PicassoPaintMaterial_SetConstant(
             &pending,
             picasso_channel_semantic_t::ROUGHNESS,
             { 0.6f, 0.6f, 0.6f, 1.0f } ) !=
         picasso_paint_material_status_t::OK ) {
        appendError( tr( "The material backend could not create a neutral material." ) );
        return;
    }

    m_material = pending;
    m_currentMaterialPath.clear();
    refreshMaterial();
    appendOutput(
        tr( "Created a neutral material with base-color and roughness channels." ),
        picasso_console_record_t::INFO,
        picasso_console_channel_t::MATERIAL );
}

void PicassoMainWindow::openMaterial()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr( "Open Cypher Material" ),
        QDir::currentPath(),
        tr( "Cypher Materials (*.cymat);;All Files (*)" ) );
    if ( path.isEmpty() ) {
        return;
    }
    (void)openMaterialFromPath( path );
}

bool PicassoMainWindow::openMaterialFromPath( const QString &path )
{
    if ( !path.endsWith( QStringLiteral( ".cymat" ), Qt::CaseInsensitive ) ) {
        appendError( tr( "Material source must use the .cymat extension: %1" )
            .arg( QDir::toNativeSeparators( path ) ) );
        return false;
    }

    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        appendError( tr( "Failed to open material %1: %2" )
            .arg( QDir::toNativeSeparators( path ), file.errorString() ) );
        return false;
    }
    constexpr qint64 PICASSO_MAX_MATERIAL_SOURCE_SIZE = 4ll * 1024ll * 1024ll;
    if ( file.size() < 0 || file.size() > PICASSO_MAX_MATERIAL_SOURCE_SIZE ) {
        appendError( tr( "Material source exceeds the 4 MiB editor limit: %1" )
            .arg( QDir::toNativeSeparators( path ) ) );
        return false;
    }

    const QByteArray bytes = file.readAll();
    if ( bytes.size() != file.size() ) {
        appendError( tr( "Material source could not be read completely: %1" )
            .arg( QDir::toNativeSeparators( path ) ) );
        return false;
    }
    const QByteArray nameUtf8 = QFileInfo( path ).completeBaseName().toUtf8();
    const string_view_t materialName{
        nameUtf8.constData(),
        static_cast<usize>( nameUtf8.size() )
    };
    const string_view_t sourceText{
        bytes.constData(),
        static_cast<usize>( bytes.size() )
    };

    const picasso_material_import_result_t imported =
        PicassoMaterialImport_FromText(
            materialName,
            sourceText,
            &m_material );
    if ( imported.status != picasso_material_import_status_t::OK ) {
        QString detail = QString::fromLatin1(
            PicassoMaterialImport_StatusName( imported.status ) );
        if ( imported.status ==
             picasso_material_import_status_t::CYKV_PARSE_FAILED ) {
            detail = QStringLiteral( "%1 at %2:%3" )
                .arg( QString::fromLatin1(
                    KeyValue_ParseStatusName( imported.parseStatus ) ) )
                .arg( imported.sourceLocation.nLine )
                .arg( imported.sourceLocation.nColumn );
        } else if ( imported.status ==
                    picasso_material_import_status_t::SCHEMA_DECODE_FAILED ) {
            if ( imported.schemaDiagnostic.code !=
                 schema_diagnostic_code_t::NONE ) {
                detail = QStringLiteral( "%1 at %2" )
                    .arg(
                        QString::fromLatin1( Schema_DiagnosticCodeName(
                            imported.schemaDiagnostic.code ) ),
                        QString::fromUtf8(
                            imported.schemaDiagnostic.path ) );
                if ( imported.sourceLocation.nLine != 0u ) {
                    detail += QStringLiteral( " (%1:%2)" )
                        .arg( imported.sourceLocation.nLine )
                        .arg( imported.sourceLocation.nColumn );
                }
            } else {
                detail = QString::fromLatin1(
                    RenderAsset_DecodeStatusName( imported.decodeStatus ) );
            }
        }
        appendOutput(
            tr( "%1: material import failed: %2" )
                .arg( QDir::toNativeSeparators( path ), detail ),
            picasso_console_record_t::ERROR,
            picasso_console_channel_t::MATERIAL );
        return false;
    }

    m_currentMaterialPath = QFileInfo( path ).absoluteFilePath();
    refreshMaterial();
    appendOutput(
        tr( "Opened %1: %2 standard texture binding(s)." )
            .arg( QFileInfo( path ).fileName() )
            .arg( imported.nTexturesImported ),
        picasso_console_record_t::INFO,
        picasso_console_channel_t::MATERIAL );
    if ( imported.nTexturesSkipped != 0u ||
         imported.nParametersSkipped != 0u ) {
        appendWarning(
            tr( "%1 shader-specific texture binding(s) and %2 parameter(s) remain in .cymat without guessed Picasso semantics." )
                .arg( imported.nTexturesSkipped )
                .arg( imported.nParametersSkipped ) );
    }
    return true;
}

void PicassoMainWindow::activateMaterialPreset(
    const QString &name,
    int iPreset )
{
    static constexpr colorf_t BASE_COLORS[]{
        { 0.19f, 0.21f, 0.23f, 1.0f },
        { 0.31f, 0.10f, 0.045f, 1.0f },
        { 0.13f, 0.075f, 0.035f, 1.0f },
        { 0.23f, 0.23f, 0.21f, 1.0f }
    };
    static constexpr f32 ROUGHNESS[]{ 0.28f, 0.72f, 0.66f, 0.82f };
    static constexpr f32 METALNESS[]{ 0.92f, 0.84f, 0.0f, 0.0f };
    if ( iPreset < 0 || iPreset >= static_cast<int>(
             CYPHER_ARRAY_COUNT( BASE_COLORS ) ) ) {
        return;
    }

    const QByteArray nameUtf8 = name.toUtf8();
    picasso_paint_material_t pending{};
    const string_view_t nameView{
        nameUtf8.constData(),
        static_cast<usize>( nameUtf8.size() )
    };
    const f32 roughness = ROUGHNESS[iPreset];
    const f32 metalness = METALNESS[iPreset];
    if ( PicassoPaintMaterial_Init( &pending, nameView ) !=
             picasso_paint_material_status_t::OK ||
         PicassoPaintMaterial_SetConstant(
             &pending,
             picasso_channel_semantic_t::BASE_COLOR,
             BASE_COLORS[iPreset] ) !=
             picasso_paint_material_status_t::OK ||
         PicassoPaintMaterial_SetConstant(
             &pending,
             picasso_channel_semantic_t::ROUGHNESS,
             { roughness, roughness, roughness, 1.0f } ) !=
             picasso_paint_material_status_t::OK ||
         PicassoPaintMaterial_SetConstant(
             &pending,
             picasso_channel_semantic_t::METALNESS,
             { metalness, metalness, metalness, 1.0f } ) !=
             picasso_paint_material_status_t::OK ) {
        appendError( tr( "Material preset could not be created: %1" ).arg( name ) );
        return;
    }

    m_material = pending;
    m_currentMaterialPath.clear();
    refreshMaterial();
    appendOutput(
        tr( "Activated material preset: %1" ).arg( name ),
        picasso_console_record_t::INFO,
        picasso_console_channel_t::MATERIAL );
}

bool PicassoMainWindow::saveTexture()
{
    if ( m_currentPath.isEmpty() ||
         QFileInfo( m_currentPath ).suffix().compare(
             QStringLiteral( "png" ),
             Qt::CaseInsensitive ) != 0 ) {
        return saveTextureAs();
    }
    return saveTextureTo( m_currentPath );
}

bool PicassoMainWindow::saveTextureAs()
{
    QString suggested = m_currentPath;
    if ( suggested.isEmpty() ) {
        suggested = QStandardPaths::writableLocation(
            QStandardPaths::PicturesLocation ) + QStringLiteral( "/untitled.png" );
    } else {
        suggested = QFileInfo( suggested ).absolutePath() + QLatin1Char( '/' ) +
                    QFileInfo( suggested ).completeBaseName() + QStringLiteral( ".png" );
    }
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr( "Save Texture" ),
        suggested,
        tr( "PNG Image (*.png)" ) );
    return !path.isEmpty() && saveTextureTo( path );
}

bool PicassoMainWindow::saveTextureTo( const QString &path )
{
    blob_t encoded{};
    const picasso_document_status_t status =
        PicassoTextureDocument_ExportPng( &m_document, &encoded );
    if ( status != picasso_document_status_t::OK ) {
        QMessageBox::critical(
            this,
            tr( "Save Texture" ),
            tr( "PNG encoding failed: %1" )
                .arg( QString::fromLatin1(
                    PicassoTextureDocument_StatusName( status ) ) ) );
        return false;
    }

    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ||
         file.write(
             reinterpret_cast<const char *>( encoded.pData ),
             static_cast<qint64>( encoded.cbSize ) ) !=
             static_cast<qint64>( encoded.cbSize ) ||
         !file.commit() ) {
        QMessageBox::critical( this, tr( "Save Texture" ), file.errorString() );
        return false;
    }

    m_currentPath = path;
    PicassoTextureDocument_MarkSaved( &m_document );
    updateWindowTitle();
    refreshActions();
    appendInfo( tr( "Saved %1 (%2 bytes)." )
        .arg( QDir::toNativeSeparators( path ) )
        .arg( encoded.cbSize ) );
    setStatusMessage( tr( "Saved" ) );
    return true;
}

bool PicassoMainWindow::maybeSave()
{
    if ( !PicassoTextureDocument_IsDirty( &m_document ) ) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        tr( "Unsaved Texture" ),
        tr( "The current texture has unsaved changes." ),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save );
    if ( answer == QMessageBox::Cancel ) {
        return false;
    }
    return answer == QMessageBox::Discard || saveTexture();
}

void PicassoMainWindow::applyOperation(
    picasso_texture_operation_t operation )
{
    const picasso_document_status_t status = PicassoTextureDocument_Apply(
        &m_document,
        operation );
    if ( status != picasso_document_status_t::OK ) {
        appendError(
            tr( "%1 failed: %2" )
                .arg( QString::fromLatin1(
                    PicassoTextureDocument_OperationName( operation ) ) )
                .arg( QString::fromLatin1(
                    PicassoTextureDocument_StatusName( status ) ) ) );
        return;
    }
    refreshDocument();
    appendInfo( QString::fromLatin1(
        PicassoTextureDocument_OperationName( operation ) ) );
}

void PicassoMainWindow::compileTexture()
{
    if ( !PicassoTextureDocument_IsOpen( &m_document ) ) {
        appendOutput(
            tr( "No active texture document to compile." ),
            picasso_console_record_t::ERROR,
            picasso_console_channel_t::COMPILER );
        return;
    }

    const QString sourceName = m_currentPath.isEmpty()
        ? tr( "Untitled.cytex" )
        : QFileInfo( m_currentPath ).fileName();
    appendOutput(
        tr( "Compile requested for %1." ).arg( sourceName ),
        picasso_console_record_t::INFO,
        picasso_console_channel_t::COMPILER );

    // Picasso can already invoke its command surface, but authored .cytex
    // serialization must exist before the compiler bridge can publish a valid
    // proprietary artifact. Report that boundary instead of faking success.
    appendOutput(
        tr( "The .cytex authoring recipe bridge is not available yet; no artifact was written." ),
        picasso_console_record_t::WARNING,
        picasso_console_channel_t::COMPILER );
    setStatusMessage( tr( "Compile requires a .cytex recipe" ) );
}

void PicassoMainWindow::handleCanvasInteraction(
    picasso_canvas_tool_t tool,
    picasso_canvas_interaction_t interaction,
    qreal x,
    qreal y )
{
    if ( tool == picasso_canvas_tool_t::FILL &&
         interaction == picasso_canvas_interaction_t::BEGIN ) {
        if ( x < 0.0 || y < 0.0 || m_pColorSwatches == nullptr ) {
            return;
        }
        const QColor color = m_paintColor;
        const byte replacement[4]{
            static_cast<byte>( color.red() ),
            static_cast<byte>( color.green() ),
            static_cast<byte>( color.blue() ),
            static_cast<byte>( color.alpha() )
        };
        const picasso_document_status_t status =
            PicassoTextureDocument_FloodFill(
                &m_document,
                picasso_channel_semantic_t::BASE_COLOR,
                static_cast<u32>( std::floor( x ) ),
                static_cast<u32>( std::floor( y ) ),
                replacement );
        if ( status == picasso_document_status_t::OK ) {
            refreshDocument( false );
            appendInfo( tr( "Flood Fill" ) );
        } else if ( status != picasso_document_status_t::NOTHING_TO_COMMIT ) {
            appendError( tr( "Flood fill failed: %1" ).arg(
                QString::fromLatin1(
                    PicassoTextureDocument_StatusName( status ) ) ) );
        }
        return;
    }

    if ( tool == picasso_canvas_tool_t::EYEDROPPER &&
         interaction == picasso_canvas_interaction_t::BEGIN ) {
        if ( x < 0.0 || y < 0.0 || m_pColorSwatches == nullptr ) {
            return;
        }
        const const_image_view_t view = PicassoTextureDocument_View(
            &m_document );
        if ( view.desc.pixelFormat != image_pixel_format_t::RGBA8_UNORM ) {
            appendError( tr( "The color picker requires an RGBA8 canvas." ) );
            return;
        }
        const byte *pPixel = ImageView_GetPixel(
            view,
            static_cast<u32>( std::floor( x ) ),
            static_cast<u32>( std::floor( y ) ),
            0u ).pData;
        m_paintColor = QColor(
            pPixel[0], pPixel[1], pPixel[2], pPixel[3] );
        static_cast<PicassoColorSwatches *>( m_pColorSwatches )
            ->setForeground( m_paintColor );
        appendInfo( tr( "Sampled paint color %1" ).arg(
            m_paintColor.name( QColor::HexArgb ) ) );
        return;
    }

    if ( tool != picasso_canvas_tool_t::BRUSH &&
         tool != picasso_canvas_tool_t::ERASER ) {
        return;
    }

    if ( interaction == picasso_canvas_interaction_t::BEGIN ) {
        const picasso_pixel_edit_kind_t editKind =
            tool == picasso_canvas_tool_t::BRUSH
                ? picasso_pixel_edit_kind_t::BRUSH_STROKE
                : picasso_pixel_edit_kind_t::ERASER_STROKE;
        const picasso_document_status_t status =
            PicassoTextureDocument_BeginPixelEdit(
                &m_document,
                picasso_channel_semantic_t::BASE_COLOR,
                editKind );
        if ( status != picasso_document_status_t::OK ) {
            appendError( tr( "Paint stroke could not start: %1" ).arg(
                QString::fromLatin1(
                    PicassoTextureDocument_StatusName( status ) ) ) );
            return;
        }
        m_bHasLastStrokePoint = false;
    }

    if ( interaction == picasso_canvas_interaction_t::CANCEL ) {
        PicassoTextureDocument_CancelPixelEdit( &m_document );
        m_bHasLastStrokePoint = false;
        refreshDocument( false );
        setStatusMessage( tr( "Stroke cancelled" ) );
        return;
    }
    if ( !PicassoTextureDocument_HasActivePixelEdit( &m_document ) ) {
        return;
    }

    QRect dirtyRegion{};
    if ( interaction == picasso_canvas_interaction_t::UPDATE &&
         ( x < 0.0 || y < 0.0 ) ) {
        // Re-entering the canvas begins a fresh segment inside the same undo
        // transaction instead of drawing a line across the outside region.
        m_bHasLastStrokePoint = false;
        return;
    }
    if ( ( interaction == picasso_canvas_interaction_t::BEGIN ||
           interaction == picasso_canvas_interaction_t::UPDATE ||
           interaction == picasso_canvas_interaction_t::END ) &&
         x >= 0.0 && y >= 0.0 &&
         !continuePaintStroke( tool, x, y, &dirtyRegion ) ) {
        PicassoTextureDocument_CancelPixelEdit( &m_document );
        m_bHasLastStrokePoint = false;
        refreshDocument( false );
        appendError( tr( "Paint stroke failed and was rolled back." ) );
        return;
    }
    if ( !dirtyRegion.isEmpty() ) {
        refreshCanvasRegion( dirtyRegion );
    }

    if ( interaction == picasso_canvas_interaction_t::END ) {
        const picasso_document_status_t status =
            PicassoTextureDocument_EndPixelEdit( &m_document );
        m_bHasLastStrokePoint = false;
        refreshDocument( false );
        if ( status == picasso_document_status_t::OK ) {
            appendInfo( tool == picasso_canvas_tool_t::BRUSH
                ? tr( "Brush Stroke" )
                : tr( "Eraser Stroke" ) );
        } else if ( status != picasso_document_status_t::NOTHING_TO_COMMIT ) {
            appendError( tr( "Paint stroke could not commit: %1" ).arg(
                QString::fromLatin1(
                    PicassoTextureDocument_StatusName( status ) ) ) );
        }
    }
}

bool PicassoMainWindow::continuePaintStroke(
    picasso_canvas_tool_t tool,
    qreal x,
    qreal y,
    QRect *pDirtyRegionOut )
{
    if ( pDirtyRegionOut == nullptr ) {
        return false;
    }
    if ( !m_bHasLastStrokePoint ) {
        if ( !applyBrushDab( tool, x, y, pDirtyRegionOut ) ) {
            return false;
        }
        m_lastStrokeX = x;
        m_lastStrokeY = y;
        m_bHasLastStrokePoint = true;
        return true;
    }

    const qreal dx = x - m_lastStrokeX;
    const qreal dy = y - m_lastStrokeY;
    const qreal distance = std::hypot( dx, dy );
    if ( distance < 0.001 ) {
        return true;
    }
    const qreal diameter = m_pBrushSize != nullptr
        ? m_pBrushSize->value()
        : 24.0;
    const qreal spacing = std::max( 1.0, diameter * 0.18 );
    const int nSteps = std::max(
        1,
        static_cast<int>( std::ceil( distance / spacing ) ) );
    for ( int iStep = 1; iStep <= nSteps; ++iStep ) {
        const qreal t = static_cast<qreal>( iStep ) / nSteps;
        QRect dabRegion{};
        if ( !applyBrushDab(
                 tool,
                 m_lastStrokeX + dx * t,
                 m_lastStrokeY + dy * t,
                 &dabRegion ) ) {
            return false;
        }
        *pDirtyRegionOut = pDirtyRegionOut->isEmpty()
            ? dabRegion
            : pDirtyRegionOut->united( dabRegion );
    }
    m_lastStrokeX = x;
    m_lastStrokeY = y;
    return true;
}

bool PicassoMainWindow::applyBrushDab(
    picasso_canvas_tool_t tool,
    qreal x,
    qreal y,
    QRect *pDirtyRegionOut )
{
    if ( pDirtyRegionOut == nullptr || m_pColorSwatches == nullptr ) {
        return false;
    }
    image_surface_t *pSurface = PicassoTextureDocument_PrimarySurface(
        &m_document );
    if ( pSurface == nullptr ) {
        return false;
    }

    const QColor color = m_paintColor;
    picasso_brush_dab_t dab{};
    dab.x = static_cast<f32>( x );
    dab.y = static_cast<f32>( y );
    dab.nDiameter = static_cast<f32>( m_pBrushSize->value() );
    dab.opacity = static_cast<f32>( m_pBrushOpacity->value() ) / 100.0f;
    dab.hardness = static_cast<f32>( m_pBrushHardness->value() ) / 100.0f;
    dab.color[0] = static_cast<byte>( color.red() );
    dab.color[1] = static_cast<byte>( color.green() );
    dab.color[2] = static_cast<byte>( color.blue() );
    dab.color[3] = static_cast<byte>( color.alpha() );
    dab.mode = tool == picasso_canvas_tool_t::ERASER
        ? picasso_brush_mode_t::ERASE
        : picasso_brush_mode_t::PAINT;

    picasso_pixel_rect_t bounds{};
    if ( PicassoPaint_DabBounds(
             ImageSurface_GetView( pSurface ),
             dab,
             &bounds ) != picasso_paint_status_t::OK ||
         PicassoTextureDocument_ApplyDab( &m_document, dab ) !=
             picasso_document_status_t::OK ) {
        return false;
    }
    *pDirtyRegionOut = QRect(
        static_cast<int>( bounds.x ),
        static_cast<int>( bounds.y ),
        static_cast<int>( bounds.nWidth ),
        static_cast<int>( bounds.nHeight ) );
    return true;
}

void PicassoMainWindow::refreshCanvasRegion( const QRect &imageRegion )
{
    const const_image_view_t source = PicassoTextureDocument_View( &m_document );
    if ( source.desc.pixelFormat != image_pixel_format_t::RGBA8_UNORM ) {
        refreshDocument( false );
        return;
    }
    const QImage borrowed(
        source.pixels.pData,
        static_cast<int>( source.desc.extent.nWidth ),
        static_cast<int>( source.desc.extent.nHeight ),
        static_cast<qsizetype>( source.cbRowPitch ),
        QImage::Format_RGBA8888 );
    m_pCanvas->updateImageRegion( borrowed, imageRegion );
}

void PicassoMainWindow::undo()
{
    if ( PicassoTextureDocument_Undo( &m_document ) ==
         picasso_document_status_t::OK ) {
        refreshDocument( false );
        appendInfo( tr( "Undo" ) );
    }
}

void PicassoMainWindow::redo()
{
    if ( PicassoTextureDocument_Redo( &m_document ) ==
         picasso_document_status_t::OK ) {
        refreshDocument( false );
        appendInfo( tr( "Redo" ) );
    }
}

void PicassoMainWindow::jumpToHistory( usize iHistoryCursor )
{
    if ( iHistoryCursor > m_document.nHistoryCount ||
         iHistoryCursor == m_document.iHistoryCursor ) {
        return;
    }

    const usize iOriginalCursor = m_document.iHistoryCursor;
    while ( m_document.iHistoryCursor > iHistoryCursor ) {
        if ( PicassoTextureDocument_Undo( &m_document ) !=
             picasso_document_status_t::OK ) {
            break;
        }
    }
    while ( m_document.iHistoryCursor < iHistoryCursor ) {
        if ( PicassoTextureDocument_Redo( &m_document ) !=
             picasso_document_status_t::OK ) {
            break;
        }
    }
    if ( m_document.iHistoryCursor != iOriginalCursor ) {
        refreshDocument( false );
        appendInfo( tr( "History moved to step %1." )
            .arg( m_document.iHistoryCursor ) );
    }
}

void PicassoMainWindow::refreshDocument( bool bResetCanvasView )
{
    const QImage image = buildDisplayImage();
    m_pCanvas->setImage( image, bResetCanvasView );
    m_pPreview->setImage( image );
    refreshProperties();
    refreshHistory();
    refreshMaterial();
    refreshActions();
    updateWindowTitle();
    setStatusMessage( tr( "Ready" ) );
}

void PicassoMainWindow::refreshMaterial()
{
    if ( m_pMaterialName == nullptr || m_pMaterialShader == nullptr ||
         m_pMaterialChannels == nullptr ) {
        return;
    }

    m_pMaterialName->setText( StringFromView(
        FixedString_View( m_material.name ) ) );
    const QString shader = FixedString_IsEmpty( m_material.shader )
        ? tr( "Shader: not assigned" )
        : tr( "Shader: %1" ).arg( StringFromView(
              FixedString_View( m_material.shader ) ) );
    m_pMaterialShader->setText( shader );
    m_pMaterialShader->setToolTip( shader );

    const QSignalBlocker blocker( m_pMaterialChannels );
    m_pMaterialChannels->clear();
    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const auto semantic =
            static_cast<picasso_channel_semantic_t>( iChannel );
        const picasso_material_channel_t *pChannel =
            PicassoPaintMaterial_GetChannel( &m_material, semantic );
        if ( pChannel == nullptr ) {
            continue;
        }

        auto *pItem = new QTreeWidgetItem( m_pMaterialChannels );
        pItem->setText( 0, QString::fromLatin1(
            PicassoChannel_Name( semantic ) ) );
        if ( pChannel->kind ==
             picasso_material_source_kind_t::TEXTURE_RESOURCE ) {
            const QString texture = StringFromView(
                FixedString_View( pChannel->texture ) );
            pItem->setText( 1, QFileInfo( texture ).fileName() );
            pItem->setToolTip( 1, texture );
            pItem->setIcon( 0, PicassoIcon_Create(
                u"image", picasso_icon_tone_t::BLUE ) );
        } else {
            pItem->setText( 1, tr( "Constant" ) );
            pItem->setIcon( 0, PicassoIcon_Create(
                u"swatch-book", picasso_icon_tone_t::TEAL ) );
        }
    }
    m_pMaterialChannels->resizeColumnToContents( 0 );
}

void PicassoMainWindow::refreshActions()
{
    const bool bOpen = PicassoTextureDocument_IsOpen( &m_document );
    m_pSaveAction->setEnabled( bOpen );
    m_pSaveAsAction->setEnabled( bOpen );
    m_pCompileAction->setEnabled( bOpen );
    m_pUndoAction->setEnabled( PicassoTextureDocument_CanUndo( &m_document ) );
    m_pRedoAction->setEnabled( PicassoTextureDocument_CanRedo( &m_document ) );
    for ( QAction *pAction : {
              m_pFlipHorizontalAction,
              m_pFlipVerticalAction,
              m_pRotateClockwiseAction,
              m_pRotateCounterClockwiseAction,
              m_pRotate180Action } ) {
        pAction->setEnabled( bOpen );
    }
}

void PicassoMainWindow::refreshProperties()
{
    if ( !PicassoTextureDocument_IsOpen( &m_document ) ) {
        return;
    }
    const image_surface_t *pPrimary =
        PicassoTextureDocument_PrimarySurface( &m_document );
    const image_desc_t &desc = pPrimary->desc;
    const QString dimensions = QStringLiteral( "%1 x %2" )
        .arg( desc.extent.nWidth )
        .arg( desc.extent.nHeight );
    m_pDimensionsValue->setText( dimensions );
    m_pFormatValue->setText( QString::fromLatin1(
        ImageFormat_Name( desc.pixelFormat ) ) );
    m_pColorSpaceValue->setText( ColorSpaceName( desc.colorSpace ) );
    m_pSourceValue->setText(
        m_currentPath.isEmpty()
            ? tr( "Generated" )
            : QFileInfo( m_currentPath ).fileName() );
    m_pSourceValue->setToolTip( m_currentPath );
    m_pStatusDimensions->setText( dimensions );
    m_pDocumentInfo->setText( QStringLiteral( "%1    %2" ).arg(
        dimensions,
        QString::fromLatin1( ImageFormat_Name( desc.pixelFormat ) ) ) );
    const double memoryMiB = static_cast<double>(
        PicassoTextureSet_ByteSize( &m_document.textureSet ) ) /
        static_cast<double>( CY_MIB );
    m_pStatusMemory->setText(
        tr( "Image: %1 MiB" ).arg( memoryMiB, 0, 'f', 1 ) );
}

void PicassoMainWindow::refreshHistory()
{
    if ( m_pHistoryList == nullptr ) {
        return;
    }
    const QSignalBlocker blocker( m_pHistoryList );
    m_pHistoryList->clear();

    const QString sourceName = m_currentPath.isEmpty()
        ? tr( "Create Texture" )
        : tr( "Open %1" ).arg( QFileInfo( m_currentPath ).fileName() );
    auto *pRoot = new QListWidgetItem(
        PicassoIcon_Create( u"folder-open", picasso_icon_tone_t::GOLD ),
        sourceName,
        m_pHistoryList );
    pRoot->setData( Qt::UserRole, qulonglong( 0u ) );

    for ( usize iEntry = 0u; iEntry < m_document.nHistoryCount; ++iEntry ) {
        const picasso_texture_history_entry_t &entry = m_document.history[iEntry];
        auto *pItem = new QListWidgetItem(
            PicassoIcon_Create(
                u"sliders-horizontal", picasso_icon_tone_t::TEAL ),
            QString::fromLatin1(
                PicassoTextureDocument_HistoryName( entry ) ),
            m_pHistoryList );
        const usize iCursor = iEntry + 1u;
        pItem->setData( Qt::UserRole, qulonglong( iCursor ) );
        if ( iCursor > m_document.iHistoryCursor ) {
            pItem->setForeground( QColor( 91, 99, 106 ) );
        }
    }

    const int currentRow = static_cast<int>( m_document.iHistoryCursor );
    if ( currentRow >= 0 && currentRow < m_pHistoryList->count() ) {
        QListWidgetItem *pCurrent = m_pHistoryList->item( currentRow );
        pCurrent->setText( pCurrent->text() + tr( "    CURRENT" ) );
        m_pHistoryList->setCurrentItem( pCurrent );
        m_pHistoryList->scrollToItem( pCurrent );
    }
}

void PicassoMainWindow::refreshToolContext()
{
    if ( m_pStatusBrush == nullptr || m_pToolActions == nullptr ) {
        return;
    }
    QAction *pTool = m_pToolActions->checkedAction();
    const QString toolName = pTool != nullptr ? pTool->text() : tr( "Select" );
    const picasso_canvas_tool_t tool = pTool != nullptr
        ? static_cast<picasso_canvas_tool_t>( pTool->data().toInt() )
        : picasso_canvas_tool_t::SELECT;
    const bool bUsesBrushOptions =
        tool == picasso_canvas_tool_t::BRUSH ||
        tool == picasso_canvas_tool_t::ERASER ||
        tool == picasso_canvas_tool_t::CLONE;

    if ( m_pContextToolButton != nullptr ) {
        m_pContextToolButton->setText( toolName );
        m_pContextToolButton->setIcon(
            pTool != nullptr ? pTool->icon() : m_pSelectToolAction->icon() );
        m_pContextToolButton->setToolTip(
            pTool != nullptr ? pTool->toolTip() : toolName );
    }
    if ( m_pBrushContextAction != nullptr ) {
        m_pBrushContextAction->setVisible( bUsesBrushOptions );
    }

    if ( !bUsesBrushOptions ) {
        m_pStatusBrush->setText( toolName );
        return;
    }

    m_pStatusBrush->setText(
        tr( "%1 | Size: %2px  Opacity: %3%  Hardness: %4%  %5" )
            .arg( toolName )
            .arg( m_pBrushSize != nullptr ? m_pBrushSize->value() : 24 )
            .arg( m_pBrushOpacity != nullptr ? m_pBrushOpacity->value() : 80 )
            .arg( m_pBrushHardness != nullptr ? m_pBrushHardness->value() : 90 )
            .arg( m_pBlendMode != nullptr
                ? m_pBlendMode->currentText()
                : tr( "Normal" ) ) );
}

void PicassoMainWindow::updateWindowTitle()
{
    const QString name = m_currentPath.isEmpty()
        ? tr( "Untitled" )
        : QFileInfo( m_currentPath ).fileName();
    const QString dirty = PicassoTextureDocument_IsDirty( &m_document )
        ? QStringLiteral( "*" )
        : QString();
    setWindowTitle( tr( "%1%2 - Picasso" ).arg( name, dirty ) );
    setWindowModified( PicassoTextureDocument_IsDirty( &m_document ) );
    if ( m_pDocumentName != nullptr ) {
        m_pDocumentName->setText( tr( "%1%2" ).arg(
            m_currentPath.isEmpty() ? tr( "Untitled.cytex" ) : name,
            dirty ) );
    }
}

void PicassoMainWindow::appendOutput(
    const QString &message,
    picasso_console_record_t type,
    picasso_console_channel_t channel )
{
    if ( m_pConsole != nullptr ) {
        m_pConsole->appendRecord( type, channel, message );
    }
    setStatusMessage(
        type == picasso_console_record_t::ERROR
            ? tr( "Operation failed" )
            : tr( "Ready" ) );
}

void PicassoMainWindow::appendInfo( const QString &message )
{
    appendOutput(
        message,
        picasso_console_record_t::INFO,
        picasso_console_channel_t::PICASSO );
}

void PicassoMainWindow::appendWarning( const QString &message )
{
    appendOutput(
        message,
        picasso_console_record_t::WARNING,
        picasso_console_channel_t::MATERIAL );
}

void PicassoMainWindow::appendError( const QString &message )
{
    appendOutput(
        message,
        picasso_console_record_t::ERROR,
        picasso_console_channel_t::PICASSO );
}

void PicassoMainWindow::setStatusMessage( const QString &message )
{
    m_pStatusMessage->setText( message );
}

QImage PicassoMainWindow::buildDisplayImage() const
{
    const const_image_view_t source = PicassoTextureDocument_View( &m_document );
    if ( !ImageView_IsValid( source ) ) {
        return {};
    }
    if ( source.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM ) {
        return QImage(
            source.pixels.pData,
            static_cast<int>( source.desc.extent.nWidth ),
            static_cast<int>( source.desc.extent.nHeight ),
            static_cast<qsizetype>( source.cbRowPitch ),
            QImage::Format_RGBA8888 ).copy();
    }

    image_desc_t displayDesc = source.desc;
    displayDesc.pixelFormat = image_pixel_format_t::RGBA8_UNORM;
    displayDesc.colorSpace = image_color_space_t::SRGB;
    image_surface_t display{};
    if ( ImageSurface_Create(
             &display,
             Allocator_GetSystem(),
             displayDesc,
             image_surface_init_t::UNINITIALIZED,
             4u ) != image_surface_status_t::OK ||
         ImageConvert(
             ImageSurface_GetView( &display ),
             source ) != image_convert_status_t::OK ) {
        return {};
    }
    const const_image_view_t converted = ImageSurface_GetView(
        static_cast<const image_surface_t *>( &display ) );
    return QImage(
        converted.pixels.pData,
        static_cast<int>( converted.desc.extent.nWidth ),
        static_cast<int>( converted.desc.extent.nHeight ),
        static_cast<qsizetype>( converted.cbRowPitch ),
        QImage::Format_RGBA8888 ).copy();
}

} // namespace cypher::tools::picasso
