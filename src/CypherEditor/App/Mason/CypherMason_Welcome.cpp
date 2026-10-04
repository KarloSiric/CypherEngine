//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_Welcome.cpp
//  Purpose: Implements Mason's welcome window.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMason_Welcome.h"

#include "CypherMason_Branding.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace cypher::mason
{

using namespace cypher::editor::gui;

namespace
{

constexpr int kFileRole = Qt::UserRole;

class welcome_t final : public QDialog {
public:
    welcome_t( QWidget *pParent, const editor_style_t &style, const QStringList &recentMaps, bool bShowAtStartup ) : QDialog( pParent )
    {
        setObjectName( QStringLiteral( "MasonWelcome" ) );
        setWindowTitle( QStringLiteral( "Welcome to Mason" ) );
        resize( 760, 420 );
        auto *pRoot = new QHBoxLayout( this );
        pRoot->setContentsMargins( 0, 0, 0, 0 );
        pRoot->setSpacing( 0 );

        // Left: who we are and the two ways in.
        auto *pIntro = new QWidget( this );
        pIntro->setObjectName( QStringLiteral( "MasonWelcomeIntro" ) );
        pIntro->setFixedWidth( 280 );
        auto *pIntroLayout = new QVBoxLayout( pIntro );
        pIntroLayout->setContentsMargins( 24, 28, 24, 18 );
        pIntroLayout->setSpacing( 6 );
        auto *pIcon = new QLabel( pIntro );
        pIcon->setPixmap( Mason_ApplicationIcon().pixmap( QSize( 128, 128 ) ) );
        pIcon->setAlignment( Qt::AlignHCenter );
        auto *pName = new QLabel( QStringLiteral( "Mason" ), pIntro );
        QFont nameFont = style.uiFont;
        nameFont.setPointSizeF( nameFont.pointSizeF() * 2.2 );
        nameFont.setBold( true );
        pName->setFont( nameFont );
        pName->setAlignment( Qt::AlignHCenter );
        auto *pTagline = new QLabel( QStringLiteral( "CypherEngine world editor\nVersion %1" ).arg( QString::fromLatin1( Mason_Version() ) ), pIntro );
        pTagline->setAlignment( Qt::AlignHCenter );
        pTagline->setProperty( "muted", true );
        auto *pNew = new QPushButton( EditorStyle_Icon( style, "file-new" ), QStringLiteral( "New Map" ), pIntro );
        pNew->setObjectName( QStringLiteral( "MasonWelcomeNew" ) );
        auto *pOpen = new QPushButton( EditorStyle_Icon( style, "file-open" ), QStringLiteral( "Open Map..." ), pIntro );
        pOpen->setObjectName( QStringLiteral( "MasonWelcomeOpen" ) );
        for ( QPushButton *pButton : { pNew, pOpen } ) {
            pButton->setIconSize( QSize( 22, 22 ) );
            pButton->setMinimumHeight( 34 );
        }
        m_pShowAtStartup = new QCheckBox( QStringLiteral( "Show at startup" ), pIntro );
        m_pShowAtStartup->setObjectName( QStringLiteral( "MasonWelcomeShowAtStartup" ) );
        m_pShowAtStartup->setChecked( bShowAtStartup );
        pIntroLayout->addWidget( pIcon );
        pIntroLayout->addWidget( pName );
        pIntroLayout->addWidget( pTagline );
        pIntroLayout->addStretch( 1 );
        pIntroLayout->addWidget( pNew );
        pIntroLayout->addWidget( pOpen );
        pIntroLayout->addSpacing( 10 );
        pIntroLayout->addWidget( m_pShowAtStartup );
        pRoot->addWidget( pIntro );

        // Right: the recent maps, newest first.
        auto *pRecent = new QWidget( this );
        auto *pRecentLayout = new QVBoxLayout( pRecent );
        pRecentLayout->setContentsMargins( 0, 0, 0, 0 );
        pRecentLayout->setSpacing( 0 );
        auto *pTitle = new QLabel( QStringLiteral( "Recent Maps" ), pRecent );
        pTitle->setObjectName( QStringLiteral( "MasonWelcomeRecentTitle" ) );
        m_pList = new QListWidget( pRecent );
        m_pList->setObjectName( QStringLiteral( "MasonWelcomeRecent" ) );
        m_pList->setIconSize( QSize( 32, 32 ) );
        m_pList->setSpacing( 1 );
        pRecentLayout->addWidget( pTitle );
        pRecentLayout->addWidget( m_pList, 1 );
        pRoot->addWidget( pRecent, 1 );

        const QIcon mapIcon = EditorStyle_Icon( style, "asset-map" );
        for ( const QString &file : recentMaps ) {
            const QFileInfo info( file );
            const bool bExists = info.exists();
            // The folder holding a chunked map is named after it; show the
            // folder above that, where the map lives in the project.
            const QString folder = QDir::toNativeSeparators( info.absolutePath() );
            auto *pItem = new QListWidgetItem( mapIcon, QStringLiteral( "%1\n%2" ).arg( info.completeBaseName(), bExists ? folder : folder + QStringLiteral( "  (missing)" ) ), m_pList );
            pItem->setData( kFileRole, file );
            pItem->setToolTip( bExists ? QStringLiteral( "%1\nLast saved %2" ).arg( QDir::toNativeSeparators( file ), info.lastModified().toString( QStringLiteral( "yyyy-MM-dd HH:mm" ) ) )
                                       : QStringLiteral( "%1\nNot found" ).arg( QDir::toNativeSeparators( file ) ) );
            if ( !bExists ) {
                pItem->setFlags( pItem->flags() & ~Qt::ItemIsEnabled );
                pItem->setForeground( EditorStyle_TokenColor( style, "ui.text.disabled" ) );
            }
        }
        if ( recentMaps.isEmpty() ) {
            auto *pItem = new QListWidgetItem( QStringLiteral( "Maps you open appear here." ), m_pList );
            pItem->setFlags( Qt::NoItemFlags );
        }

        QObject::connect( pNew, &QPushButton::clicked, this, [this]() { Choose( mason_welcome_choice_t::NEW_MAP ); } );
        QObject::connect( pOpen, &QPushButton::clicked, this, [this]() { Choose( mason_welcome_choice_t::BROWSE ); } );
        QObject::connect( m_pList, &QListWidget::itemActivated, this, [this]( QListWidgetItem *pItem ) {
            if ( pItem != nullptr && ( pItem->flags() & Qt::ItemIsEnabled ) != 0 ) { Choose( mason_welcome_choice_t::OPEN, m_pList->row( pItem ) ); }
        } );
    }

    void Choose( mason_welcome_choice_t choice, int iRecent = -1 )
    {
        m_choice = choice;
        m_file = choice == mason_welcome_choice_t::OPEN && iRecent >= 0 && iRecent < m_pList->count()
                     ? m_pList->item( iRecent )->data( kFileRole ).toString()
                     : QString();
        accept();
    }

    QStringList Rows() const
    {
        QStringList rows;
        for ( int i = 0; i < m_pList->count(); ++i ) {
            const QString file = m_pList->item( i )->data( kFileRole ).toString();
            if ( !file.isEmpty() ) { rows.append( QStringLiteral( "%1\t%2" ).arg( QFileInfo( file ).completeBaseName(), QFileInfo( file ).absolutePath() ) ); }
        }
        return rows;
    }

    mason_welcome_choice_t m_choice{ mason_welcome_choice_t::NONE };
    QString m_file{};
    QCheckBox *m_pShowAtStartup{ nullptr };
    QListWidget *m_pList{ nullptr };
};

welcome_t *AsWelcome( QDialog *pWelcome )
{
    return dynamic_cast<welcome_t *>( pWelcome );
}

} // namespace

QDialog *MasonWelcome_Create( QWidget *pParent, const editor_style_t &style, const QStringList &recentMaps, bool bShowAtStartup )
{
    return new welcome_t( pParent, style, recentMaps, bShowAtStartup );
}

mason_welcome_choice_t MasonWelcome_Choice( QDialog *pWelcome )
{
    welcome_t *pImpl = AsWelcome( pWelcome );
    return pImpl != nullptr ? pImpl->m_choice : mason_welcome_choice_t::NONE;
}

QString MasonWelcome_ChosenFile( QDialog *pWelcome )
{
    welcome_t *pImpl = AsWelcome( pWelcome );
    return pImpl != nullptr ? pImpl->m_file : QString();
}

bool MasonWelcome_ShowAtStartup( QDialog *pWelcome )
{
    welcome_t *pImpl = AsWelcome( pWelcome );
    return pImpl != nullptr && pImpl->m_pShowAtStartup->isChecked();
}

QStringList MasonWelcome_Rows( QDialog *pWelcome )
{
    welcome_t *pImpl = AsWelcome( pWelcome );
    return pImpl != nullptr ? pImpl->Rows() : QStringList{};
}

void MasonWelcome_Choose( QDialog *pWelcome, mason_welcome_choice_t choice, int iRecent )
{
    if ( welcome_t *pImpl = AsWelcome( pWelcome ) ) { pImpl->Choose( choice, iRecent ); }
}

} // namespace cypher::mason
